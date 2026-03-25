// engine/ui/widgets/fate_status_bar.h
#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include <string>
#include <vector>

namespace fate {

class FateStatusBar : public UINode {
public:
    FateStatusBar(const std::string& id);
    void render(SpriteBatch& batch, SDFText& sdf) override;
    bool onPress(const Vec2& localPos) override;
    bool hitTest(const Vec2& point) const override;

    // Player stats (bound from game_app)
    float hp = 0.0f, maxHp = 1.0f;
    float mp = 0.0f, maxMp = 1.0f;
    float xp = 0.0f, xpToLevel = 1.0f;
    int level = 1;
    std::string playerName;
    int playerTileX = 0, playerTileY = 0;

    // Layout (reference pixels, scaled by layoutScale_)
    float topBarHeight   = 40.0f;
    float portraitRadius = 20.0f;
    float barHeight      = 22.0f;  // HP/MP bar height
    float menuBtnSize    = 21.0f;  // Menu button radius
    float chatBtnSize    = 21.0f;  // Chat button radius
    float chatBtnOffsetX = 8.0f;   // from right edge
    float menuBtnGap     = 6.0f;   // gap below EXP circle
    float coordOffsetY   = 3.0f;   // below bar strip

    // Font sizes (base, before layoutScale_)
    float levelFontSize  = 26.0f;
    float labelFontSize  = 22.0f;  // "HP"/"MP" labels
    float numberFontSize = 28.0f;  // HP/MP values
    float coordFontSize  = 11.0f;
    float buttonFontSize = 9.0f;   // Menu/Chat button text

    // Colors
    Color hpBarColor = {0.9f, 0.55f, 0.1f, 1.0f};
    Color mpBarColor = {0.2f, 0.5f, 0.9f, 1.0f};
    Color coordColor = {1.0f, 1.0f, 1.0f, 0.8f};

    // Visibility toggles
    bool showCoordinates = true;
    bool showMenuButton  = true;
    bool showChatButton  = true;

    // Menu state
    bool menuOpen = false;
    std::vector<std::string> menuItems;

    // Callbacks
    UIClickCallback onMenuItemSelected; // nodeId = item label (e.g. "Event")
    UIClickCallback onChatButtonPressed;

private:
    void renderTopBar(SpriteBatch& batch, SDFText& sdf, float d, float s);
    void renderMenuButton(SpriteBatch& batch, SDFText& sdf, float d, float s);
    void renderChatButton(SpriteBatch& batch, SDFText& sdf, float d, float s);
    void renderMenuOverlay(SpriteBatch& batch, SDFText& sdf, float d, float s);

    // Hit-test region caches (computed during render, used by onPress)
    Vec2 menuBtnCenter_ = {0, 0};
    float menuBtnRadius_ = 0.0f;
    Vec2 chatBtnCenter_ = {0, 0};
    float chatBtnRadius_ = 0.0f;
    Rect menuOverlayRect_ = {0, 0, 0, 0};
    float menuItemHeight_ = 0.0f;
};

} // namespace fate
