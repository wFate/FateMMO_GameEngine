#include "engine/ui/widgets/bank_panel.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <cmath>
#include <SDL.h>

namespace fate {

BankPanel::BankPanel(const std::string& id)
    : UINode(id, "bank_panel") {}

// ---------------------------------------------------------------------------
// open / close / rebuild
// ---------------------------------------------------------------------------

void BankPanel::open(uint32_t npc) {
    npcId = npc;
    goldInputAmount_ = 0;
    scrollOffsetBank_ = 0.0f;
    errorMessage.clear();
    errorTimer = 0.0f;
    setVisible(true);
}

void BankPanel::close() {
    setVisible(false);
    if (onClose) onClose(id_);
}

void BankPanel::rebuild() {
    scrollOffsetBank_ = 0.0f;
    goldInputAmount_ = 0;
    errorMessage.clear();
    errorTimer = 0.0f;
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

void BankPanel::update(float dt) {
    if (errorTimer > 0.0f) {
        errorTimer -= dt;
        if (errorTimer < 0.0f) {
            errorTimer = 0.0f;
            errorMessage.clear();
        }
    }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void BankPanel::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Layout constants
    float headerH = 28.0f;
    float bottomBarH = 80.0f;
    float bw = 2.0f;
    float leftW = rect.w * 0.5f;
    float rightW = rect.w - leftW;
    float contentY = rect.y + headerH;
    float contentH = rect.h - headerH - bottomBarH;

    // ---- Colors ----
    Color bg          = {0.08f, 0.08f, 0.12f, 0.95f};
    Color bdr         = {0.25f, 0.25f, 0.35f, 1.0f};
    Color textColor   = {0.9f, 0.9f, 0.85f, 1.0f};
    Color goldColor   = {1.0f, 0.84f, 0.0f, 1.0f};
    Color divColor    = {0.25f, 0.25f, 0.35f, 0.6f};
    Color slotBg      = {0.12f, 0.12f, 0.18f, 0.8f};
    Color withdrawBg  = {0.3f, 0.15f, 0.15f, 0.8f};
    Color depositBg   = {0.15f, 0.3f, 0.15f, 0.8f};
    Color disabledBg  = {0.2f, 0.2f, 0.2f, 0.5f};

    // ---- Background ----
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, bg, d);
    // Border edges
    float innerH = rect.h - bw * 2.0f;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f},           {rect.w, bw}, bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f},  {rect.w, bw}, bdr, d + 0.1f);
    batch.drawRect({rect.x + bw * 0.5f,     rect.y + rect.h * 0.5f},       {bw, innerH}, bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f},  {bw, innerH}, bdr, d + 0.1f);

    // ---- Title bar ----
    Color titleBarBg = {0.12f, 0.12f, 0.18f, 1.0f};
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + headerH * 0.5f},
                   {rect.w - bw * 2.0f, headerH}, titleBarBg, d + 0.05f);

    sdf.drawScreen(batch, "Bank",
        {rect.x + 10.0f, rect.y + 7.0f},
        14.0f, textColor, d + 0.2f);

    // ---- Close button (X at top-right, 20x20) ----
    float closeSize = 20.0f;
    float closeCX = rect.x + rect.w - closeSize * 0.5f - 6.0f;
    float closeCY = rect.y + headerH * 0.5f;
    Color closeBg = {0.3f, 0.15f, 0.15f, 0.9f};
    batch.drawRect({closeCX, closeCY}, {closeSize, closeSize}, closeBg, d + 0.2f);
    Vec2 xts = sdf.measure("X", 12.0f);
    sdf.drawScreen(batch, "X",
        {closeCX - xts.x * 0.5f, closeCY - xts.y * 0.5f},
        12.0f, textColor, d + 0.3f);

    // ---- Divider below title ----
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + headerH},
                   {rect.w - bw * 2.0f, 1.0f}, divColor, d + 0.1f);

    // ---- Vertical divider between panes ----
    float dividerX = rect.x + leftW;
    batch.drawRect({dividerX, contentY + contentH * 0.5f},
                   {1.0f, contentH}, divColor, d + 0.1f);

    // =====================================================================
    // LEFT PANE: Bank Storage
    // =====================================================================
    float leftPaneX = rect.x + 4.0f;
    float leftPaneW = leftW - 8.0f;

    // "Bank Storage" sub-header
    sdf.drawScreen(batch, "Bank Storage",
        {leftPaneX, contentY + 4.0f},
        12.0f, textColor, d + 0.2f);

    // Scrollable bank item rows
    float rowH = 36.0f;
    float bankListY = contentY + 22.0f;
    float bankListH = contentH - 24.0f;
    int maxVisibleRows = static_cast<int>(bankListH / rowH);
    if (maxVisibleRows < 1) maxVisibleRows = 1;

    int startRow = static_cast<int>(scrollOffsetBank_ / rowH);
    if (startRow < 0) startRow = 0;
    if (startRow >= static_cast<int>(bankItems.size())) startRow = 0;

    float withdrawBtnW = 70.0f;
    float withdrawBtnH = 22.0f;

    for (int i = 0; i < maxVisibleRows; ++i) {
        int itemIdx = startRow + i;
        if (itemIdx >= static_cast<int>(bankItems.size())) break;

        const BankItem& item = bankItems[static_cast<size_t>(itemIdx)];
        float rowY = bankListY + static_cast<float>(i) * rowH;
        float rowCY = rowY + rowH * 0.5f;

        // Alternate row shading
        if (i % 2 == 1) {
            Color altBg = {0.1f, 0.1f, 0.15f, 0.5f};
            batch.drawRect({leftPaneX + leftPaneW * 0.5f, rowCY},
                           {leftPaneW, rowH}, altBg, d + 0.05f);
        }

        // Row bottom border
        Color rowBorderC = {0.2f, 0.2f, 0.3f, 0.5f};
        batch.drawRect({leftPaneX + leftPaneW * 0.5f, rowY + rowH - 0.5f},
                       {leftPaneW, 1.0f}, rowBorderC, d + 0.1f);

        // Item name
        sdf.drawScreen(batch, item.displayName,
            {leftPaneX + 4.0f, rowY + 4.0f},
            11.0f, textColor, d + 0.2f);

        // Quantity
        char qbuf[16];
        snprintf(qbuf, sizeof(qbuf), "x%u", item.count);
        sdf.drawScreen(batch, qbuf,
            {leftPaneX + 4.0f, rowY + 18.0f},
            9.0f, goldColor, d + 0.2f);

        // "Withdraw" button (right side of row)
        float wBtnX = leftPaneX + leftPaneW - withdrawBtnW - 4.0f;
        float wBtnCX = wBtnX + withdrawBtnW * 0.5f;
        batch.drawRect({wBtnCX, rowCY}, {withdrawBtnW, withdrawBtnH}, withdrawBg, d + 0.15f);
        Vec2 wts = sdf.measure("Withdraw", 9.0f);
        sdf.drawScreen(batch, "Withdraw",
            {wBtnCX - wts.x * 0.5f, rowCY - wts.y * 0.5f},
            9.0f, textColor, d + 0.25f);
    }

    // Empty state
    if (bankItems.empty()) {
        Color emptyColor = {0.5f, 0.5f, 0.5f, 0.7f};
        sdf.drawScreen(batch, "No items stored",
            {leftPaneX + 10.0f, bankListY + 10.0f},
            10.0f, emptyColor, d + 0.2f);
    }

    // =====================================================================
    // RIGHT PANE: Player Inventory
    // =====================================================================
    float rightPaneX = rect.x + leftW + 4.0f;
    float rightPaneW = rightW - 8.0f;

    // "Your Inventory" sub-header
    sdf.drawScreen(batch, "Your Inventory",
        {rightPaneX, contentY + 4.0f},
        12.0f, textColor, d + 0.2f);

    // 4x4 grid of inventory slots
    float slotSize = 40.0f;
    float slotPad = 4.0f;
    int gridCols = 4;
    int gridRows = 4;
    float gridStartX = rightPaneX + (rightPaneW - (slotSize * gridCols + slotPad * (gridCols - 1))) * 0.5f;
    float gridStartY = contentY + 24.0f;

    for (int row = 0; row < gridRows; ++row) {
        for (int col = 0; col < gridCols; ++col) {
            int idx = row * gridCols + col;
            if (idx >= MAX_SLOTS) break;

            float cx = gridStartX + slotSize * 0.5f + static_cast<float>(col) * (slotSize + slotPad);
            float cy = gridStartY + slotSize * 0.5f + static_cast<float>(row) * (slotSize + slotPad);

            bool hasItem = !playerItems[idx].itemId.empty();

            // Slot background
            Color sBg = hasItem ? Color{0.16f, 0.16f, 0.24f, 0.9f} : slotBg;
            batch.drawRect({cx, cy}, {slotSize, slotSize}, sBg, d + 0.1f);

            // Slot border
            Color sBdr = {0.3f, 0.3f, 0.4f, 0.6f};
            float sbw = 1.0f;
            float sIH = slotSize - sbw * 2.0f;
            batch.drawRect({cx, cy - slotSize * 0.5f + sbw * 0.5f}, {slotSize, sbw}, sBdr, d + 0.15f);
            batch.drawRect({cx, cy + slotSize * 0.5f - sbw * 0.5f}, {slotSize, sbw}, sBdr, d + 0.15f);
            batch.drawRect({cx - slotSize * 0.5f + sbw * 0.5f, cy}, {sbw, sIH}, sBdr, d + 0.15f);
            batch.drawRect({cx + slotSize * 0.5f - sbw * 0.5f, cy}, {sbw, sIH}, sBdr, d + 0.15f);

            if (hasItem) {
                // Item name (truncated)
                const std::string& name = playerItems[idx].displayName;
                std::string shortName = name.size() > 5 ? name.substr(0, 5) : name;
                Vec2 ns = sdf.measure(shortName, 8.0f);
                sdf.drawScreen(batch, shortName,
                    {cx - ns.x * 0.5f, cy - 6.0f},
                    8.0f, textColor, d + 0.2f);

                // Quantity badge
                if (playerItems[idx].quantity > 1) {
                    char qbuf[16];
                    snprintf(qbuf, sizeof(qbuf), "%d", playerItems[idx].quantity);
                    sdf.drawScreen(batch, qbuf,
                        {cx + slotSize * 0.5f - 14.0f, cy + slotSize * 0.5f - 10.0f},
                        9.0f, goldColor, d + 0.25f);
                }
            }
        }
    }

    // "Double-click to deposit" hint
    Color hintColor = {0.5f, 0.5f, 0.5f, 0.6f};
    float hintY = gridStartY + static_cast<float>(gridRows) * (slotSize + slotPad) + 4.0f;
    sdf.drawScreen(batch, "Double-click item to deposit",
        {rightPaneX + 4.0f, hintY},
        8.0f, hintColor, d + 0.2f);

    // =====================================================================
    // BOTTOM BAR: Gold operations
    // =====================================================================
    float barY = rect.y + rect.h - bottomBarH;

    // Divider above bottom bar
    batch.drawRect({rect.x + rect.w * 0.5f, barY},
                   {rect.w - bw * 2.0f, 1.0f}, divColor, d + 0.1f);

    // Bottom bar background
    Color barBg = {0.1f, 0.1f, 0.14f, 0.9f};
    batch.drawRect({rect.x + rect.w * 0.5f, barY + bottomBarH * 0.5f},
                   {rect.w - bw * 2.0f, bottomBarH - bw}, barBg, d + 0.05f);

    float barPadX = 10.0f;
    float lineY1 = barY + 6.0f;
    float lineY2 = barY + 22.0f;
    float lineY3 = barY + 42.0f;

    // Gold labels
    char playerGoldBuf[48];
    snprintf(playerGoldBuf, sizeof(playerGoldBuf), "Your Gold: %lld",
             static_cast<long long>(playerGold));
    sdf.drawScreen(batch, playerGoldBuf,
        {rect.x + barPadX, lineY1},
        11.0f, goldColor, d + 0.2f);

    char bankGoldBuf[48];
    snprintf(bankGoldBuf, sizeof(bankGoldBuf), "Bank Gold: %lld",
             static_cast<long long>(bankGold));
    sdf.drawScreen(batch, bankGoldBuf,
        {rect.x + rect.w * 0.5f, lineY1},
        11.0f, goldColor, d + 0.2f);

    // Gold input: +1K, +10K buttons and amount display
    float btnW = 40.0f;
    float btnH = 20.0f;
    float inputRowX = rect.x + barPadX;

    // "+1K" button
    float btn1kCX = inputRowX + btnW * 0.5f;
    float btn1kCY = lineY2 + btnH * 0.5f;
    batch.drawRect({btn1kCX, btn1kCY}, {btnW, btnH}, depositBg, d + 0.15f);
    Vec2 b1kts = sdf.measure("+1K", 9.0f);
    sdf.drawScreen(batch, "+1K",
        {btn1kCX - b1kts.x * 0.5f, btn1kCY - b1kts.y * 0.5f},
        9.0f, textColor, d + 0.25f);

    // "+10K" button
    float btn10kCX = inputRowX + btnW + 6.0f + btnW * 0.5f;
    float btn10kCY = lineY2 + btnH * 0.5f;
    batch.drawRect({btn10kCX, btn10kCY}, {btnW, btnH}, depositBg, d + 0.15f);
    Vec2 b10kts = sdf.measure("+10K", 9.0f);
    sdf.drawScreen(batch, "+10K",
        {btn10kCX - b10kts.x * 0.5f, btn10kCY - b10kts.y * 0.5f},
        9.0f, textColor, d + 0.25f);

    // Amount display
    char amtBuf[48];
    snprintf(amtBuf, sizeof(amtBuf), "Amount: %lld",
             static_cast<long long>(goldInputAmount_));
    sdf.drawScreen(batch, amtBuf,
        {inputRowX + btnW * 2 + 18.0f, lineY2 + 3.0f},
        10.0f, textColor, d + 0.2f);

    // "Deposit (fee: 5,000)" button
    float depBtnW = 150.0f;
    float depBtnH = 24.0f;
    float depBtnCX = rect.x + barPadX + depBtnW * 0.5f;
    float depBtnCY = lineY3 + depBtnH * 0.5f;

    bool canDeposit = goldInputAmount_ > 0 &&
                      playerGold >= goldInputAmount_ + BankStorage::BANK_DEPOSIT_FEE;

    Color depColor = canDeposit ? depositBg : disabledBg;
    batch.drawRect({depBtnCX, depBtnCY}, {depBtnW, depBtnH}, depColor, d + 0.15f);

    char depLabel[64];
    snprintf(depLabel, sizeof(depLabel), "Deposit (fee: %lld)",
             static_cast<long long>(BankStorage::BANK_DEPOSIT_FEE));
    Vec2 dts = sdf.measure(depLabel, 9.0f);
    Color depTextColor = canDeposit ? textColor : Color{0.5f, 0.5f, 0.5f, 0.7f};
    sdf.drawScreen(batch, depLabel,
        {depBtnCX - dts.x * 0.5f, depBtnCY - dts.y * 0.5f},
        9.0f, depTextColor, d + 0.25f);

    // "Withdraw All" button
    float wdBtnW = 100.0f;
    float wdBtnH = 24.0f;
    float wdBtnCX = rect.x + rect.w - barPadX - wdBtnW * 0.5f;
    float wdBtnCY = lineY3 + wdBtnH * 0.5f;

    bool canWithdrawGold = bankGold > 0;
    Color wdColor = canWithdrawGold ? withdrawBg : disabledBg;
    batch.drawRect({wdBtnCX, wdBtnCY}, {wdBtnW, wdBtnH}, wdColor, d + 0.15f);

    Vec2 wdts = sdf.measure("Withdraw All", 9.0f);
    Color wdTextColor = canWithdrawGold ? textColor : Color{0.5f, 0.5f, 0.5f, 0.7f};
    sdf.drawScreen(batch, "Withdraw All",
        {wdBtnCX - wdts.x * 0.5f, wdBtnCY - wdts.y * 0.5f},
        9.0f, wdTextColor, d + 0.25f);

    // ---- Error message (if active) ----
    if (errorTimer > 0.0f && !errorMessage.empty()) {
        float errAlpha = (errorTimer < 1.0f) ? errorTimer : 1.0f;
        Color errColor = {0.8f, 0.2f, 0.2f, errAlpha};
        Vec2 errSize = sdf.measure(errorMessage, 10.0f);
        float errY = barY - 14.0f;
        sdf.drawScreen(batch, errorMessage,
            {rect.x + rect.w * 0.5f - errSize.x * 0.5f, errY},
            10.0f, errColor, d + 0.3f);
    }

    renderChildren(batch, sdf);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool BankPanel::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    const auto& rect = computedRect_;
    float headerH = 28.0f;
    float bottomBarH = 80.0f;
    float leftW = rect.w * 0.5f;
    float rightW = rect.w - leftW;
    float contentY = headerH;
    float contentH = rect.h - headerH - bottomBarH;

    // ---- Close button hit test (20x20 rect at top-right) ----
    float closeSize = 20.0f;
    float closeCX = rect.w - closeSize * 0.5f - 6.0f;
    float closeCY = headerH * 0.5f;
    if (localPos.x >= closeCX - closeSize * 0.5f &&
        localPos.x <= closeCX + closeSize * 0.5f &&
        localPos.y >= closeCY - closeSize * 0.5f &&
        localPos.y <= closeCY + closeSize * 0.5f) {
        close();
        return true;
    }

    // ---- Left pane: Withdraw buttons ----
    float leftPaneX = 4.0f;
    float leftPaneW = leftW - 8.0f;
    float rowH = 36.0f;
    float bankListY = contentY + 22.0f;
    float withdrawBtnW = 70.0f;
    float withdrawBtnH = 22.0f;

    int startRow = static_cast<int>(scrollOffsetBank_ / rowH);
    if (startRow < 0) startRow = 0;
    if (startRow >= static_cast<int>(bankItems.size())) startRow = 0;

    float bankListH = contentH - 24.0f;
    int maxVisibleRows = static_cast<int>(bankListH / rowH);
    if (maxVisibleRows < 1) maxVisibleRows = 1;

    for (int i = 0; i < maxVisibleRows; ++i) {
        int itemIdx = startRow + i;
        if (itemIdx >= static_cast<int>(bankItems.size())) break;

        float rowY = bankListY + static_cast<float>(i) * rowH;
        float rowCY = rowY + rowH * 0.5f;

        // Withdraw button area
        float wBtnX = leftPaneX + leftPaneW - withdrawBtnW - 4.0f;
        if (localPos.x >= wBtnX &&
            localPos.x <= wBtnX + withdrawBtnW &&
            localPos.y >= rowCY - withdrawBtnH * 0.5f &&
            localPos.y <= rowCY + withdrawBtnH * 0.5f) {
            if (onWithdrawItem) {
                onWithdrawItem(npcId, static_cast<uint16_t>(itemIdx));
            }
            return true;
        }
    }

    // ---- Right pane: Inventory slot double-click ----
    float slotSize = 40.0f;
    float slotPad = 4.0f;
    int gridCols = 4;
    int gridRows = 4;
    float rightPaneX = leftW + 4.0f;
    float rightPaneW = rightW - 8.0f;
    float gridStartX = rightPaneX + (rightPaneW - (slotSize * gridCols + slotPad * (gridCols - 1))) * 0.5f;
    float gridStartY = contentY + 24.0f;

    for (int row = 0; row < gridRows; ++row) {
        for (int col = 0; col < gridCols; ++col) {
            int idx = row * gridCols + col;
            if (idx >= MAX_SLOTS) break;

            float cx = gridStartX + slotSize * 0.5f + static_cast<float>(col) * (slotSize + slotPad);
            float cy = gridStartY + slotSize * 0.5f + static_cast<float>(row) * (slotSize + slotPad);

            if (localPos.x >= cx - slotSize * 0.5f && localPos.x <= cx + slotSize * 0.5f &&
                localPos.y >= cy - slotSize * 0.5f && localPos.y <= cy + slotSize * 0.5f) {
                if (!playerItems[idx].itemId.empty()) {
                    if (onDepositItem) {
                        onDepositItem(npcId, static_cast<uint8_t>(idx));
                    }
                }
                return true;
            }
        }
    }

    // ---- Bottom bar: Gold buttons ----
    float barY = rect.h - bottomBarH;
    float barPadX = 10.0f;
    float lineY2 = barY + 22.0f;
    float lineY3 = barY + 42.0f;
    float btnW = 40.0f;
    float btnH = 20.0f;

    // +1K button
    float btn1kCX = barPadX + btnW * 0.5f;
    float btn1kCY = lineY2 + btnH * 0.5f;
    if (localPos.x >= btn1kCX - btnW * 0.5f && localPos.x <= btn1kCX + btnW * 0.5f &&
        localPos.y >= btn1kCY - btnH * 0.5f && localPos.y <= btn1kCY + btnH * 0.5f) {
        goldInputAmount_ += 1000;
        return true;
    }

    // +10K button
    float btn10kCX = barPadX + btnW + 6.0f + btnW * 0.5f;
    float btn10kCY = lineY2 + btnH * 0.5f;
    if (localPos.x >= btn10kCX - btnW * 0.5f && localPos.x <= btn10kCX + btnW * 0.5f &&
        localPos.y >= btn10kCY - btnH * 0.5f && localPos.y <= btn10kCY + btnH * 0.5f) {
        goldInputAmount_ += 10000;
        return true;
    }

    // "Deposit (fee)" button
    float depBtnW = 150.0f;
    float depBtnH = 24.0f;
    float depBtnCX = barPadX + depBtnW * 0.5f;
    float depBtnCY = lineY3 + depBtnH * 0.5f;
    if (localPos.x >= depBtnCX - depBtnW * 0.5f && localPos.x <= depBtnCX + depBtnW * 0.5f &&
        localPos.y >= depBtnCY - depBtnH * 0.5f && localPos.y <= depBtnCY + depBtnH * 0.5f) {
        if (goldInputAmount_ > 0 &&
            playerGold >= goldInputAmount_ + BankStorage::BANK_DEPOSIT_FEE) {
            if (onDepositGold) {
                onDepositGold(npcId, goldInputAmount_);
            }
            goldInputAmount_ = 0;
        } else if (goldInputAmount_ <= 0) {
            errorMessage = "Enter an amount to deposit";
            errorTimer = 3.0f;
        } else {
            errorMessage = "Not enough gold (includes fee)";
            errorTimer = 3.0f;
        }
        return true;
    }

    // "Withdraw All" button
    float wdBtnW = 100.0f;
    float wdBtnH = 24.0f;
    float wdBtnCX = rect.w - barPadX - wdBtnW * 0.5f;
    float wdBtnCY = lineY3 + wdBtnH * 0.5f;
    if (localPos.x >= wdBtnCX - wdBtnW * 0.5f && localPos.x <= wdBtnCX + wdBtnW * 0.5f &&
        localPos.y >= wdBtnCY - wdBtnH * 0.5f && localPos.y <= wdBtnCY + wdBtnH * 0.5f) {
        if (bankGold > 0) {
            if (onWithdrawGold) {
                onWithdrawGold(npcId, bankGold);
            }
        } else {
            errorMessage = "No gold to withdraw";
            errorTimer = 3.0f;
        }
        return true;
    }

    return true; // consume all clicks on panel
}

bool BankPanel::onKeyInput(int scancode, bool pressed) {
    if (!pressed) return false;
    if (scancode == SDL_SCANCODE_ESCAPE) {
        close();
        return true;
    }
    return false;
}

} // namespace fate
