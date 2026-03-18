#pragma once
#include "engine/net/net_server.h"
#include "engine/net/replication.h"
#include "engine/net/protocol.h"
#include "engine/ecs/world.h"
#include "server/auth/auth_server.h"
#include "server/db/db_connection.h"
#include "server/db/account_repository.h"
#include "server/db/character_repository.h"
#include "server/db/inventory_repository.h"
#include "engine/net/auth_protocol.h"
#include <cstdint>
#include <unordered_map>
#include <memory>
#include <string>

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

    // Auth & DB
    AuthServer authServer_;
    DbConnection gameDbConn_;
    std::unique_ptr<CharacterRepository> characterRepo_;
    std::unique_ptr<InventoryRepository> inventoryRepo_;
    std::unordered_map<AuthToken, PendingSession, AuthTokenHash> pendingSessions_;
    std::unordered_map<int, uint16_t> activeAccountSessions_; // account_id -> clientId
    uint16_t authPort_ = 7778;
    std::string dbConnectionString_;
    std::string tlsCertPath_ = "config/server.crt";
    std::string tlsKeyPath_ = "config/server.key";

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
    void consumePendingSessions();
    void savePlayerToDB(uint16_t clientId);
};

} // namespace fate
