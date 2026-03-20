#pragma once
#include <unordered_set>
#include "engine/net/net_server.h"
#include "engine/net/replication.h"
#include "engine/net/protocol.h"
#include "engine/net/game_messages.h"
#include "engine/ecs/world.h"
#include "server/auth/auth_server.h"
#include "server/db/db_connection.h"
#include "server/db/db_pool.h"
#include "server/db/db_dispatcher.h"
#include "server/db/account_repository.h"
#include "server/db/character_repository.h"
#include "server/db/inventory_repository.h"
#include "server/db/skill_repository.h"
#include "server/db/guild_repository.h"
#include "server/db/social_repository.h"
#include "server/db/market_repository.h"
#include "server/db/trade_repository.h"
#include "server/db/bounty_repository.h"
#include "server/db/quest_repository.h"
#include "server/db/bank_repository.h"
#include "server/db/pet_repository.h"
#include "server/db/zone_mob_state_repository.h"
#include "server/db/definition_caches.h"
#include "game/shared/gauntlet.h"
#include "server/cache/item_definition_cache.h"
#include "server/cache/loot_table_cache.h"
#include "server/db/spawn_zone_cache.h"
#include "server/server_spawn_manager.h"
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

    // Auto-save interval (staggered per-player)
    static constexpr float AUTO_SAVE_INTERVAL = 300.0f;     // 5 minutes
    static constexpr float MARKET_EXPIRY_INTERVAL = 60.0f;  // check every minute
    static constexpr float BOUNTY_EXPIRY_INTERVAL = 60.0f;  // check every minute
    static constexpr float TRADE_CLEANUP_INTERVAL = 30.0f;  // check every 30s

    World world_;
    NetServer server_;
    ReplicationManager replication_;
    float gameTime_ = 0.0f;
    bool running_ = false;

    // Auth
    AuthServer authServer_;
    uint16_t authPort_ = 7778;
    std::string dbConnectionString_;
    std::string tlsCertPath_ = "config/server.crt";
    std::string tlsKeyPath_ = "config/server.key";

    // DB: legacy single connection (for repos that run on game thread)
    DbConnection gameDbConn_;

    // DB: connection pool + async dispatcher (for fiber-offloaded work)
    DbPool dbPool_;
    DbDispatcher dbDispatcher_;

    // Repositories (all use gameDbConn_ for now; will migrate to pool+dispatcher)
    std::unique_ptr<CharacterRepository> characterRepo_;
    std::unique_ptr<InventoryRepository> inventoryRepo_;
    std::unique_ptr<SkillRepository> skillRepo_;
    std::unique_ptr<GuildRepository> guildRepo_;
    std::unique_ptr<SocialRepository> socialRepo_;
    std::unique_ptr<MarketRepository> marketRepo_;
    std::unique_ptr<TradeRepository> tradeRepo_;
    std::unique_ptr<BountyRepository> bountyRepo_;
    std::unique_ptr<QuestRepository> questRepo_;
    std::unique_ptr<BankRepository> bankRepo_;
    std::unique_ptr<PetRepository> petRepo_;
    std::unique_ptr<ZoneMobStateRepository> mobStateRepo_;

    // Definition caches (read-only, loaded at startup)
    ItemDefinitionCache itemDefCache_;
    LootTableCache lootTableCache_;
    MobDefCache mobDefCache_;
    SkillDefCache skillDefCache_;
    SceneCache sceneCache_;

    // Gauntlet event system
    GauntletManager gauntletManager_;

    // Spawn system
    SpawnZoneCache spawnZoneCache_;
    ServerSpawnManager spawnManager_;

    // Session tracking
    std::unordered_map<AuthToken, PendingSession, AuthTokenHash> pendingSessions_;
    std::unordered_map<int, uint16_t> activeAccountSessions_; // account_id -> clientId

    // Per-client movement tracking
    std::unordered_map<uint16_t, Vec2>  lastValidPositions_;
    std::unordered_map<uint16_t, float> lastMoveTime_;
    std::unordered_map<uint16_t, int>   moveCountThisTick_;
    std::unordered_set<uint16_t> needsFirstMoveSync_;  // accept first CmdMove unconditionally

    // Per-client auto-save tracking (staggered)
    std::unordered_map<uint16_t, float> nextAutoSaveTime_;

    // Periodic maintenance timers
    float bossTickTimer_ = 0.0f;
    float marketExpiryTimer_ = 0.0f;
    float bountyExpiryTimer_ = 0.0f;
    float tradeCleanupTimer_ = 0.0f;

    void tick(float dt);
    void onClientConnected(uint16_t clientId);
    void onClientDisconnected(uint16_t clientId);
    void onPacketReceived(uint16_t clientId, uint8_t type, ByteReader& payload);

    void processAction(uint16_t clientId, const CmdAction& action);
    void processUseSkill(uint16_t clientId, const CmdUseSkillMsg& msg);
    void sendPlayerState(uint16_t clientId);
    void consumePendingSessions();
    void savePlayerToDB(uint16_t clientId);
    void savePlayerToDBAsync(uint16_t clientId);
    void saveInventoryForClient(uint16_t clientId);
    void tickAutoSave(float dt);
    void tickMaintenance(float dt);
    void initGauntlet();
    void processGauntletCommand(uint16_t clientId, ByteReader& payload);
};

} // namespace fate
