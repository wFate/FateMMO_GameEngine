#pragma once
#include "engine/net/net_server.h"
#include "engine/ecs/world.h"
#include <cstdint>

namespace fate {

class ServerApp {
public:
    bool init(uint16_t port = 7777);
    void run();
    void shutdown();

private:
    static constexpr float TICK_RATE = 20.0f;
    static constexpr float TICK_INTERVAL = 1.0f / TICK_RATE;

    World world_;
    NetServer server_;
    float gameTime_ = 0.0f;
    bool running_ = false;

    void tick(float dt);
    void onClientConnected(uint16_t clientId);
    void onClientDisconnected(uint16_t clientId);
    void onPacketReceived(uint16_t clientId, uint8_t type, ByteReader& payload);
};

} // namespace fate
