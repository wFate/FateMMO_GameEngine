#include "engine/ui/widgets/status_panel.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <cmath>

namespace fate {

StatusPanel::StatusPanel(const std::string& id)
    : UINode(id, "status_panel") {}

// ---------------------------------------------------------------------------
// Character display — left side
// ---------------------------------------------------------------------------
void StatusPanel::renderCharacterDisplay(SpriteBatch& batch, SDFText& sdf,
                                          const Rect& area, float depth) {
    // Class icon diamond — small rotated square above-left of character
    float diamondSize = 16.0f;
    float diamondCX = area.x + diamondSize;
    float diamondCY = area.y + diamondSize;
    Color diamondFill   = {0.45f, 0.32f, 0.18f, 0.95f};
    Color diamondBorder = {0.65f, 0.50f, 0.28f, 1.0f};
    // Draw as two triangles (rotated square) using a small rect rotated 45°:
    // Approximate by drawing a circle with 4 segments
    batch.drawCircle({diamondCX, diamondCY}, diamondSize * 0.5f, diamondFill, depth + 0.1f, 4);
    batch.drawRing(  {diamondCX, diamondCY}, diamondSize * 0.5f, 1.5f, diamondBorder, depth + 0.2f, 4);

    // Character sprite placeholder rectangle
    float charW  = area.w * 0.72f;
    float charH  = area.h * 0.62f;
    float charCX = area.x + area.w * 0.5f;
    float charCY = area.y + area.h * 0.38f;

    Color charBg  = {0.68f, 0.62f, 0.50f, 0.9f};
    Color charBdr = {0.35f, 0.28f, 0.18f, 1.0f};
    float bw = 2.0f;
    float innerH = charH - bw * 2.0f;

    batch.drawRect({charCX, charCY}, {charW, charH}, charBg, depth);
    batch.drawRect({charCX, charCY - charH * 0.5f + bw * 0.5f}, {charW, bw}, charBdr, depth + 0.05f);
    batch.drawRect({charCX, charCY + charH * 0.5f - bw * 0.5f}, {charW, bw}, charBdr, depth + 0.05f);
    batch.drawRect({charCX - charW * 0.5f + bw * 0.5f, charCY}, {bw, innerH}, charBdr, depth + 0.05f);
    batch.drawRect({charCX + charW * 0.5f - bw * 0.5f, charCY}, {bw, innerH}, charBdr, depth + 0.05f);

    // Faction name banner below character
    if (!factionName.empty()) {
        float bannerW  = charW + 8.0f;
        float bannerH  = 18.0f;
        float bannerCX = charCX;
        float bannerCY = charCY + charH * 0.5f + bannerH * 0.5f + 4.0f;

        Color bannerBg  = {0.40f, 0.30f, 0.18f, 0.90f};
        Color bannerBdr = {0.55f, 0.42f, 0.25f, 1.0f};
        batch.drawRect({bannerCX, bannerCY}, {bannerW, bannerH}, bannerBg, depth + 0.1f);
        batch.drawRing({bannerCX, bannerCY}, bannerW * 0.5f, 1.0f, bannerBdr, depth + 0.15f, 4);

        float factionFontSize = 9.0f;
        Color factionColor = {1.0f, 0.92f, 0.75f, 1.0f};
        Vec2 fts = sdf.measure(factionName.c_str(), factionFontSize);
        sdf.drawScreen(batch, factionName.c_str(),
            {bannerCX - fts.x * 0.5f, bannerCY - fts.y * 0.5f},
            factionFontSize, factionColor, depth + 0.2f);
    }
}

// ---------------------------------------------------------------------------
// Stat grid — right side
// ---------------------------------------------------------------------------
void StatusPanel::renderStatGrid(SpriteBatch& batch, SDFText& sdf,
                                   const Rect& area, float depth) {
    // Inset panel background
    Color insetBg  = {0.75f, 0.68f, 0.55f, 0.90f};
    Color insetBdr = {0.40f, 0.30f, 0.20f, 0.85f};
    float insetPad = 4.0f;
    batch.drawRect({area.x + area.w * 0.5f, area.y + area.h * 0.5f},
                   {area.w, area.h}, insetBg, depth);
    float iH = area.h - 2.0f;
    batch.drawRect({area.x + area.w * 0.5f, area.y + 1.0f},        {area.w, 2.0f}, insetBdr, depth + 0.05f);
    batch.drawRect({area.x + area.w * 0.5f, area.y + area.h - 1.0f}, {area.w, 2.0f}, insetBdr, depth + 0.05f);
    batch.drawRect({area.x + 1.0f,          area.y + area.h * 0.5f}, {2.0f, iH},    insetBdr, depth + 0.05f);
    batch.drawRect({area.x + area.w - 1.0f, area.y + area.h * 0.5f}, {2.0f, iH},    insetBdr, depth + 0.05f);

    // 3-column × 3-row stat grid
    static const char* labels[9] = {
        "STR", "INT", "DEX",
        "CON", "WIS", "ARM",
        "HIT", "CRI", "SPD"
    };
    const int values[9] = {
        str, intl, dex,
        con, wis,  arm,
        hit, cri,  spd
    };

    int cols = 3;
    int rows = 3;
    float cellW = (area.w - insetPad * 2.0f) / static_cast<float>(cols);
    float cellH = (area.h - insetPad * 2.0f) / static_cast<float>(rows);
    float labelFontSize = 9.0f;
    float valueFontSize = 11.0f;
    Color labelColor = {0.50f, 0.40f, 0.30f, 1.0f};
    Color valueColor = {0.20f, 0.14f, 0.08f, 1.0f};

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            int idx = row * cols + col;
            float cellX = area.x + insetPad + static_cast<float>(col) * cellW;
            float cellY = area.y + insetPad + static_cast<float>(row) * cellH;

            // Abbreviation label
            Vec2 lts = sdf.measure(labels[idx], labelFontSize);
            sdf.drawScreen(batch, labels[idx],
                {cellX + cellW * 0.5f - lts.x * 0.5f, cellY + 2.0f},
                labelFontSize, labelColor, depth + 0.1f);

            // Value
            char vbuf[16];
            snprintf(vbuf, sizeof(vbuf), "%d", values[idx]);
            Vec2 vts = sdf.measure(vbuf, valueFontSize);
            sdf.drawScreen(batch, vbuf,
                {cellX + cellW * 0.5f - vts.x * 0.5f, cellY + labelFontSize + 4.0f},
                valueFontSize, valueColor, depth + 0.1f);
        }
    }
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------
void StatusPanel::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // ---- Parchment background ----
    Color bg  = {0.85f, 0.78f, 0.65f, 0.95f};
    Color bdr = {0.40f, 0.30f, 0.20f, 1.0f};
    float bw  = 3.0f;

    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, bg, d);
    float innerH = rect.h - bw * 2.0f;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f},          {rect.w, bw}, bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f}, {rect.w, bw}, bdr, d + 0.1f);
    batch.drawRect({rect.x + bw * 0.5f,     rect.y + rect.h * 0.5f},      {bw, innerH}, bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bdr, d + 0.1f);

    // ---- "STATUS" title ----
    Color titleColor = {0.28f, 0.18f, 0.08f, 1.0f};
    sdf.drawScreen(batch, "STATUS",
        {rect.x + 10.0f, rect.y + 6.0f},
        16.0f, titleColor, d + 0.2f);

    // ---- Close button (X circle at top-right) ----
    float closeR  = 12.0f;
    float closeCX = rect.x + rect.w - closeR - 6.0f;
    float closeCY = rect.y + closeR + 6.0f;
    Color closeBg  = {0.55f, 0.42f, 0.28f, 1.0f};
    Color closeBdr = {0.30f, 0.20f, 0.10f, 1.0f};
    Color closeX   = {1.0f, 0.95f, 0.88f, 1.0f};
    batch.drawCircle({closeCX, closeCY}, closeR, closeBg,  d + 0.2f, 16);
    batch.drawRing  ({closeCX, closeCY}, closeR, 1.5f, closeBdr, d + 0.3f, 16);
    Vec2 xts = sdf.measure("X", 12.0f);
    sdf.drawScreen(batch, "X",
        {closeCX - xts.x * 0.5f, closeCY - xts.y * 0.5f},
        12.0f, closeX, d + 0.4f);

    // ---- Layout: left ~40% = character, right ~60% = stats ----
    float headerH = 28.0f;
    float contentY = rect.y + headerH;
    float contentH = rect.h - headerH;

    float leftW  = rect.w * 0.40f;
    float rightW = rect.w - leftW;

    Rect charArea  = {rect.x + 4.0f,        contentY + 2.0f, leftW  - 8.0f, contentH - 4.0f};
    Rect statsArea = {rect.x + leftW + 4.0f, contentY + 2.0f, rightW - 8.0f, contentH - 4.0f};

    renderCharacterDisplay(batch, sdf, charArea, d + 0.15f);

    // ---- Right side: name, level, XP bar, stat grid ----
    float curY = statsArea.y + 2.0f;

    // Player name (large, dark brown)
    if (!playerName.empty()) {
        float nameFontSize = 15.0f;
        Color nameColor = {0.25f, 0.16f, 0.08f, 1.0f};
        sdf.drawScreen(batch, playerName.c_str(),
            {statsArea.x, curY}, nameFontSize, nameColor, d + 0.2f);
        curY += nameFontSize + 3.0f;
    }

    // "Lv N" below name
    {
        char lvBuf[24];
        snprintf(lvBuf, sizeof(lvBuf), "Lv %d  %s", level, className.c_str());
        float lvFontSize = 11.0f;
        Color lvColor = {0.40f, 0.28f, 0.16f, 1.0f};
        sdf.drawScreen(batch, lvBuf,
            {statsArea.x, curY}, lvFontSize, lvColor, d + 0.2f);
        curY += lvFontSize + 5.0f;
    }

    // XP bar
    {
        float barW = statsArea.w - 4.0f;
        float barH = 8.0f;
        float barX = statsArea.x;
        float barY = curY;

        // Background
        Color xpBg = {0.65f, 0.58f, 0.45f, 0.9f};
        batch.drawRect({barX + barW * 0.5f, barY + barH * 0.5f}, {barW, barH}, xpBg, d + 0.2f);

        // Fill (light blue)
        float xpRatio = (xpToLevel > 0.0f) ? (xp / xpToLevel) : 0.0f;
        if (xpRatio > 1.0f) xpRatio = 1.0f;
        if (xpRatio > 0.001f) {
            float fillW = barW * xpRatio;
            Color xpFill = {0.35f, 0.65f, 0.92f, 0.95f};
            batch.drawRect({barX + fillW * 0.5f, barY + barH * 0.5f}, {fillW, barH}, xpFill, d + 0.25f);
        }

        // Border
        Color xpBdr = {0.38f, 0.28f, 0.18f, 0.9f};
        batch.drawRect({barX + barW * 0.5f, barY},               {barW, 1.0f}, xpBdr, d + 0.3f);
        batch.drawRect({barX + barW * 0.5f, barY + barH},        {barW, 1.0f}, xpBdr, d + 0.3f);
        batch.drawRect({barX,               barY + barH * 0.5f}, {1.0f, barH}, xpBdr, d + 0.3f);
        batch.drawRect({barX + barW,        barY + barH * 0.5f}, {1.0f, barH}, xpBdr, d + 0.3f);

        curY += barH + 8.0f;
    }

    // Stat grid fills the remaining right-side area
    Rect gridArea = {statsArea.x, curY, statsArea.w, statsArea.y + statsArea.h - curY};
    if (gridArea.h > 20.0f) {
        renderStatGrid(batch, sdf, gridArea, d + 0.15f);
    }

    renderChildren(batch, sdf);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
bool StatusPanel::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    float closeR  = 12.0f;
    float closeCX = computedRect_.w - closeR - 6.0f;
    float closeCY = closeR + 6.0f;
    float dx = localPos.x - closeCX;
    float dy = localPos.y - closeCY;
    if (dx * dx + dy * dy <= closeR * closeR) {
        if (onClose) onClose(id_);
        return true;
    }

    return true;  // consume all clicks on the panel
}

} // namespace fate
