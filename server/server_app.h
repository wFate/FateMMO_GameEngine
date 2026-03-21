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
#include "server/rate_limiter.h"
#include "server/wal/write_ahead_log.h"
#include "server/player_lock.h"
#include "server/nonce_manager.h"
#include "game/shared/gauntlet.h"
#include "game/shared/battlefield_manager.h"
#include "game/shared/arena_manager.h"
#include "game/shared/event_scheduler.h"
#include "server/cache/item_definition_cache.h"
#include "server/cache/loot_table_cache.h"
#include "server/cache/recipe_cache.h"
#include "server/cache/pet_definition_cache.h"
#include "server/db/spawn_zone_cache.h"
#include "server/server_spawn_manager.h"
#include "engine/net/auth_protocol.h"
#include "server/gm_commands.h"
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

    // Write-Ahead Log for crash recovery
    WriteAheadLog wal_;

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
    RecipeCache recipeCache_;
    PetDefinitionCache petDefCache_;

    // Gauntlet event system
    GauntletManager gauntletManager_;

    // Battlefield event system
    BattlefieldManager battlefieldManager_;
    EventScheduler eventScheduler_;

    // Arena matchmaking system
    ArenaManager arenaManager_;
    uint32_t arenaTickCounter_ = 0;

    // Spawn system
    SpawnZoneCache spawnZoneCache_;
    ServerSpawnManager spawnManager_;

    // GM command system
    GMCommandRegistry gmCommands_;
    std::unordered_map<uint16_t, int> clientAdminRoles_; // clientId -> admin_role

    // Session tracking
    std::unordered_map<AuthToken, PendingSession, AuthTokenHash> pendingSessions_;
    std::unordered_map<int, uint16_t> activeAccountSessions_; // account_id -> clientId

    // Per-client movement tracking
    std::unordered_map<uint16_t, Vec2>  lastValidPositions_;
    std::unordered_map<uint16_t, float> lastMoveTime_;
    std::unordered_map<uint16_t, int>   moveCountThisTick_;
    std::unordered_map<uint16_t, int>   skillCommandsThisTick_;
    std::unordered_set<uint16_t> needsFirstMoveSync_;  // accept first CmdMove unconditionally

    // Per-client auto-save tracking (staggered)
    std::unordered_map<uint16_t, float> nextAutoSaveTime_;

    // Per-client auto-attack cooldown tracking
    std::unordered_map<uint16_t, float> lastAutoAttackTime_;

    // Per-client token bucket rate limiters
    std::unordered_map<uint16_t, ClientRateLimiter> rateLimiters_;

    // Per-client skill cooldown tracking: clientId -> skillId -> last cast gameTime
    std::unordered_map<uint16_t, std::unordered_map<std::string, float>> skillCooldowns_;

    // Server-side HP/MP regen timers
    float regenTimer_ = 0.0f;
    float mpRegenTimer_ = 0.0f;

    // One-time nonces for economic actions (trade/market replay prevention)
    NonceManager nonceManager_;

    // Per-player mutex for serializing game-thread mutations vs async fiber DB saves
    PlayerLockMap playerLocks_;

    // Per-player event lock: prevents double-enrollment across Battlefield/Arena events
    std::unordered_map<uint32_t, std::string> playerEventLocks_; // entityId -> eventType

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
    void processEquip(uint16_t clientId, const CmdEquipMsg& msg);
    void processEnchant(uint16_t clientId, const CmdEnchantMsg& msg);
    void processRepair(uint16_t clientId, const CmdRepairMsg& msg);
    void processExtractCore(uint16_t clientId, const CmdExtractCoreMsg& msg);
    void processCraft(uint16_t clientId, const CmdCraftMsg& msg);
    void recalcEquipmentBonuses(Entity* player);
    void sendPlayerState(uint16_t clientId);
    void sendSkillSync(uint16_t clientId);
    void sendQuestSync(uint16_t clientId);
    void sendInventorySync(uint16_t clientId);
    void consumePendingSessions();
    void savePlayerToDB(uint16_t clientId);
    void savePlayerToDBAsync(uint16_t clientId);
    void saveInventoryForClient(uint16_t clientId);
    void tickAutoSave(float dt);
    void tickMaintenance(float dt);
    void initGauntlet();
    void processGauntletCommand(uint16_t clientId, ByteReader& payload);
    void processBattlefield(uint16_t clientId, const CmdBattlefieldMsg& msg);
    void processArena(uint16_t clientId, const CmdArenaMsg& msg);
    void processPetCommand(uint16_t clientId, const CmdPetMsg& msg);
    void sendPetUpdate(uint16_t clientId, Entity* player);
    void processBank(uint16_t clientId, const CmdBankMsg& msg);
    void processSocketItem(uint16_t clientId, const CmdSocketItemMsg& msg);
    void processStatEnchant(uint16_t clientId, const CmdStatEnchantMsg& msg);
    void processUseConsumable(uint16_t clientId, const CmdUseConsumableMsg& msg);
    void broadcastBossKillNotification(const EnemyStats& es,
                                       const EnemyStats::LootOwnerResult& lootResult,
                                       const std::string& scene);
    void initGMCommands();
    uint16_t findClientByCharacterName(const std::string& name);
};

} // namespace fate
