#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include <vector>
#include <functional>
#include <string>

namespace fate {

struct SkillSlotData {
    std::string skillId;
    int   level             = 0;
    float cooldownRemaining = 0.0f;
    float cooldownTotal     = 0.0f;
};

class SkillArc : public UINode {
public:
    SkillArc(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;

    // Compute slot positions relative to the arc center (0, 0).
    std::vector<Vec2> computeSlotPositions() const;

    // Hit test in local space relative to arc center.
    // Returns: -1 = attack button hit, -2 = miss, 0..N-1 = slot index
    int hitSlotIndex(const Vec2& localPos) const;

    float attackButtonSize = 80.0f;  // diameter of the central attack button
    float slotSize         = 52.0f;  // diameter of each skill slot
    float arcRadius        = 70.0f;  // distance from center to slot centers
    int   slotCount        = 5;
    float startAngleDeg    = 210.0f; // arc start angle in degrees (0=right, CCW)
    float endAngleDeg      = 330.0f; // arc end angle in degrees

    std::vector<SkillSlotData> slots;

    UIClickCallback onAttack;
    std::function<void(int slotIndex)> onSkillSlot;
};

} // namespace fate
