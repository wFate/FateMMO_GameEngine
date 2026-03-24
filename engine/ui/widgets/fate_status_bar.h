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

    // Player stats (bound from game_app)
    float hp = 0.0f, maxHp = 1.0f;
    float mp = 0.0f, maxMp = 1.0f;
    float xp = 0.0f, xpToLevel = 1.0f;
    int level = 1;
    std::string playerName;
    int playerTileX = 0, playerTileY = 0;

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
