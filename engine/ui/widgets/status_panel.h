#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"

namespace fate {

class StatusPanel : public UINode {
public:
    StatusPanel(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;

    // Character info
    std::string playerName;
    std::string className;
    std::string factionName;
    int level = 1;
    float xp = 0;
    float xpToLevel = 1;

    // Stats (3x3 grid — 9 stats)
    int str = 0, intl = 0, dex = 0;
    int con = 0, wis = 0, arm = 0;
    int hit = 0, cri = 0, spd = 0;

    UIClickCallback onClose;

private:
    void renderCharacterDisplay(SpriteBatch& batch, SDFText& sdf, const Rect& area, float depth);
    void renderStatGrid(SpriteBatch& batch, SDFText& sdf, const Rect& area, float depth);
};

} // namespace fate
