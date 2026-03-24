#include "engine/ui/widgets/skill_panel.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <cmath>

namespace fate {

static constexpr float DEG_TO_RAD = 3.14159265f / 180.0f;

SkillPanel::SkillPanel(const std::string& id)
    : UINode(id, "skill_panel") {}

// ---------------------------------------------------------------------------
// Skill Wheel — left side
// ---------------------------------------------------------------------------
void SkillPanel::renderSkillWheel(SpriteBatch& batch, SDFText& sdf,
                                    const Rect& area, float depth) {
    // ---- 5 numbered page tabs at top ----
    int numTabs = 5;
    float tabR  = 14.0f;
    float tabSpacing = tabR * 2.5f;
    float totalTabW  = static_cast<float>(numTabs) * tabSpacing;
    float tabStartX  = area.x + area.w * 0.5f - totalTabW * 0.5f + tabR;
    float tabY = area.y + tabR + 2.0f;

    for (int i = 0; i < numTabs; ++i) {
        float cx = tabStartX + static_cast<float>(i) * tabSpacing;
        bool isActive = (i == activeSetPage);

        Color tabBg = isActive
            ? Color{0.30f, 0.52f, 0.85f, 1.0f}   // active: blue fill
            : Color{0.60f, 0.54f, 0.42f, 0.85f};  // inactive: muted tan
        Color tabBdr = isActive
            ? Color{0.55f, 0.78f, 1.0f, 1.0f}
            : Color{0.40f, 0.32f, 0.22f, 0.85f};

        batch.drawCircle({cx, tabY}, tabR, tabBg,  depth + 0.1f, 20);
        batch.drawRing(  {cx, tabY}, tabR, 1.5f, tabBdr, depth + 0.2f, 20);

        char tabNum[4];
        snprintf(tabNum, sizeof(tabNum), "%d", i + 1);
        float numFontSize = 11.0f;
        Color numColor = isActive
            ? Color{1.0f, 1.0f, 1.0f, 1.0f}
            : Color{0.85f, 0.80f, 0.72f, 1.0f};
        Vec2 nts = sdf.measure(tabNum, numFontSize);
        sdf.drawScreen(batch, tabNum,
            {cx - nts.x * 0.5f, tabY - nts.y * 0.5f},
            numFontSize, numColor, depth + 0.3f);
    }

    // ---- Semicircular arc showing current slot assignments ----
    // Arc center in the lower half of the area
    float arcContentY = tabY + tabR + 4.0f;
    float arcAvailH   = area.y + area.h - arcContentY - 30.0f; // leave room for remaining pts
    float arcRadius   = (arcAvailH * 0.48f < area.w * 0.38f)
                          ? arcAvailH * 0.48f : area.w * 0.38f;
    if (arcRadius < 20.0f) arcRadius = 20.0f;

    float arcCX = area.x + area.w * 0.5f;
    float arcCY = arcContentY + arcRadius + 4.0f;

    float slotSize = arcRadius * 0.40f;
    if (slotSize < 12.0f) slotSize = 12.0f;

    // Draw 5 arc slots spaced over a 120-degree upward arc (210°→330° from center-bottom)
    int slotCount = 5;
    float startRad = 210.0f * DEG_TO_RAD;
    float endRad   = 330.0f * DEG_TO_RAD;
    float step     = (slotCount > 1) ? ((endRad - startRad) / static_cast<float>(slotCount - 1)) : 0.0f;

    for (int i = 0; i < slotCount; ++i) {
        float angle  = startRad + step * static_cast<float>(i);
        float slotCX = arcCX + std::cos(angle) * arcRadius;
        float slotCY = arcCY + std::sin(angle) * arcRadius;
        float slotR  = slotSize * 0.5f;

        // Empty arc slot
        Color slotBg  = {0.68f, 0.62f, 0.50f, 0.85f};
        Color slotBdr = {0.42f, 0.32f, 0.20f, 0.90f};
        batch.drawCircle({slotCX, slotCY}, slotR, slotBg,  depth + 0.1f, 16);
        batch.drawRing  ({slotCX, slotCY}, slotR, 1.5f, slotBdr, depth + 0.2f, 16);

        // Slot number label
        char sNum[4];
        snprintf(sNum, sizeof(sNum), "%d", i + 1);
        float snFontSize = 8.0f;
        Color snColor = {0.35f, 0.25f, 0.14f, 0.85f};
        Vec2 snts = sdf.measure(sNum, snFontSize);
        sdf.drawScreen(batch, sNum,
            {slotCX - snts.x * 0.5f, slotCY - snts.y * 0.5f},
            snFontSize, snColor, depth + 0.3f);
    }

    // ---- "Remaining Points N" at bottom with orange badge ----
    float ptsBadgeR = 12.0f;
    float ptsCX = arcCX;
    float ptsCY = area.y + area.h - ptsBadgeR - 4.0f;

    Color badgeBg  = (remainingPoints > 0)
        ? Color{0.88f, 0.50f, 0.10f, 1.0f}   // orange if points available
        : Color{0.45f, 0.38f, 0.28f, 0.85f};  // muted if none
    Color badgeBdr = {0.30f, 0.22f, 0.10f, 1.0f};

    batch.drawCircle({ptsCX, ptsCY}, ptsBadgeR, badgeBg,  depth + 0.1f, 20);
    batch.drawRing  ({ptsCX, ptsCY}, ptsBadgeR, 1.5f, badgeBdr, depth + 0.2f, 20);

    char ptsBuf[8];
    snprintf(ptsBuf, sizeof(ptsBuf), "%d", remainingPoints);
    float ptsFontSize = 10.0f;
    Color ptsTextColor = {1.0f, 1.0f, 0.9f, 1.0f};
    Vec2 pts = sdf.measure(ptsBuf, ptsFontSize);
    sdf.drawScreen(batch, ptsBuf,
        {ptsCX - pts.x * 0.5f, ptsCY - pts.y * 0.5f},
        ptsFontSize, ptsTextColor, depth + 0.3f);

    // "pts" label beside badge
    float lblFontSize = 8.0f;
    Color lblColor = {0.35f, 0.25f, 0.14f, 0.9f};
    sdf.drawScreen(batch, "pts",
        {ptsCX + ptsBadgeR + 3.0f, ptsCY - lblFontSize * 0.5f},
        lblFontSize, lblColor, depth + 0.3f);
}

// ---------------------------------------------------------------------------
// Skill List — right side
// ---------------------------------------------------------------------------
void SkillPanel::renderSkillList(SpriteBatch& batch, SDFText& sdf,
                                   const Rect& area, float depth) {
    // "Skills" header
    float headerFontSize = 13.0f;
    Color headerColor = {0.28f, 0.18f, 0.08f, 1.0f};
    sdf.drawScreen(batch, "Skills",
        {area.x + 4.0f, area.y + 2.0f},
        headerFontSize, headerColor, depth + 0.1f);

    float headerH = headerFontSize + 6.0f;
    float gridY   = area.y + headerH;
    float gridH   = area.h - headerH;

    // 4-column grid of skill circles
    int cols = 4;
    float cellSize = (area.w - 8.0f) / static_cast<float>(cols);
    if (cellSize < 16.0f) cellSize = 16.0f;

    float skillR = cellSize * 0.38f;
    int numSkills = static_cast<int>(classSkills.size());

    int dotsCount  = 5;  // level dots below each skill
    float dotSize  = 4.0f;
    float dotSpacing = dotSize + 2.0f;
    float cellH    = skillR * 2.0f + dotSize + 8.0f + 4.0f;  // circle + dot row + label space

    for (int i = 0; i < numSkills; ++i) {
        int col = i % cols;
        int row = i / cols;

        float cx = area.x + 4.0f + cellSize * 0.5f + static_cast<float>(col) * cellSize;
        float cy = gridY + skillR + 4.0f + static_cast<float>(row) * cellH;

        const SkillInfo& sk = classSkills[static_cast<size_t>(i)];
        bool isSelected = (i == selectedSkillIndex);

        // Skill circle background
        Color skillBg = sk.unlocked
            ? Color{0.60f, 0.54f, 0.42f, 0.90f}
            : Color{0.48f, 0.43f, 0.34f, 0.70f};
        batch.drawCircle({cx, cy}, skillR, skillBg, depth + 0.1f, 20);

        // Ring border — gold if selected, brown otherwise
        Color ringColor = isSelected
            ? Color{1.0f, 0.82f, 0.20f, 1.0f}   // gold highlight
            : Color{0.42f, 0.32f, 0.20f, 0.85f};
        float ringW = isSelected ? 2.5f : 1.5f;
        batch.drawRing({cx, cy}, skillR, ringW, ringColor, depth + 0.2f, 20);

        // Level dots below the circle
        float dotsStartX = cx - (static_cast<float>(dotsCount) * dotSpacing - 2.0f) * 0.5f;
        float dotsY      = cy + skillR + 4.0f;

        for (int d = 0; d < dotsCount; ++d) {
            float dotCX = dotsStartX + static_cast<float>(d) * dotSpacing;
            bool filled = (d < sk.currentLevel);
            Color dotColor = filled
                ? Color{0.90f, 0.52f, 0.12f, 1.0f}   // filled: orange
                : Color{0.55f, 0.50f, 0.40f, 0.70f};  // empty: grey
            batch.drawCircle({dotCX, dotsY + dotSize * 0.5f}, dotSize * 0.5f, dotColor, depth + 0.2f, 10);
        }

        // Skill name truncated below dots
        if (!sk.name.empty()) {
            float nameFontSize = 7.0f;
            Color nameColor = sk.unlocked
                ? Color{0.22f, 0.15f, 0.08f, 1.0f}
                : Color{0.50f, 0.44f, 0.35f, 0.75f};
            // Truncate to first 5 chars if too wide
            std::string displayName = sk.name.substr(0, 5);
            Vec2 nts = sdf.measure(displayName.c_str(), nameFontSize);
            sdf.drawScreen(batch, displayName.c_str(),
                {cx - nts.x * 0.5f, dotsY + dotSize + 2.0f},
                nameFontSize, nameColor, depth + 0.3f);
        }
    }
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------
void SkillPanel::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    const auto& style = resolvedStyle_;
    float d = static_cast<float>(zOrder_);

    // ---- Parchment background ----
    Color bg  = (style.backgroundColor.a > 0.0f)
              ? style.backgroundColor
              : Color{0.85f, 0.78f, 0.65f, 0.95f};
    bg.a *= style.opacity;
    Color bdr = (style.borderColor.a > 0.0f)
              ? style.borderColor
              : Color{0.40f, 0.30f, 0.20f, 1.0f};
    float bw  = 3.0f;

    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, bg, d);
    float innerH = rect.h - bw * 2.0f;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f},          {rect.w, bw}, bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f}, {rect.w, bw}, bdr, d + 0.1f);
    batch.drawRect({rect.x + bw * 0.5f,     rect.y + rect.h * 0.5f},      {bw, innerH}, bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bdr, d + 0.1f);

    // ---- "SKILL" title ----
    Color titleColor = style.textColor;
    sdf.drawScreen(batch, "SKILL",
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

    // ---- Layout: left ~40% = skill wheel, right ~60% = skill list ----
    float headerH = 28.0f;
    float contentY = rect.y + headerH;
    float contentH = rect.h - headerH;

    float leftW  = rect.w * 0.42f;
    float rightW = rect.w - leftW;

    Rect wheelArea = {rect.x + 4.0f,         contentY + 2.0f, leftW  - 8.0f, contentH - 4.0f};
    Rect listArea  = {rect.x + leftW + 4.0f,  contentY + 2.0f, rightW - 8.0f, contentH - 4.0f};

    // Divider line between the two sides
    Color divColor = {0.40f, 0.30f, 0.20f, 0.50f};
    batch.drawRect({rect.x + leftW, contentY + contentH * 0.5f},
                   {1.5f, contentH}, divColor, d + 0.1f);

    renderSkillWheel(batch, sdf, wheelArea, d + 0.15f);
    renderSkillList (batch, sdf, listArea,  d + 0.15f);

    renderChildren(batch, sdf);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
bool SkillPanel::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    // Close button hit test
    float closeR  = 12.0f;
    float closeCX = computedRect_.w - closeR - 6.0f;
    float closeCY = closeR + 6.0f;
    float dx = localPos.x - closeCX;
    float dy = localPos.y - closeCY;
    if (dx * dx + dy * dy <= closeR * closeR) {
        if (onClose) onClose(id_);
        return true;
    }

    // Skill set page tabs (top of left side)
    float headerH = 28.0f;
    float leftW   = computedRect_.w * 0.42f;
    Rect wheelArea = {4.0f, headerH + 2.0f, leftW - 8.0f, computedRect_.h - headerH - 4.0f};

    int numTabs   = 5;
    float tabR    = 14.0f;
    float tabSpacing = tabR * 2.5f;
    float totalTabW  = static_cast<float>(numTabs) * tabSpacing;
    float tabStartX  = wheelArea.x + wheelArea.w * 0.5f - totalTabW * 0.5f + tabR;
    float tabY       = wheelArea.y + tabR + 2.0f;

    for (int i = 0; i < numTabs; ++i) {
        float cx = tabStartX + static_cast<float>(i) * tabSpacing;
        float tdx = localPos.x - cx;
        float tdy = localPos.y - tabY;
        if (tdx * tdx + tdy * tdy <= tabR * tabR) {
            activeSetPage = i;
            return true;
        }
    }

    // Skill list grid — select skill
    Rect listArea  = {leftW + 4.0f, headerH + 2.0f, computedRect_.w - leftW - 8.0f, computedRect_.h - headerH - 4.0f};
    float headerFontH = 13.0f + 6.0f;
    float gridY = listArea.y + headerFontH;

    int cols = 4;
    float cellSize = (listArea.w - 8.0f) / static_cast<float>(cols);
    if (cellSize < 16.0f) cellSize = 16.0f;
    float skillR = cellSize * 0.38f;
    float dotSize = 4.0f;
    float cellH  = skillR * 2.0f + dotSize + 8.0f + 4.0f;

    int numSkills = static_cast<int>(classSkills.size());
    for (int i = 0; i < numSkills; ++i) {
        int col = i % cols;
        int row = i / cols;
        float cx = listArea.x + 4.0f + cellSize * 0.5f + static_cast<float>(col) * cellSize;
        float cy = gridY + skillR + 4.0f + static_cast<float>(row) * cellH;
        float dx2 = localPos.x - cx;
        float dy2 = localPos.y - cy;
        if (dx2 * dx2 + dy2 * dy2 <= skillR * skillR) {
            selectedSkillIndex = (selectedSkillIndex == i) ? -1 : i;
            return true;
        }
    }

    return true;  // consume all clicks on the panel
}

} // namespace fate
