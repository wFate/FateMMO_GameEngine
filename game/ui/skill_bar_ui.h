#pragma once
#include "engine/ecs/world.h"
#include "engine/ecs/entity.h"
#include "engine/core/types.h"
#include "game/shared/skill_manager.h"
#include "imgui.h"
#include <string>
#include <algorithm>

namespace fate {

// Skill Bar UI — 5 slots x 4 pages (20 total), positioned on the right side
class SkillBarUI {
public:
    static SkillBarUI& instance() {
        static SkillBarUI s;
        return s;
    }

    bool isVisible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }
    void toggle() { visible_ = !visible_; }

    int currentPage() const { return currentPage_; }
    void setPage(int page) { currentPage_ = std::clamp(page, 0, TOTAL_PAGES - 1); }
    void nextPage() { currentPage_ = (currentPage_ + 1) % TOTAL_PAGES; }
    void prevPage() { currentPage_ = (currentPage_ - 1 + TOTAL_PAGES) % TOTAL_PAGES; }

    // Draw the skill bar (call every frame)
    void draw(World* world);

    static constexpr int SLOTS_PER_PAGE = 5;
    static constexpr int TOTAL_PAGES = 4;

private:
    SkillBarUI() = default;

    bool visible_ = true;  // Always visible by default
    int currentPage_ = 0;  // 0-3

    // Helpers
    SkillManager* findPlayerSkills(World* world);
    Entity* findPlayer(World* world);

    void drawSkillSlot(SkillManager* skills, int pageSlotIndex, float size);
    void drawCooldownOverlay(ImVec2 pos, ImVec2 size, float cooldownPct);
};

} // namespace fate
