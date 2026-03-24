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
    // hitSlotIndex returns: -1=Attack, -2=PickUp, -10..-13=page 0-3, 0..N=skill slot, -99=miss
    int hitSlotIndex(const Vec2& localPos) const;

    float attackButtonSize = 80.0f;  // diameter of the central attack button
    float pickUpButtonSize = 60.0f;  // diameter of the pick up button
    float slotSize         = 52.0f;  // diameter of each skill slot
    float arcRadius        = 70.0f;  // distance from center to slot centers
    int   slotCount        = 3;
    float startAngleDeg    = 200.0f; // arc start angle in degrees (0=right, CCW)
    float endAngleDeg      = 310.0f; // arc end angle in degrees

    std::vector<SkillSlotData> slots;

    UIClickCallback onAttack;
    UIClickCallback onPickUp;
    std::function<void(int slotIndex)> onSkillSlot;

    // Skill activation callback (game_app resolves skill from global slot index)
    std::function<void(const std::string& skillId, int rank)> onSkillActivated;

    // Page management (replaces SkillBarUI singleton)
    int currentPage = 0;
    static constexpr int SLOTS_PER_PAGE = 3;
    static constexpr int TOTAL_PAGES = 4;

    void nextPage();
    void prevPage();
    void setPage(int page);
};

} // namespace fate
