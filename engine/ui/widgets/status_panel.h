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

    // Font sizes (unscaled)
    float titleFontSize = 16.0f;
    float nameFontSize = 15.0f;
    float levelFontSize = 11.0f;
    float statLabelFontSize = 9.0f;
    float statValueFontSize = 11.0f;
    float factionFontSize = 9.0f;

    // Colors
    Color titleColor = {0.25f, 0.16f, 0.08f, 1.0f};
    Color nameColor = {0.25f, 0.16f, 0.08f, 1.0f};
    Color levelColor = {0.40f, 0.28f, 0.16f, 1.0f};
    Color statLabelColor = {0.50f, 0.40f, 0.30f, 1.0f};
    Color factionColor = {1.0f, 0.92f, 0.75f, 1.0f};

    UIClickCallback onClose;

private:
    void renderCharacterDisplay(SpriteBatch& batch, SDFText& sdf, const Rect& area, float depth);
    void renderStatGrid(SpriteBatch& batch, SDFText& sdf, const Rect& area, float depth);
};

} // namespace fate
