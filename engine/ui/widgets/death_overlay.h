#pragma once
#include "engine/ui/ui_node.h"
#include <cstdint>
#include <functional>

namespace fate {

class DeathOverlay : public UINode {
public:
    DeathOverlay(const std::string& id);

    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;
    void onRelease(const Vec2& localPos) override;

    // State
    int32_t xpLost = 0;
    int32_t honorLost = 0;
    float countdown = 0.0f;
    bool respawnPending = false;
    uint8_t deathSource = 0;
    bool active = false;

    void onDeath(int32_t xp, int32_t honor, float timer, uint8_t source = 0);
    void update(float dt);
    void reset();

    std::function<void(uint8_t respawnType)> onRespawnRequested;

private:
    Rect townBtnRect_ = {};
    Rect spawnBtnRect_ = {};
    Rect phoenixBtnRect_ = {};
    int pressedBtn_ = -1;
};

} // namespace fate
