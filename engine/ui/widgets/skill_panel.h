#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include <vector>
#include <functional>

namespace fate {

struct SkillInfo {
    std::string name;
    int currentLevel = 0;
    int maxLevel = 10;
    bool unlocked = false;
};

class SkillPanel : public UINode {
public:
    SkillPanel(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;

    int activeSetPage = 0;       // skill set tab (0-4)
    int remainingPoints = 0;
    std::vector<SkillInfo> classSkills;  // all learnable skills
    int selectedSkillIndex = -1;

    UIClickCallback onClose;
    std::function<void(int skillIndex)> onLevelUp;

private:
    void renderSkillWheel(SpriteBatch& batch, SDFText& sdf, const Rect& area, float depth);
    void renderSkillList(SpriteBatch& batch, SDFText& sdf, const Rect& area, float depth);
};

} // namespace fate
