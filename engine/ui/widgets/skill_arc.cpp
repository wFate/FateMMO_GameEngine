#include "engine/ui/widgets/skill_arc.h"
#include "engine/ui/widgets/metallic_draw.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cmath>
#include <cstdio>

namespace fate {

static constexpr float DEG_TO_RAD = 3.14159265f / 180.0f;

SkillArc::SkillArc(const std::string& id) : UINode(id, "skill_arc") {}

std::vector<Vec2> SkillArc::computeSlotPositions() const {
    std::vector<Vec2> positions;
    if (slotCount <= 0) return positions;

    float s = layoutScale_;
    float scaledRadius = arcRadius * s;
    float startRad = startAngleDeg * DEG_TO_RAD;
    float endRad   = endAngleDeg   * DEG_TO_RAD;

    if (slotCount == 1) {
        float mid = (startRad + endRad) * 0.5f;
        positions.push_back({std::cos(mid) * scaledRadius,
                             std::sin(mid) * scaledRadius});
        return positions;
    }

    float step = (endRad - startRad) / static_cast<float>(slotCount - 1);
    for (int i = 0; i < slotCount; ++i) {
        float angle = startRad + step * static_cast<float>(i);
        positions.push_back({std::cos(angle) * scaledRadius,
                             std::sin(angle) * scaledRadius});
    }
    return positions;
}

int SkillArc::hitSlotIndex(const Vec2& localPos) const {
    // localPos is relative to arc center (0,0)
    float s = layoutScale_;
    float dist = localPos.length();

    // Attack button: centered at origin, circular hit area
    float attackRadius = attackButtonSize * s * 0.5f;
    if (dist <= attackRadius) return -1;

    // Pick Up button: centered above attack button
    Vec2 pickUpCenter{0, -85.0f * s};
    float pickUpRadius = pickUpButtonSize * s * 0.5f;
    float pdx = localPos.x - pickUpCenter.x;
    float pdy = localPos.y - pickUpCenter.y;
    float pickUpDist = std::sqrt(pdx * pdx + pdy * pdy);
    if (pickUpDist <= pickUpRadius) return -2;

    // Page selector circles (4 circles near top of arc)
    float pageDotRadius = 12.0f * s;  // 24px diameter
    float pageDotSpacing = 28.0f * s;
    float pageDotY = -120.0f * s;     // above pick up button
    float pageDotStartX = -1.5f * pageDotSpacing;
    for (int i = 0; i < TOTAL_PAGES; ++i) {
        float dotX = pageDotStartX + static_cast<float>(i) * pageDotSpacing;
        float ddx = localPos.x - dotX;
        float ddy = localPos.y - pageDotY;
        float dotDist = std::sqrt(ddx * ddx + ddy * ddy);
        if (dotDist <= pageDotRadius) return -(10 + i);  // -10..-13
    }

    // Check each slot (positions already scaled via computeSlotPositions)
    auto positions = computeSlotPositions();
    float slotRadius = slotSize * s * 0.5f;
    for (int i = 0; i < static_cast<int>(positions.size()); ++i) {
        float dx = localPos.x - positions[i].x;
        float dy = localPos.y - positions[i].y;
        float slotDist = std::sqrt(dx * dx + dy * dy);
        if (slotDist <= slotRadius) return i;
    }

    return -99; // miss
}

bool SkillArc::onPress(const Vec2& localPos) {
    if (!enabled_) return false;
    float cx = computedRect_.w * 0.5f;
    float cy = computedRect_.h * 0.5f;
    Vec2 rel{localPos.x - cx, localPos.y - cy};
    int hit = hitSlotIndex(rel);
    if (hit == -1) {
        if (onAttack) onAttack(id_);
        return true;
    } else if (hit == -2) {
        if (onPickUp) onPickUp(id_);
        return true;
    } else if (hit >= -13 && hit <= -10) {
        setPage(-(hit + 10)); // -10 -> page 0, -13 -> page 3
        return true;
    } else if (hit >= 0) {
        if (onSkillSlot) onSkillSlot(hit);
        return true;
    }
    return false;
}

void SkillArc::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);
    float cx = rect.x + rect.w * 0.5f;
    float cy = rect.y + rect.h * 0.5f;
    float s = layoutScale_;

    // Draw skill slots (positions already scaled via computeSlotPositions)
    auto positions = computeSlotPositions();
    for (int i = 0; i < static_cast<int>(positions.size()); ++i) {
        Vec2 slotCenter = {cx + positions[i].x, cy + positions[i].y};
        float slotR = slotSize * s * 0.5f;

        bool hasFill = (i < static_cast<int>(slots.size()) && !slots[i].skillId.empty());

        if (hasFill) {
            // Filled slot: full opacity, warm dark background
            Color slotBg = {0.18f, 0.16f, 0.22f, 0.88f};
            batch.drawCircle(slotCenter, slotR, slotBg, d, 20);

            // Filled slot border (slightly brighter/warmer)
            Color slotBorder = {0.60f, 0.55f, 0.72f, 0.95f};
            batch.drawRing(slotCenter, slotR, 3.0f, slotBorder, d + 0.1f, 20);

            // Level indicator at bottom-right of slot
            char lvBuf[8];
            std::snprintf(lvBuf, sizeof(lvBuf), "LV%d", slots[i].level > 0 ? slots[i].level : 1);
            float lvFontSize = scaledFont(8.0f);
            Vec2 lvTs = sdf.measure(lvBuf, lvFontSize);
            Color lvColor = {1.0f, 0.9f, 0.6f, 0.9f};
            sdf.drawScreen(batch, lvBuf,
                {slotCenter.x + slotR * 0.2f - lvTs.x * 0.5f,
                 slotCenter.y + slotR * 0.55f},
                lvFontSize, lvColor, d + 0.25f);
        } else {
            // Empty slot: faded, semi-transparent outline only
            Color emptyBg = {0.15f, 0.15f, 0.18f, 0.35f};
            batch.drawCircle(slotCenter, slotR, emptyBg, d, 16);

            // Visible outline ring
            Color emptyBorder = {0.50f, 0.48f, 0.58f, 0.55f};
            batch.drawRing(slotCenter, slotR, 2.5f, emptyBorder, d + 0.1f, 16);

            // "+" placeholder inside empty slot
            Color plusColor = {0.6f, 0.6f, 0.65f, 0.4f};
            float crossW = slotR * 0.15f;
            float crossH = slotR * 0.6f;
            batch.drawRect(slotCenter, {crossW, crossH}, plusColor, d + 0.12f);
            batch.drawRect(slotCenter, {crossH, crossW}, plusColor, d + 0.12f);
        }

        // Cooldown overlay — TWOM-style clockwise radial sweep from 12 o'clock
        if (i < static_cast<int>(slots.size()) && slots[i].cooldownTotal > 0.0f) {
            float ratio = slots[i].cooldownRemaining / slots[i].cooldownTotal;
            if (ratio > 0.001f) {
                // Dark fill sweeps clockwise from top (–π/2)
                Color cdColor = {0.0f, 0.0f, 0.0f, 0.65f};
                float startAngle = -3.14159265f * 0.5f;
                float cdEnd = startAngle + ratio * 2.0f * 3.14159265f;
                batch.drawArc(slotCenter, slotR - 1.0f,
                              startAngle, cdEnd,
                              cdColor, d + 0.2f, 32);

                // Leading edge highlight (thin bright line at sweep boundary)
                float edgeAngle = cdEnd;
                float edgeLen = slotR - 2.0f;
                Vec2 edgeTip = {slotCenter.x + std::cos(edgeAngle) * edgeLen,
                                slotCenter.y + std::sin(edgeAngle) * edgeLen};
                Color edgeColor = {1.0f, 1.0f, 1.0f, 0.3f};
                float lineW = 1.5f;
                // Approximate line as thin rect from center to edge
                Vec2 mid = {(slotCenter.x + edgeTip.x) * 0.5f,
                            (slotCenter.y + edgeTip.y) * 0.5f};
                float dx = edgeTip.x - slotCenter.x;
                float dy = edgeTip.y - slotCenter.y;
                float len = std::sqrt(dx * dx + dy * dy);
                if (len > 1.0f) {
                    // Perpendicular normal for thickness
                    float nx = -dy / len * lineW * 0.5f;
                    float ny =  dx / len * lineW * 0.5f;
                    // Draw as small rect centered on midpoint
                    batch.drawRect(mid, {len, lineW}, edgeColor, d + 0.22f);
                }

                // Remaining seconds text centered on slot
                if (slots[i].cooldownRemaining >= 1.0f) {
                    char cdBuf[8];
                    int secs = static_cast<int>(slots[i].cooldownRemaining + 0.5f);
                    std::snprintf(cdBuf, sizeof(cdBuf), "%d", secs);
                    float cdFontSize = scaledFont(11.0f);
                    Vec2 cdTs = sdf.measure(cdBuf, cdFontSize);
                    sdf.drawScreen(batch, cdBuf,
                        {slotCenter.x - cdTs.x * 0.5f, slotCenter.y - cdTs.y * 0.5f},
                        cdFontSize, Color(1.0f, 1.0f, 1.0f, 0.9f), d + 0.25f);
                }
            }
        }
    }

    // Central attack button — gold metallic
    float attackR = attackButtonSize * s * 0.5f;
    Vec2 center{cx, cy};
    drawMetallicCircle(batch, center, attackR, d + 0.3f);

    // Crossed-swords icon: two crossed line-rects in dark brown
    {
        Color swordColor = {0.30f, 0.20f, 0.10f, 0.9f};
        float halfLen = attackR * 0.28f;
        float iconW   = 3.0f * s;
        // Sword 1: top-left to bottom-right (diagonal approximated as short H + V)
        batch.drawRect({cx - halfLen * 0.4f, cy - halfLen * 0.2f}, {halfLen * 1.4f, iconW}, swordColor, d + 0.42f);
        batch.drawRect({cx + halfLen * 0.1f, cy - halfLen * 0.1f}, {iconW, halfLen * 1.2f}, swordColor, d + 0.42f);
        // Sword 2: top-right to bottom-left (mirrored)
        batch.drawRect({cx - halfLen * 0.1f, cy - halfLen * 0.2f}, {halfLen * 1.4f, iconW}, swordColor, d + 0.42f);
        batch.drawRect({cx - halfLen * 0.4f, cy - halfLen * 0.1f}, {iconW, halfLen * 1.2f}, swordColor, d + 0.42f);
    }

    // "Action" text below icon
    {
        float actionFontSize = scaledFont(10.0f);
        const char* actionText = "Action";
        Vec2 actionTs = sdf.measure(actionText, actionFontSize);
        Color actionTextColor = {0.30f, 0.20f, 0.10f, 0.95f};
        sdf.drawScreen(batch, actionText,
            {cx - actionTs.x * 0.5f, cy + attackR * 0.25f},
            actionFontSize, actionTextColor, d + 0.45f);
    }

    // Pick Up button — above Attack
    {
        float pickUpR = pickUpButtonSize * s * 0.5f;
        Vec2 pickUpCenter{cx, cy - 85.0f * s};
        drawMetallicCircle(batch, pickUpCenter, pickUpR, d + 0.3f);

        // "Pick up" text centered in button
        float puFontSize = scaledFont(9.0f);
        const char* puText = "Pick up";
        Vec2 puTs = sdf.measure(puText, puFontSize);
        Color puTextColor = {0.30f, 0.20f, 0.10f, 0.95f};
        sdf.drawScreen(batch, puText,
            {pickUpCenter.x - puTs.x * 0.5f, pickUpCenter.y - puTs.y * 0.5f},
            puFontSize, puTextColor, d + 0.45f);
    }

    // Page selector circles (4 dots near top of arc)
    {
        float pageDotR = 12.0f * s;
        float pageDotSpacing = 28.0f * s;
        float pageDotY = cy - 120.0f * s;
        float pageDotStartX = cx - 1.5f * pageDotSpacing;
        for (int i = 0; i < TOTAL_PAGES; ++i) {
            float dotX = pageDotStartX + static_cast<float>(i) * pageDotSpacing;
            Vec2 dotCenter{dotX, pageDotY};
            if (i == currentPage) {
                drawMetallicCircle(batch, dotCenter, pageDotR, d + 0.35f);
            } else {
                Color inactiveColor = {0.45f, 0.45f, 0.50f, 0.7f};
                batch.drawCircle(dotCenter, pageDotR, inactiveColor, d + 0.35f, 16);
            }
            // Page number label
            char pgBuf[4];
            std::snprintf(pgBuf, sizeof(pgBuf), "%d", i + 1);
            float pgFontSize = scaledFont(8.0f);
            Vec2 pgTs = sdf.measure(pgBuf, pgFontSize);
            Color pgTextColor = (i == currentPage)
                ? Color{0.30f, 0.20f, 0.10f, 0.95f}
                : Color{1.0f, 1.0f, 1.0f, 0.8f};
            sdf.drawScreen(batch, pgBuf,
                {dotCenter.x - pgTs.x * 0.5f, dotCenter.y - pgTs.y * 0.5f},
                pgFontSize, pgTextColor, d + 0.4f);
        }
    }

    renderChildren(batch, sdf);
}

void SkillArc::nextPage() {
    currentPage = (currentPage + 1) % TOTAL_PAGES;
}

void SkillArc::prevPage() {
    currentPage = (currentPage + TOTAL_PAGES - 1) % TOTAL_PAGES;
}

void SkillArc::setPage(int page) {
    if (page >= 0 && page < TOTAL_PAGES) currentPage = page;
}

} // namespace fate
