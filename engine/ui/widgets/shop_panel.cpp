#include "engine/ui/widgets/shop_panel.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <SDL_scancode.h>
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace fate {

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------
static constexpr float kHeaderH      = 30.0f;
static constexpr float kPanePad      = 6.0f;
static constexpr float kRowH         = 36.0f;
static constexpr float kSlotSize     = 40.0f;
static constexpr float kSlotPad      = 4.0f;
static constexpr int   kGridCols     = 4;
static constexpr int   kGridRows     = 4;
static constexpr float kGoldBarH     = 28.0f;
static constexpr float kBuyBtnW      = 42.0f;
static constexpr float kBuyBtnH      = 22.0f;
static constexpr float kConfirmW     = 220.0f;
static constexpr float kConfirmH     = 120.0f;
static constexpr float kDoubleClickT = 0.3f;
static constexpr float kErrorDuration = 3.0f;

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------
static const Color kBgColor          = {0.08f, 0.08f, 0.12f, 0.95f};
static const Color kTextColor        = {0.9f,  0.9f,  0.85f, 1.0f};
static const Color kGoldColor        = {1.0f,  0.84f, 0.0f,  1.0f};
static const Color kBuyBtnColor      = {0.15f, 0.3f,  0.15f, 0.8f};
static const Color kBuyBtnDisabled   = {0.2f,  0.2f,  0.2f,  0.5f};
static const Color kSlotBg           = {0.12f, 0.12f, 0.18f, 0.8f};
static const Color kSoulboundTint    = {0.4f,  0.4f,  0.4f,  0.5f};
static const Color kErrorColor       = {0.8f,  0.2f,  0.2f,  1.0f};
static const Color kHeaderBg         = {0.12f, 0.12f, 0.18f, 0.9f};
static const Color kDividerColor     = {0.3f,  0.3f,  0.4f,  0.6f};
static const Color kConfirmBg        = {0.1f,  0.1f,  0.16f, 0.97f};
static const Color kConfirmBdr       = {0.4f,  0.4f,  0.55f, 0.9f};
static const Color kConfirmBtnColor  = {0.15f, 0.25f, 0.45f, 0.85f};
static const Color kCancelBtnColor   = {0.35f, 0.15f, 0.15f, 0.85f};
static const Color kCloseXColor      = {1.0f,  0.9f,  0.9f,  1.0f};
static const Color kCloseBg          = {0.40f, 0.12f, 0.12f, 0.90f};
static const Color kCloseBdr         = {0.65f, 0.25f, 0.25f, 1.0f};
static const Color kRowAlt           = {0.0f,  0.0f,  0.0f,  0.15f};
static const Color kStockColor       = {0.6f,  0.6f,  0.7f,  0.8f};
static const Color kQtyColor         = {1.0f,  1.0f,  0.85f, 1.0f};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
ShopPanel::ShopPanel(const std::string& id)
    : UINode(id, "shop_panel") {}

// ---------------------------------------------------------------------------
// open / close / rebuild
// ---------------------------------------------------------------------------
void ShopPanel::open(uint32_t npc, const std::string& name,
                     const std::vector<ShopItem>& items) {
    npcId = npc;
    shopName = name;
    shopItems.clear();
    shopItems.reserve(items.size());
    for (const auto& si : items) {
        ShopEntry entry;
        entry.itemId    = si.itemId;
        entry.itemName  = si.itemName;
        entry.buyPrice  = si.buyPrice;
        entry.sellPrice = si.sellPrice;
        entry.stock     = (si.stock == 0) ? -1 : static_cast<int16_t>(si.stock);
        shopItems.push_back(std::move(entry));
    }
    rebuild();
    setVisible(true);
}

void ShopPanel::close() {
    setVisible(false);
    showSellConfirm_ = false;
    sellSlot_ = 0;
    sellMaxQty_ = 0;
    sellInputQty_ = 0;
    lastClickSlot_ = -1;
    if (onClose) onClose(id_);
}

void ShopPanel::rebuild() {
    scrollOffsetShop_ = 0.0f;
    showSellConfirm_ = false;
    lastClickSlot_ = -1;
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------
void ShopPanel::update(float dt) {
    timeSinceStart_ += dt;
    if (errorTimer > 0.0f) {
        errorTimer -= dt;
        if (errorTimer < 0.0f) {
            errorTimer = 0.0f;
            errorMessage.clear();
        }
    }
}

// ---------------------------------------------------------------------------
// Layout helpers
// ---------------------------------------------------------------------------
Rect ShopPanel::getShopListArea() const {
    const auto& r = computedRect_;
    float paneW = (r.w - kPanePad * 3.0f) * 0.5f;
    float topY  = kHeaderH + kPanePad;
    float areaH = r.h - topY - kGoldBarH - kPanePad;
    // Sub-header for "Shop Items"
    float subHeaderH = 22.0f;
    return {kPanePad, topY + subHeaderH, paneW, areaH - subHeaderH};
}

Rect ShopPanel::getInventoryGridArea() const {
    const auto& r = computedRect_;
    float paneW = (r.w - kPanePad * 3.0f) * 0.5f;
    float leftPaneW = paneW;
    float topY  = kHeaderH + kPanePad;
    float areaH = r.h - topY - kGoldBarH - kPanePad;
    float subHeaderH = 22.0f;
    return {kPanePad * 2.0f + leftPaneW, topY + subHeaderH, paneW, areaH - subHeaderH};
}

int ShopPanel::hitTestInventorySlot(const Vec2& localPos) const {
    Rect area = getInventoryGridArea();
    float gridStartX = area.x + kSlotPad;
    float gridStartY = area.y + kSlotPad;

    for (int row = 0; row < kGridRows; ++row) {
        for (int col = 0; col < kGridCols; ++col) {
            int idx = row * kGridCols + col;
            if (idx >= MAX_SLOTS) return -1;

            float cx = gridStartX + kSlotSize * 0.5f + static_cast<float>(col) * (kSlotSize + kSlotPad);
            float cy = gridStartY + kSlotSize * 0.5f + static_cast<float>(row) * (kSlotSize + kSlotPad);

            if (localPos.x >= cx - kSlotSize * 0.5f && localPos.x <= cx + kSlotSize * 0.5f &&
                localPos.y >= cy - kSlotSize * 0.5f && localPos.y <= cy + kSlotSize * 0.5f) {
                return idx;
            }
        }
    }
    return -1;
}

int ShopPanel::hitTestShopBuyButton(const Vec2& localPos) const {
    Rect area = getShopListArea();
    int maxVisible = static_cast<int>(area.h / kRowH);
    int scrollRow = static_cast<int>(scrollOffsetShop_ / kRowH);
    if (scrollRow < 0) scrollRow = 0;

    for (int i = 0; i < maxVisible; ++i) {
        int itemIdx = scrollRow + i;
        if (itemIdx >= static_cast<int>(shopItems.size())) break;

        float rowY = area.y + static_cast<float>(i) * kRowH;
        float btnX = area.x + area.w - kBuyBtnW - 4.0f;
        float btnY = rowY + (kRowH - kBuyBtnH) * 0.5f;

        if (localPos.x >= btnX && localPos.x <= btnX + kBuyBtnW &&
            localPos.y >= btnY && localPos.y <= btnY + kBuyBtnH) {
            return itemIdx;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Render: main
// ---------------------------------------------------------------------------
void ShopPanel::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // ---- Background ----
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, kBgColor, d);

    // ---- Border ----
    float bw = 2.0f;
    Color bdr = {0.3f, 0.3f, 0.4f, 0.8f};
    float innerH = rect.h - bw * 2.0f;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f},              {rect.w, bw}, bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f},     {rect.w, bw}, bdr, d + 0.1f);
    batch.drawRect({rect.x + bw * 0.5f,     rect.y + rect.h * 0.5f},          {bw, innerH}, bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f},     {bw, innerH}, bdr, d + 0.1f);

    // ---- Header bar ----
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + kHeaderH * 0.5f},
                   {rect.w, kHeaderH}, kHeaderBg, d + 0.1f);

    // Shop title
    Vec2 titleSize = sdf.measure(shopName.c_str(), 14.0f);
    sdf.drawScreen(batch, shopName.c_str(),
        {rect.x + (rect.w - titleSize.x) * 0.5f, rect.y + (kHeaderH - 14.0f) * 0.5f},
        14.0f, kTextColor, d + 0.2f);

    // ---- Close button (X circle, top-right) ----
    float closeR  = 10.0f;
    float closeCX = rect.x + rect.w - closeR - 5.0f;
    float closeCY = rect.y + closeR + 5.0f;
    batch.drawCircle({closeCX, closeCY}, closeR, kCloseBg,  d + 0.2f, 16);
    batch.drawRing  ({closeCX, closeCY}, closeR, 1.0f, kCloseBdr, d + 0.3f, 16);
    Vec2 xts = sdf.measure("X", 10.0f);
    sdf.drawScreen(batch, "X",
        {closeCX - xts.x * 0.5f, closeCY - xts.y * 0.5f},
        10.0f, kCloseXColor, d + 0.4f);

    // ---- Divider below header ----
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + kHeaderH},
                   {rect.w - bw * 2.0f, 1.0f}, kDividerColor, d + 0.1f);

    // ---- Two panes ----
    renderShopPane(batch, sdf, d);
    renderInventoryPane(batch, sdf, d);

    // ---- Gold bar ----
    renderGoldBar(batch, sdf, d);

    // ---- Error message ----
    if (errorTimer > 0.0f) {
        renderError(batch, sdf, d);
    }

    // ---- Sell confirmation overlay ----
    if (showSellConfirm_) {
        renderSellConfirm(batch, sdf, d);
    }

    renderChildren(batch, sdf);
}

// ---------------------------------------------------------------------------
// Render: shop pane (left half)
// ---------------------------------------------------------------------------
void ShopPanel::renderShopPane(SpriteBatch& batch, SDFText& sdf, float depth) {
    const auto& rect = computedRect_;
    float paneW = (rect.w - kPanePad * 3.0f) * 0.5f;
    float topY  = rect.y + kHeaderH + kPanePad;

    // Sub-header: "Shop Items"
    sdf.drawScreen(batch, "Shop Items",
        {rect.x + kPanePad, topY + 3.0f},
        11.0f, kTextColor, depth + 0.2f);

    Rect area = getShopListArea();
    // Offset to screen coords
    area.x += rect.x;
    area.y += rect.y;

    int maxVisible = static_cast<int>(area.h / kRowH);
    int scrollRow = static_cast<int>(scrollOffsetShop_ / kRowH);
    if (scrollRow < 0) scrollRow = 0;

    for (int i = 0; i < maxVisible; ++i) {
        int itemIdx = scrollRow + i;
        if (itemIdx >= static_cast<int>(shopItems.size())) break;

        const ShopEntry& entry = shopItems[static_cast<size_t>(itemIdx)];
        float rowY = area.y + static_cast<float>(i) * kRowH;
        float rowCY = rowY + kRowH * 0.5f;

        // Alternate row shading
        if (i % 2 == 1) {
            batch.drawRect({area.x + area.w * 0.5f, rowCY},
                           {area.w, kRowH}, kRowAlt, depth + 0.05f);
        }

        // Item name (left side)
        float nameX = area.x + 4.0f;
        float nameY = rowY + 4.0f;
        sdf.drawScreen(batch, entry.itemName.c_str(),
            {nameX, nameY}, 10.0f, kTextColor, depth + 0.2f);

        // Price below name
        char priceBuf[32];
        snprintf(priceBuf, sizeof(priceBuf), "%lld G",
                 static_cast<long long>(entry.buyPrice));
        sdf.drawScreen(batch, priceBuf,
            {nameX, nameY + 13.0f}, 9.0f, kGoldColor, depth + 0.2f);

        // Stock indicator (right of price)
        if (entry.stock >= 0) {
            char stockBuf[24];
            snprintf(stockBuf, sizeof(stockBuf), "x%d", entry.stock);
            float stockX = nameX + 80.0f;
            sdf.drawScreen(batch, stockBuf,
                {stockX, nameY + 13.0f}, 8.0f, kStockColor, depth + 0.2f);
        }

        // Buy button
        float btnX = area.x + area.w - kBuyBtnW - 4.0f;
        float btnY = rowY + (kRowH - kBuyBtnH) * 0.5f;
        float btnCX = btnX + kBuyBtnW * 0.5f;
        float btnCY = btnY + kBuyBtnH * 0.5f;

        bool canAfford = playerGold >= entry.buyPrice;
        bool inStock   = entry.stock < 0 || entry.stock > 0;
        bool canBuy    = canAfford && inStock;

        Color btnColor = canBuy ? kBuyBtnColor : kBuyBtnDisabled;
        batch.drawRect({btnCX, btnCY}, {kBuyBtnW, kBuyBtnH}, btnColor, depth + 0.15f);

        // Button border
        float bbw = 1.0f;
        Color btnBdr = canBuy ? Color{0.3f, 0.5f, 0.3f, 0.8f} : Color{0.3f, 0.3f, 0.3f, 0.5f};
        float btnInnerH = kBuyBtnH - bbw * 2.0f;
        batch.drawRect({btnCX, btnY + bbw * 0.5f},                  {kBuyBtnW, bbw}, btnBdr, depth + 0.2f);
        batch.drawRect({btnCX, btnY + kBuyBtnH - bbw * 0.5f},       {kBuyBtnW, bbw}, btnBdr, depth + 0.2f);
        batch.drawRect({btnX + bbw * 0.5f, btnCY},                  {bbw, btnInnerH}, btnBdr, depth + 0.2f);
        batch.drawRect({btnX + kBuyBtnW - bbw * 0.5f, btnCY},       {bbw, btnInnerH}, btnBdr, depth + 0.2f);

        // "Buy" label
        Color btnTextColor = canBuy ? Color{0.85f, 0.95f, 0.85f, 1.0f}
                                    : Color{0.5f, 0.5f, 0.5f, 0.7f};
        Vec2 buyTs = sdf.measure("Buy", 9.0f);
        sdf.drawScreen(batch, "Buy",
            {btnCX - buyTs.x * 0.5f, btnCY - buyTs.y * 0.5f},
            9.0f, btnTextColor, depth + 0.3f);
    }

    // Vertical divider between panes
    float divX = rect.x + kPanePad + paneW + kPanePad * 0.5f;
    float divY1 = rect.y + kHeaderH + kPanePad;
    float divH  = rect.h - kHeaderH - kGoldBarH - kPanePad * 2.0f;
    batch.drawRect({divX, divY1 + divH * 0.5f}, {1.0f, divH}, kDividerColor, depth + 0.1f);
}

// ---------------------------------------------------------------------------
// Render: inventory pane (right half)
// ---------------------------------------------------------------------------
void ShopPanel::renderInventoryPane(SpriteBatch& batch, SDFText& sdf, float depth) {
    const auto& rect = computedRect_;
    float paneW = (rect.w - kPanePad * 3.0f) * 0.5f;
    float rightX = rect.x + kPanePad * 2.0f + paneW;
    float topY   = rect.y + kHeaderH + kPanePad;

    // Sub-header: "Your Inventory"
    sdf.drawScreen(batch, "Your Inventory",
        {rightX, topY + 3.0f},
        11.0f, kTextColor, depth + 0.2f);

    Rect area = getInventoryGridArea();
    // Offset to screen coords
    area.x += rect.x;
    area.y += rect.y;

    float gridStartX = area.x + kSlotPad;
    float gridStartY = area.y + kSlotPad;

    for (int row = 0; row < kGridRows; ++row) {
        for (int col = 0; col < kGridCols; ++col) {
            int idx = row * kGridCols + col;
            if (idx >= MAX_SLOTS) break;

            float cx = gridStartX + kSlotSize * 0.5f + static_cast<float>(col) * (kSlotSize + kSlotPad);
            float cy = gridStartY + kSlotSize * 0.5f + static_cast<float>(row) * (kSlotSize + kSlotPad);

            const InvSlot& slot = playerItems[idx];
            bool hasItem = !slot.itemId.empty();

            // Slot background
            batch.drawRect({cx, cy}, {kSlotSize, kSlotSize}, kSlotBg, depth + 0.1f);

            // Slot border
            float sbw = 1.0f;
            Color slotBdr = {0.25f, 0.25f, 0.35f, 0.6f};
            float sInnerH = kSlotSize - sbw * 2.0f;
            batch.drawRect({cx, cy - kSlotSize * 0.5f + sbw * 0.5f}, {kSlotSize, sbw}, slotBdr, depth + 0.15f);
            batch.drawRect({cx, cy + kSlotSize * 0.5f - sbw * 0.5f}, {kSlotSize, sbw}, slotBdr, depth + 0.15f);
            batch.drawRect({cx - kSlotSize * 0.5f + sbw * 0.5f, cy}, {sbw, sInnerH}, slotBdr, depth + 0.15f);
            batch.drawRect({cx + kSlotSize * 0.5f - sbw * 0.5f, cy}, {sbw, sInnerH}, slotBdr, depth + 0.15f);

            if (hasItem) {
                // Soulbound overlay
                if (slot.soulbound) {
                    batch.drawRect({cx, cy}, {kSlotSize - 2.0f, kSlotSize - 2.0f},
                                   kSoulboundTint, depth + 0.12f);
                }

                // Item name (truncated)
                std::string label = slot.displayName;
                if (label.size() > 6) label = label.substr(0, 5) + "~";
                float nameFS = 8.0f;
                Vec2 nameSize = sdf.measure(label.c_str(), nameFS);
                Color nameColor = slot.soulbound
                    ? Color{0.6f, 0.6f, 0.6f, 0.7f}
                    : kTextColor;
                sdf.drawScreen(batch, label.c_str(),
                    {cx - nameSize.x * 0.5f, cy - 4.0f},
                    nameFS, nameColor, depth + 0.2f);

                // Quantity badge (bottom-right)
                if (slot.quantity > 1) {
                    char qbuf[16];
                    snprintf(qbuf, sizeof(qbuf), "%d", slot.quantity);
                    sdf.drawScreen(batch, qbuf,
                        {cx + kSlotSize * 0.5f - 14.0f, cy + kSlotSize * 0.5f - 10.0f},
                        8.0f, kQtyColor, depth + 0.25f);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Render: gold bar (bottom)
// ---------------------------------------------------------------------------
void ShopPanel::renderGoldBar(SpriteBatch& batch, SDFText& sdf, float depth) {
    const auto& rect = computedRect_;
    float barY  = rect.y + rect.h - kGoldBarH;
    float barCY = barY + kGoldBarH * 0.5f;

    // Divider above
    batch.drawRect({rect.x + rect.w * 0.5f, barY},
                   {rect.w - 4.0f, 1.0f}, kDividerColor, depth + 0.1f);

    // Gold text
    char goldBuf[48];
    snprintf(goldBuf, sizeof(goldBuf), "Gold: %lld",
             static_cast<long long>(playerGold));
    float fontSize = 12.0f;
    sdf.drawScreen(batch, goldBuf,
        {rect.x + kPanePad, barCY - fontSize * 0.5f},
        fontSize, kGoldColor, depth + 0.2f);
}

// ---------------------------------------------------------------------------
// Render: sell confirmation popup
// ---------------------------------------------------------------------------
void ShopPanel::renderSellConfirm(SpriteBatch& batch, SDFText& sdf, float depth) {
    const auto& rect = computedRect_;
    float popX = rect.x + (rect.w - kConfirmW) * 0.5f;
    float popY = rect.y + (rect.h - kConfirmH) * 0.5f;
    float popCX = popX + kConfirmW * 0.5f;
    float popCY = popY + kConfirmH * 0.5f;

    // Darken background behind popup
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, {0.0f, 0.0f, 0.0f, 0.4f}, depth + 0.5f);

    // Popup background
    batch.drawRect({popCX, popCY}, {kConfirmW, kConfirmH}, kConfirmBg, depth + 0.6f);

    // Popup border
    float bw = 1.5f;
    float innerH = kConfirmH - bw * 2.0f;
    batch.drawRect({popCX, popY + bw * 0.5f},                     {kConfirmW, bw}, kConfirmBdr, depth + 0.65f);
    batch.drawRect({popCX, popY + kConfirmH - bw * 0.5f},         {kConfirmW, bw}, kConfirmBdr, depth + 0.65f);
    batch.drawRect({popX + bw * 0.5f, popCY},                     {bw, innerH},    kConfirmBdr, depth + 0.65f);
    batch.drawRect({popX + kConfirmW - bw * 0.5f, popCY},         {bw, innerH},    kConfirmBdr, depth + 0.65f);

    // "How many?" label
    Vec2 labelSize = sdf.measure("How many?", 12.0f);
    sdf.drawScreen(batch, "How many?",
        {popCX - labelSize.x * 0.5f, popY + 10.0f},
        12.0f, kTextColor, depth + 0.7f);

    // Quantity display: "- N / max +"
    char qtyBuf[48];
    snprintf(qtyBuf, sizeof(qtyBuf), "%d / %d", sellInputQty_, sellMaxQty_);
    Vec2 qtySize = sdf.measure(qtyBuf, 14.0f);
    float qtyY = popY + 35.0f;
    sdf.drawScreen(batch, qtyBuf,
        {popCX - qtySize.x * 0.5f, qtyY},
        14.0f, kTextColor, depth + 0.7f);

    // "-" button
    float minusBtnX = popCX - qtySize.x * 0.5f - 28.0f;
    float minusBtnCX = minusBtnX + 10.0f;
    float minusBtnCY = qtyY + 7.0f;
    batch.drawRect({minusBtnCX, minusBtnCY}, {20.0f, 20.0f}, kConfirmBtnColor, depth + 0.7f);
    Vec2 minusTs = sdf.measure("-", 12.0f);
    sdf.drawScreen(batch, "-",
        {minusBtnCX - minusTs.x * 0.5f, minusBtnCY - minusTs.y * 0.5f},
        12.0f, kTextColor, depth + 0.8f);

    // "+" button
    float plusBtnX = popCX + qtySize.x * 0.5f + 8.0f;
    float plusBtnCX = plusBtnX + 10.0f;
    float plusBtnCY = qtyY + 7.0f;
    batch.drawRect({plusBtnCX, plusBtnCY}, {20.0f, 20.0f}, kConfirmBtnColor, depth + 0.7f);
    Vec2 plusTs = sdf.measure("+", 12.0f);
    sdf.drawScreen(batch, "+",
        {plusBtnCX - plusTs.x * 0.5f, plusBtnCY - plusTs.y * 0.5f},
        12.0f, kTextColor, depth + 0.8f);

    // Sell price display
    if (sellSlot_ < MAX_SLOTS) {
        int64_t totalPrice = playerItems[sellSlot_].sellPrice * sellInputQty_;
        char sellPriceBuf[48];
        snprintf(sellPriceBuf, sizeof(sellPriceBuf), "Sell for: %lld G",
                 static_cast<long long>(totalPrice));
        Vec2 spSize = sdf.measure(sellPriceBuf, 10.0f);
        sdf.drawScreen(batch, sellPriceBuf,
            {popCX - spSize.x * 0.5f, qtyY + 22.0f},
            10.0f, kGoldColor, depth + 0.7f);
    }

    // Confirm button
    float btnW = 70.0f;
    float btnH = 24.0f;
    float btnY = popY + kConfirmH - btnH - 12.0f;
    float confirmBtnX = popCX - btnW - 8.0f;
    float confirmBtnCX = confirmBtnX + btnW * 0.5f;
    float confirmBtnCY = btnY + btnH * 0.5f;
    batch.drawRect({confirmBtnCX, confirmBtnCY}, {btnW, btnH}, kConfirmBtnColor, depth + 0.7f);
    Vec2 confTs = sdf.measure("Confirm", 10.0f);
    sdf.drawScreen(batch, "Confirm",
        {confirmBtnCX - confTs.x * 0.5f, confirmBtnCY - confTs.y * 0.5f},
        10.0f, kTextColor, depth + 0.8f);

    // Cancel button
    float cancelBtnX = popCX + 8.0f;
    float cancelBtnCX = cancelBtnX + btnW * 0.5f;
    float cancelBtnCY = btnY + btnH * 0.5f;
    batch.drawRect({cancelBtnCX, cancelBtnCY}, {btnW, btnH}, kCancelBtnColor, depth + 0.7f);
    Vec2 cancelTs = sdf.measure("Cancel", 10.0f);
    sdf.drawScreen(batch, "Cancel",
        {cancelBtnCX - cancelTs.x * 0.5f, cancelBtnCY - cancelTs.y * 0.5f},
        10.0f, kTextColor, depth + 0.8f);
}

// ---------------------------------------------------------------------------
// Render: error message
// ---------------------------------------------------------------------------
void ShopPanel::renderError(SpriteBatch& batch, SDFText& sdf, float depth) {
    const auto& rect = computedRect_;
    float alpha = std::min(errorTimer / 0.5f, 1.0f);  // fade out last 0.5s
    Color errColor = kErrorColor;
    errColor.a *= alpha;

    Vec2 errSize = sdf.measure(errorMessage.c_str(), 10.0f);
    float errX = rect.x + (rect.w - errSize.x) * 0.5f;
    float errY = rect.y + rect.h - kGoldBarH - 18.0f;
    sdf.drawScreen(batch, errorMessage.c_str(),
        {errX, errY}, 10.0f, errColor, depth + 0.4f);
}

// ---------------------------------------------------------------------------
// onPress
// ---------------------------------------------------------------------------
bool ShopPanel::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    // ---- Sell confirmation popup (intercept all clicks when open) ----
    if (showSellConfirm_) {
        const auto& rect = computedRect_;
        float popX = (rect.w - kConfirmW) * 0.5f;
        float popY = (rect.h - kConfirmH) * 0.5f;

        // Quantity display for button positions
        char qtyBuf[48];
        snprintf(qtyBuf, sizeof(qtyBuf), "%d / %d", sellInputQty_, sellMaxQty_);
        // We use approximate layout matching render positions (relative to panel)
        float qtyY = popY + 35.0f;

        // "-" button area
        float minusBtnX = (rect.w - kConfirmW) * 0.5f + kConfirmW * 0.5f - 40.0f - 18.0f;
        float minusBtnY = qtyY - 3.0f;
        if (localPos.x >= minusBtnX && localPos.x <= minusBtnX + 20.0f &&
            localPos.y >= minusBtnY && localPos.y <= minusBtnY + 20.0f) {
            if (sellInputQty_ > 1) --sellInputQty_;
            return true;
        }

        // "+" button area
        float plusBtnX = (rect.w - kConfirmW) * 0.5f + kConfirmW * 0.5f + 40.0f;
        float plusBtnY = qtyY - 3.0f;
        if (localPos.x >= plusBtnX && localPos.x <= plusBtnX + 20.0f &&
            localPos.y >= plusBtnY && localPos.y <= plusBtnY + 20.0f) {
            if (sellInputQty_ < sellMaxQty_) ++sellInputQty_;
            return true;
        }

        // Confirm / Cancel buttons
        float btnW = 70.0f;
        float btnH = 24.0f;
        float btnY2 = popY + kConfirmH - btnH - 12.0f;
        float popCX = rect.w * 0.5f;

        // Confirm button
        float confirmBtnX = popCX - btnW - 8.0f;
        if (localPos.x >= confirmBtnX && localPos.x <= confirmBtnX + btnW &&
            localPos.y >= btnY2 && localPos.y <= btnY2 + btnH) {
            if (onSell && sellInputQty_ > 0) {
                onSell(npcId, sellSlot_, static_cast<uint16_t>(sellInputQty_));
            }
            showSellConfirm_ = false;
            return true;
        }

        // Cancel button
        float cancelBtnX = popCX + 8.0f;
        if (localPos.x >= cancelBtnX && localPos.x <= cancelBtnX + btnW &&
            localPos.y >= btnY2 && localPos.y <= btnY2 + btnH) {
            showSellConfirm_ = false;
            return true;
        }

        return true;  // consume all clicks when confirm is open
    }

    // ---- Close button hit test ----
    float closeR  = 10.0f;
    float closeCX = computedRect_.w - closeR - 5.0f;
    float closeCY = closeR + 5.0f;
    {
        float dx = localPos.x - closeCX;
        float dy = localPos.y - closeCY;
        if (dx * dx + dy * dy <= closeR * closeR) {
            close();
            return true;
        }
    }

    // ---- Buy button hit test ----
    int buyIdx = hitTestShopBuyButton(localPos);
    if (buyIdx >= 0 && buyIdx < static_cast<int>(shopItems.size())) {
        const ShopEntry& entry = shopItems[static_cast<size_t>(buyIdx)];
        bool canAfford = playerGold >= entry.buyPrice;
        bool inStock   = entry.stock < 0 || entry.stock > 0;
        if (canAfford && inStock) {
            if (onBuy) onBuy(npcId, entry.itemId, 1);
        } else if (!canAfford) {
            errorMessage = "Not enough gold!";
            errorTimer = kErrorDuration;
        } else {
            errorMessage = "Out of stock!";
            errorTimer = kErrorDuration;
        }
        return true;
    }

    // ---- Inventory slot double-click (for selling) ----
    int slot = hitTestInventorySlot(localPos);
    if (slot >= 0 && slot < MAX_SLOTS && !playerItems[slot].itemId.empty()) {
        if (!playerItems[slot].soulbound) {
            // Check for double-click
            float now = timeSinceStart_;
            if (lastClickSlot_ == slot && (now - lastClickTime_) < kDoubleClickT) {
                // Open sell confirmation
                showSellConfirm_ = true;
                sellSlot_ = static_cast<uint8_t>(slot);
                sellMaxQty_ = playerItems[slot].quantity;
                if (sellMaxQty_ < 1) sellMaxQty_ = 1;
                sellInputQty_ = sellMaxQty_;
                lastClickSlot_ = -1;
            } else {
                lastClickSlot_ = slot;
                lastClickTime_ = now;
            }
        }
        return true;
    }

    // ---- Scroll: check if click is in shop list area for scroll wheel ----
    // (mouse wheel handled externally or via future scroll integration)

    return true;  // consume all clicks on the panel
}

// ---------------------------------------------------------------------------
// onKeyInput
// ---------------------------------------------------------------------------
bool ShopPanel::onKeyInput(int scancode, bool pressed) {
    if (!pressed) return false;

    if (scancode == SDL_SCANCODE_ESCAPE) {
        if (showSellConfirm_) {
            showSellConfirm_ = false;
            return true;
        }
        close();
        return true;
    }
    return false;
}

} // namespace fate
