#include "engine/ui/widgets/skill_arc.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cmath>

namespace fate {

static constexpr float DEG_TO_RAD = 3.14159265f / 180.0f;

SkillArc::SkillArc(const std::string& id) : UINode(id, "skill_arc") {}

std::vector<Vec2> SkillArc::computeSlotPositions() const {
    std::vector<Vec2> positions;
    if (slotCount <= 0) return positions;

    float startRad = startAngleDeg * DEG_TO_RAD;
    float endRad   = endAngleDeg   * DEG_TO_RAD;

    if (slotCount == 1) {
        float mid = (startRad + endRad) * 0.5f;
        positions.push_back({std::cos(mid) * arcRadius,
                             std::sin(mid) * arcRadius});
        return positions;
    }

    float step = (endRad - startRad) / static_cast<float>(slotCount - 1);
    for (int i = 0; i < slotCount; ++i) {
        float angle = startRad + step * static_cast<float>(i);
        positions.push_back({std::cos(angle) * arcRadius,
                             std::sin(angle) * arcRadius});
    }
    return positions;
}

int SkillArc::hitSlotIndex(const Vec2& localPos) const {
    // localPos is relative to arc center (0,0)
    float dist = localPos.length();

    // Attack button: centered at origin, circular hit area
    float attackRadius = attackButtonSize * 0.5f;
    if (dist <= attackRadius) return -1;

    // Check each slot
    auto positions = computeSlotPositions();
    float slotRadius = slotSize * 0.5f;
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

    // Draw skill slots
    auto positions = computeSlotPositions();
    for (int i = 0; i < static_cast<int>(positions.size()); ++i) {
        Vec2 slotCenter = {cx + positions[i].x, cy + positions[i].y};
        float slotR = slotSize * 0.5f;

        // Slot background (dark)
        Color slotBg = {0.15f, 0.15f, 0.2f, 0.85f};
        batch.drawCircle(slotCenter, slotR, slotBg, d, 16);

        // Slot border
        Color slotBorder = {0.5f, 0.5f, 0.7f, 0.9f};
        batch.drawRing(slotCenter, slotR, 2.0f, slotBorder, d + 0.1f, 16);

        // Cooldown overlay (dark arc proportional to remaining cooldown)
        if (i < static_cast<int>(slots.size()) && slots[i].cooldownTotal > 0.0f) {
            float ratio = slots[i].cooldownRemaining / slots[i].cooldownTotal;
            if (ratio > 0.001f) {
                Color cdColor = {0.0f, 0.0f, 0.0f, 0.6f};
                float cdEnd = -3.14159265f * 0.5f + ratio * 2.0f * 3.14159265f;
                batch.drawArc(slotCenter, slotR - 1.0f,
                              -3.14159265f * 0.5f, cdEnd,
                              cdColor, d + 0.2f, 16);
            }
        }
    }

    // Central attack button (drawn on top of slots)
    float attackR = attackButtonSize * 0.5f;
    Color attackBg = {0.8f, 0.2f, 0.2f, 0.9f};
    Color attackBorder = {1.0f, 0.5f, 0.5f, 1.0f};
    batch.drawCircle({cx, cy}, attackR, attackBg, d + 0.3f, 24);
    batch.drawRing({cx, cy}, attackR, 3.0f, attackBorder, d + 0.4f, 24);

    renderChildren(batch, sdf);
}

} // namespace fate
