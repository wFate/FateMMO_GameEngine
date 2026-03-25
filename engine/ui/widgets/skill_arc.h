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
    bool hitTest(const Vec2& point) const override;

    // Compute slot positions relative to the arc center (0, 0).
    std::vector<Vec2> computeSlotPositions() const;

    // Hit test in local space relative to arc center.
    // hitSlotIndex returns: -1=Attack, -2=PickUp, -10..-13=page 0-3, 0..N=skill slot, -99=miss
    int hitSlotIndex(const Vec2& localPos) const;

    float attackButtonSize = 80.0f;  // diameter of the attack button
    float pickUpButtonSize = 60.0f;  // diameter of the pick up button
    float slotSize         = 60.0f;  // diameter of each skill slot
    float arcRadius        = 180.0f; // distance from skill arc center to slot centers
    int   slotCount        = 5;
    float startAngleDeg    = 290.0f; // arc start angle in degrees (0=right, CCW)
    float endAngleDeg      = 190.0f; // arc end angle in degrees
    Vec2  skillArcOffset   = {0.0f, 0.0f}; // offset of skill arc center from widget center

    // Individual button positions (pixels, relative to widget center, scaled by layoutScale_)
    Vec2 attackOffset = {0.0f, 120.0f};  // offset from widget center
    Vec2 pickUpOffset = {-50.0f, 40.0f}; // offset from arc origin (widget center)

    // SlotArc — the page selector (1,2,3,4) follows its own C-arc
    float slotArcRadius    = 50.0f;   // distance from slot arc center to page dots
    float slotArcStartDeg  = 290.0f;  // start angle (0=right, CCW)
    float slotArcEndDeg    = 190.0f;  // end angle
    Vec2  slotArcOffset    = {0.0f, -40.0f}; // offset from arc origin (widget center)

    std::vector<SkillSlotData> slots;

    UIClickCallback onAttack;
    UIClickCallback onPickUp;
    std::function<void(int slotIndex)> onSkillSlot;

    // Skill activation callback (game_app resolves skill from global slot index)
    std::function<void(const std::string& skillId, int rank)> onSkillActivated;

    // Page management (replaces SkillBarUI singleton)
    int currentPage = 0;
    static constexpr int SLOTS_PER_PAGE = 5;
    static constexpr int TOTAL_PAGES = 4;

    void nextPage();
    void prevPage();
    void setPage(int page);
};

} // namespace fate
