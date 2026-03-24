#include "engine/ui/widgets/skill_arc.h"
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

    // Check each slot (positions already scaled via computeSlotPositions)
    auto positions = computeSlotPositions();
    float slotRadius = slotSize * s * 0.5f;
    for (int i = 0; i < static_cast<int>(positions.size()); ++i) {
        float dx = localPos.x - positions[i].x;
        float dy = localPos.y - positions[i].y;
        float slotDist = std::sqrt(dx * dx + dy * dy);
        if (slotDist <= slotRadius) return i;
    }

    return -2; // miss
}

bool SkillArc::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    // Convert local widget pos to arc-center-relative
    float cx = computedRect_.w * 0.5f;
    float cy = computedRect_.h * 0.5f;
    Vec2 arcLocal = {localPos.x - cx, localPos.y - cy};

    int hit = hitSlotIndex(arcLocal);
    if (hit == -1) {
        if (onAttack) onAttack(id_);
    } else if (hit >= 0) {
        if (onSkillSlot) onSkillSlot(hit);
    }
    return (hit != -2);
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
            float lvFontSize = 8.0f * s;
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
                    float cdFontSize = 11.0f * s;
                    Vec2 cdTs = sdf.measure(cdBuf, cdFontSize);
                    sdf.drawScreen(batch, cdBuf,
                        {slotCenter.x - cdTs.x * 0.5f, slotCenter.y - cdTs.y * 0.5f},
                        cdFontSize, Color(1.0f, 1.0f, 1.0f, 0.9f), d + 0.25f);
                }
            }
        }
    }

    // Central attack button (drawn on top of slots) — larger and more prominent
    float attackR = attackButtonSize * s * 0.5f;

    // Subtle outer glow (larger faded circle behind)
    Color attackGlow = {0.9f, 0.3f, 0.2f, 0.25f};
    batch.drawCircle({cx, cy}, attackR + 6.0f * s, attackGlow, d + 0.25f, 24);

    // Main attack button fill
    Color attackBg = {0.85f, 0.2f, 0.15f, 0.92f};
    batch.drawCircle({cx, cy}, attackR, attackBg, d + 0.3f, 24);

    // Inner highlight (top crescent effect)
    Color attackHighlight = {1.0f, 0.45f, 0.35f, 0.5f};
    batch.drawCircle({cx, cy - attackR * 0.15f}, attackR * 0.7f, attackHighlight, d + 0.35f, 20);

    // Thick border ring
    Color attackBorder = {1.0f, 0.55f, 0.45f, 1.0f};
    batch.drawRing({cx, cy}, attackR, 4.0f, attackBorder, d + 0.4f, 24);

    // "ATK" text inside attack button
    float atkFontSize = 14.0f * s;
    const char* atkText = "ATK";
    Vec2 atkTs = sdf.measure(atkText, atkFontSize);
    Color atkTextColor = {1.0f, 1.0f, 1.0f, 0.95f};
    sdf.drawScreen(batch, atkText,
        {cx - atkTs.x * 0.5f, cy - atkTs.y * 0.5f},
        atkFontSize, atkTextColor, d + 0.45f);

    renderChildren(batch, sdf);
}

void SkillArc::nextPage() {
    currentPage = (currentPage + 1) % TOTAL_PAGES;
}

void SkillArc::prevPage() {
    currentPage = (currentPage + TOTAL_PAGES - 1) % TOTAL_PAGES;
}

} // namespace fate
