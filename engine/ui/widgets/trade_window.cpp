#include "engine/ui/widgets/trade_window.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <cstring>

namespace fate {

TradeWindow::TradeWindow(const std::string& id)
    : UINode(id, "trade_window") {}

// ---------------------------------------------------------------------------
// Helper: draw one trade slot box
// ---------------------------------------------------------------------------
static void drawTradeSlot(SpriteBatch& batch, float cx, float cy, float size,
                           float depth, bool hasItem, bool readOnly) {
    Color bg  = readOnly
        ? Color{0.65f, 0.60f, 0.50f, 0.90f}
        : (hasItem ? Color{0.72f, 0.65f, 0.52f, 1.0f}
                   : Color{0.78f, 0.72f, 0.60f, 1.0f});
    Color bdr = {0.45f, 0.35f, 0.25f, 0.85f};
    float bw  = 1.5f;
    float iH  = size - bw * 2.0f;

    batch.drawRect({cx, cy}, {size, size}, bg, depth);
    batch.drawRect({cx, cy - size * 0.5f + bw * 0.5f}, {size, bw},  bdr, depth + 0.05f);
    batch.drawRect({cx, cy + size * 0.5f - bw * 0.5f}, {size, bw},  bdr, depth + 0.05f);
    batch.drawRect({cx - size * 0.5f + bw * 0.5f, cy}, {bw, iH},   bdr, depth + 0.05f);
    batch.drawRect({cx + size * 0.5f - bw * 0.5f, cy}, {bw, iH},   bdr, depth + 0.05f);
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
void TradeWindow::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // ---- Parchment background ----
    Color bg  = {0.85f, 0.78f, 0.65f, 0.97f};
    Color bdr = {0.40f, 0.30f, 0.20f, 1.0f};
    float bw  = 3.0f;

    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, bg, d);
    float innerH = rect.h - bw * 2.0f;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f},          {rect.w, bw}, bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f}, {rect.w, bw}, bdr, d + 0.1f);
    batch.drawRect({rect.x + bw * 0.5f,     rect.y + rect.h * 0.5f},      {bw, innerH}, bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bdr, d + 0.1f);

    // ---- Title ----
    char titleBuf[64];
    if (!partnerName.empty())
        snprintf(titleBuf, sizeof(titleBuf), "Trade with %s", partnerName.c_str());
    else
        snprintf(titleBuf, sizeof(titleBuf), "Trade");

    Color titleColor = {0.28f, 0.18f, 0.08f, 1.0f};
    sdf.drawScreen(batch, titleBuf,
        {rect.x + 10.0f * layoutScale_, rect.y + 6.0f * layoutScale_},
        scaledFont(15.0f), titleColor, d + 0.2f);

    // ---- Close button (X circle, top-right) ----
    float closeR  = 11.0f * layoutScale_;
    float closeCX = rect.x + rect.w - closeR - 6.0f * layoutScale_;
    float closeCY = rect.y + closeR + 5.0f * layoutScale_;
    Color closeBg  = {0.55f, 0.42f, 0.28f, 1.0f};
    Color closeBdr = {0.30f, 0.20f, 0.10f, 1.0f};
    Color closeXC  = {1.0f, 0.95f, 0.88f, 1.0f};
    batch.drawCircle({closeCX, closeCY}, closeR, closeBg,  d + 0.2f, 16);
    batch.drawRing  ({closeCX, closeCY}, closeR, 1.5f * layoutScale_, closeBdr, d + 0.3f, 16);
    Vec2 xts = sdf.measure("X", scaledFont(11.0f));
    sdf.drawScreen(batch, "X",
        {closeCX - xts.x * 0.5f, closeCY - xts.y * 0.5f},
        scaledFont(11.0f), closeXC, d + 0.4f);

    // ---- Layout constants ----
    float headerH  = 28.0f * layoutScale_;
    float buttonH  = 30.0f * layoutScale_;
    float padding  = 6.0f * layoutScale_;
    float contentY = rect.y + headerH;
    float contentH = rect.h - headerH - buttonH - padding * 2.0f;
    float halfW    = rect.w * 0.5f;

    // Slot grid: 3x3, slot size adapts to available space
    float slotPad  = 4.0f * layoutScale_;
    float maxSlotW = (halfW - padding * 3.0f - slotPad * 2.0f) / 3.0f;
    float maxSlotH = (contentH - 40.0f * layoutScale_ - slotPad * 2.0f) / 3.0f;  // 40 for gold row
    float slotSize = std::min(maxSlotW, maxSlotH);
    if (slotSize < 20.0f * layoutScale_) slotSize = 20.0f * layoutScale_;

    float gridW = slotSize * 3.0f + slotPad * 2.0f;
    float gridH = slotSize * 3.0f + slotPad * 2.0f;

    // ---- Left half: My Offer ----
    {
        float sideX = rect.x + padding;
        float sideW = halfW - padding * 1.5f;
        float sideCX = sideX + sideW * 0.5f;

        // "My Offer" label
        Color myLabelColor = {0.25f, 0.18f, 0.10f, 1.0f};
        sdf.drawScreen(batch, "My Offer",
            {sideX + 4.0f * layoutScale_, contentY + 4.0f * layoutScale_},
            scaledFont(12.0f), myLabelColor, d + 0.2f);

        // Locked status border
        if (myLocked) {
            Color lockBdr = {0.2f, 0.75f, 0.3f, 0.9f};
            float lbw = 2.5f;
            float lH = contentH - lbw * 2.0f;
            batch.drawRect({sideCX, contentY + lbw * 0.5f},          {sideW, lbw}, lockBdr, d + 0.15f);
            batch.drawRect({sideCX, contentY + contentH - lbw * 0.5f}, {sideW, lbw}, lockBdr, d + 0.15f);
            batch.drawRect({sideX + lbw * 0.5f,   contentY + contentH * 0.5f}, {lbw, lH}, lockBdr, d + 0.15f);
            batch.drawRect({sideX + sideW - lbw * 0.5f, contentY + contentH * 0.5f}, {lbw, lH}, lockBdr, d + 0.15f);
        }

        // 3x3 slot grid
        float gridStartX = sideX + (sideW - gridW) * 0.5f;
        float gridStartY = contentY + 20.0f * layoutScale_;

        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                int idx = row * 3 + col;
                float cx = gridStartX + slotPad + slotSize * 0.5f + static_cast<float>(col) * (slotSize + slotPad);
                float cy = gridStartY + slotPad + slotSize * 0.5f + static_cast<float>(row) * (slotSize + slotPad);
                bool has = !mySlots[idx].itemId.empty();
                drawTradeSlot(batch, cx, cy, slotSize, d + 0.2f, has, myLocked);

                // Quantity badge
                if (has && mySlots[idx].quantity > 1) {
                    char qbuf[16];
                    snprintf(qbuf, sizeof(qbuf), "%d", mySlots[idx].quantity);
                    Color qColor = {1.0f, 1.0f, 0.85f, 1.0f};
                    sdf.drawScreen(batch, qbuf,
                        {cx + slotSize * 0.5f - 13.0f * layoutScale_, cy + slotSize * 0.5f - 10.0f * layoutScale_},
                        scaledFont(9.0f), qColor, d + 0.3f);
                }
            }
        }

        // Gold display
        char goldBuf[32];
        snprintf(goldBuf, sizeof(goldBuf), "Gold: %d", myGold);
        float goldY = gridStartY + gridH + 6.0f * layoutScale_;
        Color goldColor = {0.65f, 0.52f, 0.0f, 1.0f};
        sdf.drawScreen(batch, goldBuf, {sideX + 4.0f * layoutScale_, goldY}, scaledFont(11.0f), goldColor, d + 0.2f);

        // Lock button
        float lockBtnW = sideW * 0.55f;
        float lockBtnH = 22.0f * layoutScale_;
        float lockBtnX = sideX + (sideW - lockBtnW) * 0.5f;
        float lockBtnY = goldY + 14.0f * layoutScale_;
        Color lockBtnBg  = myLocked ? Color{0.2f, 0.65f, 0.3f, 0.95f}
                                     : Color{0.50f, 0.40f, 0.28f, 0.95f};
        Color lockBtnBdr = myLocked ? Color{0.15f, 0.45f, 0.20f, 1.0f}
                                     : Color{0.35f, 0.25f, 0.15f, 1.0f};
        Color lockBtnTxt = {1.0f, 0.95f, 0.85f, 1.0f};
        float lbH = lockBtnH - 2.0f;
        float lbCX = lockBtnX + lockBtnW * 0.5f;
        float lbCY = lockBtnY + lockBtnH * 0.5f;

        batch.drawRect({lbCX, lbCY}, {lockBtnW, lockBtnH}, lockBtnBg, d + 0.2f);
        batch.drawRect({lbCX, lockBtnY + 1.0f},           {lockBtnW, 2.0f}, lockBtnBdr, d + 0.3f);
        batch.drawRect({lbCX, lockBtnY + lockBtnH - 1.0f},{lockBtnW, 2.0f}, lockBtnBdr, d + 0.3f);
        batch.drawRect({lockBtnX + 1.0f, lbCY},            {2.0f, lbH},     lockBtnBdr, d + 0.3f);
        batch.drawRect({lockBtnX + lockBtnW - 1.0f, lbCY}, {2.0f, lbH},     lockBtnBdr, d + 0.3f);

        const char* lockLabel = myLocked ? "Locked" : "Lock";
        Vec2 lts = sdf.measure(lockLabel, scaledFont(11.0f));
        sdf.drawScreen(batch, lockLabel,
            {lbCX - lts.x * 0.5f, lbCY - lts.y * 0.5f},
            scaledFont(11.0f), lockBtnTxt, d + 0.4f);
    }

    // ---- Divider ----
    {
        float divX = rect.x + halfW;
        Color divColor = {0.38f, 0.28f, 0.18f, 0.6f};
        batch.drawRect({divX, contentY + contentH * 0.5f}, {2.0f * layoutScale_, contentH}, divColor, d + 0.1f);
    }

    // ---- Right half: Their Offer ----
    {
        float sideX = rect.x + halfW + padding * 0.5f;
        float sideW = halfW - padding * 1.5f;
        float sideCX = sideX + sideW * 0.5f;

        // "Their Offer" label + lock status
        Color theirLabelColor = {0.25f, 0.18f, 0.10f, 1.0f};
        sdf.drawScreen(batch, "Their Offer",
            {sideX + 4.0f * layoutScale_, contentY + 4.0f * layoutScale_},
            scaledFont(12.0f), theirLabelColor, d + 0.2f);

        // Locked indicator text
        if (theirLocked) {
            Color lockInd = {0.2f, 0.75f, 0.3f, 0.90f};
            Vec2 lits = sdf.measure("[Locked]", scaledFont(9.0f));
            sdf.drawScreen(batch, "[Locked]",
                {sideX + sideW - lits.x - 4.0f * layoutScale_, contentY + 6.0f * layoutScale_},
                scaledFont(9.0f), lockInd, d + 0.2f);

            // Green border
            float lbw = 2.5f;
            float lH  = contentH - lbw * 2.0f;
            batch.drawRect({sideCX, contentY + lbw * 0.5f},              {sideW, lbw}, lockInd, d + 0.15f);
            batch.drawRect({sideCX, contentY + contentH - lbw * 0.5f},   {sideW, lbw}, lockInd, d + 0.15f);
            batch.drawRect({sideX + lbw * 0.5f,     contentY + contentH * 0.5f}, {lbw, lH}, lockInd, d + 0.15f);
            batch.drawRect({sideX + sideW - lbw * 0.5f, contentY + contentH * 0.5f}, {lbw, lH}, lockInd, d + 0.15f);
        }

        // 3x3 slot grid (read-only — slightly darker)
        float gridStartX = sideX + (sideW - gridW) * 0.5f;
        float gridStartY = contentY + 20.0f * layoutScale_;

        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                int idx = row * 3 + col;
                float cx = gridStartX + slotPad + slotSize * 0.5f + static_cast<float>(col) * (slotSize + slotPad);
                float cy = gridStartY + slotPad + slotSize * 0.5f + static_cast<float>(row) * (slotSize + slotPad);
                bool has = !theirSlots[idx].itemId.empty();
                drawTradeSlot(batch, cx, cy, slotSize, d + 0.2f, has, /*readOnly=*/true);

                if (has && theirSlots[idx].quantity > 1) {
                    char qbuf[16];
                    snprintf(qbuf, sizeof(qbuf), "%d", theirSlots[idx].quantity);
                    Color qColor = {1.0f, 1.0f, 0.85f, 1.0f};
                    sdf.drawScreen(batch, qbuf,
                        {cx + slotSize * 0.5f - 13.0f * layoutScale_, cy + slotSize * 0.5f - 10.0f * layoutScale_},
                        scaledFont(9.0f), qColor, d + 0.3f);
                }
            }
        }

        // Their gold display
        char goldBuf[32];
        snprintf(goldBuf, sizeof(goldBuf), "Gold: %d", theirGold);
        float goldY = gridStartY + gridH + 6.0f * layoutScale_;
        Color goldColor = {0.65f, 0.52f, 0.0f, 1.0f};
        sdf.drawScreen(batch, goldBuf, {sideX + 4.0f * layoutScale_, goldY}, scaledFont(11.0f), goldColor, d + 0.2f);
    }

    // ---- Bottom buttons: Accept + Cancel ----
    {
        bool canAccept = myLocked && theirLocked;

        float btnY  = rect.y + rect.h - buttonH - padding;
        float btnH  = 24.0f * layoutScale_;
        float btnW  = rect.w * 0.35f;
        float gap   = 8.0f * layoutScale_;

        // Accept button (left of center)
        float acceptX = rect.x + rect.w * 0.5f - btnW - gap * 0.5f;
        float acceptCX = acceptX + btnW * 0.5f;
        float acceptCY = btnY + btnH * 0.5f;

        Color acceptBg  = canAccept ? Color{0.25f, 0.68f, 0.35f, 1.0f}
                                     : Color{0.42f, 0.42f, 0.42f, 0.80f};
        Color acceptBdr = canAccept ? Color{0.15f, 0.45f, 0.20f, 1.0f}
                                     : Color{0.30f, 0.30f, 0.30f, 1.0f};
        Color acceptTxt = canAccept ? Color{1.0f, 1.0f, 1.0f, 1.0f}
                                     : Color{0.65f, 0.65f, 0.65f, 0.8f};
        float abH = btnH - 2.0f;

        batch.drawRect({acceptCX, acceptCY}, {btnW, btnH}, acceptBg, d + 0.2f);
        batch.drawRect({acceptCX, btnY + 1.0f},        {btnW, 2.0f}, acceptBdr, d + 0.3f);
        batch.drawRect({acceptCX, btnY + btnH - 1.0f}, {btnW, 2.0f}, acceptBdr, d + 0.3f);
        batch.drawRect({acceptX + 1.0f, acceptCY},     {2.0f, abH},  acceptBdr, d + 0.3f);
        batch.drawRect({acceptX + btnW - 1.0f, acceptCY}, {2.0f, abH}, acceptBdr, d + 0.3f);

        Vec2 ats = sdf.measure("Accept", scaledFont(12.0f));
        sdf.drawScreen(batch, "Accept",
            {acceptCX - ats.x * 0.5f, acceptCY - ats.y * 0.5f},
            scaledFont(12.0f), acceptTxt, d + 0.4f);

        // Cancel button (right of center)
        float cancelX  = rect.x + rect.w * 0.5f + gap * 0.5f;
        float cancelCX = cancelX + btnW * 0.5f;
        float cancelCY = btnY + btnH * 0.5f;

        Color cancelBg  = {0.65f, 0.30f, 0.25f, 0.95f};
        Color cancelBdr = {0.45f, 0.18f, 0.14f, 1.0f};
        Color cancelTxt = {1.0f, 0.92f, 0.90f, 1.0f};
        float cbH = btnH - 2.0f;

        batch.drawRect({cancelCX, cancelCY}, {btnW, btnH}, cancelBg, d + 0.2f);
        batch.drawRect({cancelCX, btnY + 1.0f},        {btnW, 2.0f}, cancelBdr, d + 0.3f);
        batch.drawRect({cancelCX, btnY + btnH - 1.0f}, {btnW, 2.0f}, cancelBdr, d + 0.3f);
        batch.drawRect({cancelX + 1.0f, cancelCY},     {2.0f, cbH},  cancelBdr, d + 0.3f);
        batch.drawRect({cancelX + btnW - 1.0f, cancelCY}, {2.0f, cbH}, cancelBdr, d + 0.3f);

        Vec2 cts = sdf.measure("Cancel", scaledFont(12.0f));
        sdf.drawScreen(batch, "Cancel",
            {cancelCX - cts.x * 0.5f, cancelCY - cts.y * 0.5f},
            scaledFont(12.0f), cancelTxt, d + 0.4f);
    }

    renderChildren(batch, sdf);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
bool TradeWindow::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    const float w = computedRect_.w;
    const float h = computedRect_.h;

    // ---- Close button ----
    float closeR  = 11.0f;
    float closeCX = w - closeR - 6.0f;
    float closeCY = closeR + 5.0f;
    {
        float dx = localPos.x - closeCX;
        float dy = localPos.y - closeCY;
        if (dx * dx + dy * dy <= closeR * closeR) {
            if (onCancel) onCancel(id_);
            return true;
        }
    }

    // ---- Button area geometry (matching render) ----
    float padding  = 6.0f;
    float buttonH  = 30.0f;
    float btnH     = 24.0f;
    float btnY     = h - buttonH - padding;
    float btnW     = w * 0.35f;
    float gap      = 8.0f;
    float halfW    = w * 0.5f;

    // Accept button
    float acceptX = halfW - btnW - gap * 0.5f;
    if (localPos.y >= btnY && localPos.y < btnY + btnH &&
        localPos.x >= acceptX && localPos.x < acceptX + btnW) {
        if (myLocked && theirLocked && onAccept) {
            onAccept(id_);
        }
        return true;
    }

    // Cancel button
    float cancelX = halfW + gap * 0.5f;
    if (localPos.y >= btnY && localPos.y < btnY + btnH &&
        localPos.x >= cancelX && localPos.x < cancelX + btnW) {
        if (onCancel) onCancel(id_);
        return true;
    }

    // Lock button (left half, below slot grid)
    float headerH  = 28.0f;
    float contentH = h - headerH - buttonH - padding * 2.0f;
    float sideW    = halfW - padding * 1.5f;
    float sideX    = padding;

    float maxSlotW = (halfW - padding * 3.0f - 4.0f * 2.0f) / 3.0f;
    float maxSlotH = (contentH - 40.0f - 4.0f * 2.0f) / 3.0f;
    float slotSize = std::min(maxSlotW, maxSlotH);
    if (slotSize < 20.0f) slotSize = 20.0f;
    float gridH = slotSize * 3.0f + 4.0f * 2.0f;

    float gridStartY = headerH + 20.0f;
    float goldY  = gridStartY + gridH + 6.0f;
    float lockBtnW = sideW * 0.55f;
    float lockBtnH = 22.0f;
    float lockBtnX = sideX + (sideW - lockBtnW) * 0.5f;
    float lockBtnY = goldY + 14.0f;

    if (!myLocked &&
        localPos.x >= lockBtnX && localPos.x < lockBtnX + lockBtnW &&
        localPos.y >= lockBtnY && localPos.y < lockBtnY + lockBtnH) {
        if (onLock) onLock(id_);
        return true;
    }

    return true;  // consume all clicks on window
}

} // namespace fate
