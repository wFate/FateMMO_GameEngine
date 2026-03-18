#pragma once
#include "engine/net/net_server.h"
#include "engine/net/replication.h"
#include "engine/net/protocol.h"
#include "engine/ecs/world.h"
#include <cstdint>
#include <unordered_map>

namespace fate {

class ServerApp {
public:
    bool init(uint16_t port = 7777);
    void run();
    void shutdown();

private:
    static constexpr float TICK_RATE = 20.0f;
    static constexpr float TICK_INTERVAL = 1.0f / TICK_RATE;

    // Movement validation constants
    static constexpr float MAX_MOVE_SPEED = 160.0f;         // px/sec
    static constexpr float RUBBER_BAND_THRESHOLD = 200.0f;  // px
    static constexpr int   MAX_MOVES_PER_SEC = 30;

    World world_;
    NetServer server_;
    ReplicationManager replication_;
    float gameTime_ = 0.0f;
    bool running_ = false;

    // Per-client movement tracking
    std::unordered_map<uint16_t, Vec2>  lastValidPositions_;
    std::unordered_map<uint16_t, float> lastMoveTime_;
    std::unordered_map<uint16_t, int>   moveCountThisTick_;

    void tick(float dt);
    void onClientConnected(uint16_t clientId);
    void onClientDisconnected(uint16_t clientId);
    void onPacketReceived(uint16_t clientId, uint8_t type, ByteReader& payload);

    void processAction(uint16_t clientId, const CmdAction& action);
    void sendPlayerState(uint16_t clientId);
};

} // namespace fate
