#pragma once
#include "engine/ecs/entity.h"
#include <cstdint>
#include <functional>

namespace fate {

class DeathOverlayUI {
public:
    std::function<void(uint8_t respawnType)> onRespawnRequested;
    bool respawnPending = false;

    void onDeath(int32_t xpLost, int32_t honorLost, float respawnTimer);
    void render(Entity* player);

private:
    bool active_ = false;
    float countdown_ = 0.0f;
    int32_t xpLost_ = 0;
    int32_t honorLost_ = 0;
};

} // namespace fate
