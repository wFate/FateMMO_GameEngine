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
#include "server/db/pvp_kill_log_repository.h"
#include "server/db/definition_caches.h"
#include "server/rate_limiter.h"
#include "server/wal/write_ahead_log.h"
#include "server/db/persistence_priority.h"
#include "server/db/player_dirty_flags.h"
#include "server/player_lock.h"
#include "server/nonce_manager.h"
#include "game/shared/gauntlet.h"
#include "game/shared/battlefield_manager.h"
#include "game/shared/arena_manager.h"
#include "game/shared/event_scheduler.h"
#include "game/shared/faction.h"
#include "game/shared/ranking_system.h"
#include "server/cache/item_definition_cache.h"
#include "server/cache/loot_table_cache.h"
#include "server/cache/recipe_cache.h"
#include "server/cache/pet_definition_cache.h"
#include "server/cache/collection_cache.h"
#include "server/cache/costume_cache.h"
#include "server/db/collection_repository.h"
#include "server/db/costume_repository.h"
#include "server/db/spawn_zone_cache.h"
#include "game/shared/collection_system.h"
#include "server/server_spawn_manager.h"
#include "server/dungeon_manager.h"
#include "engine/spatial/collision_grid.h"
#include "engine/net/auth_protocol.h"
#include "server/gm_commands.h"
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <memory>
#include <string>

namespace fate {

struct CharacterStatsComponent;
struct EnemyStatsComponent;

class ServerApp {
public:
    bool init(uint16_t port = 7777);
    void run();
    void shutdown();
    void requestShutdown() { running_ = false; }

private:
    static constexpr float TICK_RATE = 20.0f;
    static constexpr float TICK_INTERVAL = 1.0f / TICK_RATE;
    static constexpr int   MAX_CATCHUP_TICKS = 3;

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
    int autoSavesInFlight_ = 0; // tracks async auto-saves; WAL truncates only when 0
    bool walNeedsTruncate_ = false; // set true when saves complete; cleared after truncate

    // Priority-based DB flush queue
    PersistenceQueue persistQueue_;

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
    std::unique_ptr<PvPKillLogRepository> pvpKillLogRepo_;
    std::unique_ptr<CollectionRepository> collectionRepo_;
    std::unique_ptr<CostumeRepository> costumeRepo_;

    // Definition caches (read-only, loaded at startup)
    ItemDefinitionCache itemDefCache_;
    LootTableCache lootTableCache_;
    MobDefCache mobDefCache_;
    SkillDefCache skillDefCache_;
    SceneCache sceneCache_;
    RecipeCache recipeCache_;
    PetDefinitionCache petDefCache_;
    CollectionCache collectionCache_;
    CostumeCache costumeCache_;

    // Gauntlet event system
    GauntletManager gauntletManager_;

    // Battlefield event system
    BattlefieldManager battlefieldManager_;
    EventScheduler eventScheduler_;

    // Ranking cache
    RankingManager rankingMgr_;

    // Arena matchmaking system
    ArenaManager arenaManager_;
    uint32_t arenaTickCounter_ = 0;

    // Spawn system
    SpawnZoneCache spawnZoneCache_;
    ServerSpawnManager spawnManager_;

    // GM command system
    GMCommandRegistry gmCommands_;
    std::unordered_map<uint16_t, AdminRole> clientAdminRoles_; // clientId -> admin_role

    // Mute system (in-memory, timed)
    struct MuteInfo {
        std::chrono::steady_clock::time_point expiresAt;
        std::string reason;
    };
    std::unordered_map<uint16_t, MuteInfo> clientMutes_;

    // GM tools: invisible and god mode (PersistentId values)
    std::unordered_set<uint64_t> invisibleEntities_;
    std::unordered_set<uint64_t> godModeEntities_;

    // Instanced dungeons
    DungeonManager dungeonManager_;

    // Portal definitions loaded from scene JSON files (for proximity validation)
    struct ServerPortal {
        std::string sourceScene;
        std::string targetScene;
        float x, y;         // portal center in source scene (pixels)
        float halfW, halfH; // half trigger size
        float targetSpawnX = 0.0f; // spawn position in target scene
        float targetSpawnY = 0.0f;
    };
    std::vector<ServerPortal> portals_;
    void loadPortalsFromScenes();

    // Per-scene collision grids loaded from scene JSON (tile-based blocking)
    std::unordered_map<std::string, CollisionGrid> collisionGrids_;
    void loadCollisionGridsFromScenes();

    // Session tracking
    std::unordered_map<AuthToken, PendingSession, AuthTokenHash> pendingSessions_;
    std::unordered_map<int, uint16_t> activeAccountSessions_; // account_id -> clientId

    // Per-client movement tracking
    std::unordered_map<uint16_t, Vec2>  lastValidPositions_;
    std::unordered_map<uint16_t, float> lastMoveTime_;
    std::unordered_map<uint16_t, int>   moveCountThisTick_;
    std::unordered_map<uint16_t, int>   skillCommandsThisTick_;
    std::unordered_set<uint16_t> needsFirstMoveSync_;  // accept first CmdMove unconditionally

    // Per-client dirty tracking (skip DB writes when nothing changed)
    std::unordered_map<uint16_t, PlayerDirtyFlags> playerDirty_;

    // Deduplication for persistence queue
    std::unordered_map<uint64_t, float> pendingPersist_;

    // Per-client auto-save tracking (staggered)
    std::unordered_map<uint16_t, float> nextAutoSaveTime_;

    // Per-client auto-attack cooldown tracking
    std::unordered_map<uint16_t, float> lastAutoAttackTime_;

    // Per-client token bucket rate limiters
    std::unordered_map<uint16_t, ClientRateLimiter> rateLimiters_;
    std::unordered_map<int, ClientRateLimiter> accountRateLimiters_;

    // Per-client skill cooldown tracking: clientId -> skillId -> last cast gameTime
    std::unordered_map<uint16_t, std::unordered_map<std::string, float>> skillCooldowns_;

    // Server-side HP/MP regen timers
    float regenTimer_ = 0.0f;
    float mpRegenTimer_ = 0.0f;
    float petAutoLootTimer_ = 0.0f;

    // One-time nonces for economic actions (trade/market replay prevention)
    NonceManager nonceManager_;

    // Per-player mutex for serializing game-thread mutations vs async fiber DB saves
    PlayerLockMap playerLocks_;

    // Per-player event lock: prevents double-enrollment across Battlefield/Arena events
    std::unordered_map<uint32_t, std::string> playerEventLocks_; // entityId -> eventType
    std::unordered_map<int, float> dungeonDeclineCooldowns_;  // partyId -> cooldown remaining (seconds)

    // Periodic maintenance timers
    float bossTickTimer_ = 0.0f;
    float marketExpiryTimer_ = 0.0f;
    float bountyExpiryTimer_ = 0.0f;
    float tradeCleanupTimer_ = 0.0f;
    float pvpKillLogPruneTimer_ = 0.0f;

    // Aurora rotation state
    Faction lastFavoredFaction_ = Faction::None;
    float auroraTickTimer_ = 0.0f;

    // Cast-completion re-entry guard (skip castTime check when executing after cast completes)
    bool castCompleting_ = false;

    void tick(float dt);
    void onClientConnected(uint16_t clientId);
    void onClientDisconnected(uint16_t clientId);
    void onPacketReceived(uint16_t clientId, uint8_t type, ByteReader& payload);
    bool validatePayload(ByteReader& payload, uint16_t clientId, uint8_t type);

    void processAction(uint16_t clientId, const CmdAction& action);
    void processUseSkill(uint16_t clientId, const CmdUseSkillMsg& msg);

    // handlers/movement_handler.cpp
    void processMove(uint16_t clientId, const CmdMove& move);

    // handlers/chat_handler.cpp
    void processChat(uint16_t clientId, const CmdChat& chat);

    // handlers/market_handler.cpp
    void processMarket(uint16_t clientId, ByteReader& payload);

    // handlers/bounty_handler.cpp
    void processBounty(uint16_t clientId, ByteReader& payload);

    // handlers/guild_handler.cpp
    void processGuild(uint16_t clientId, ByteReader& payload);

    // handlers/social_handler.cpp
    void processSocial(uint16_t clientId, ByteReader& payload);

    // handlers/trade_handler.cpp
    void processTrade(uint16_t clientId, ByteReader& payload);

    // handlers/quest_handler.cpp
    void processQuestAction(uint16_t clientId, ByteReader& payload);

    // handlers/zone_transition_handler.cpp
    void processZoneTransition(uint16_t clientId, const CmdZoneTransition& cmd);

    // handlers/respawn_handler.cpp
    void processRespawn(uint16_t clientId, const CmdRespawnMsg& msg);

    // handlers/mob_death_handler.cpp
    void processMobDeath(uint16_t killerClientId, CharacterStatsComponent* killerStats,
                         EnemyStatsComponent* mobStats, Vec2 deathPos,
                         World& world, ReplicationManager& repl);

    // handlers/pk_handler.cpp
    void processPKAttack(uint16_t attackerClientId, uint16_t targetClientId, int damage);
    void processPKKill(uint16_t killerClientId, uint16_t victimClientId);

    void processEquip(uint16_t clientId, const CmdEquipMsg& msg);
    void processMoveItem(uint16_t clientId, const CmdMoveItemMsg& msg);
    void processDestroyItem(uint16_t clientId, const CmdDestroyItemMsg& msg);
    void processActivateSkillRank(uint16_t clientId, const CmdActivateSkillRankMsg& msg);
    void processAssignSkillSlot(uint16_t clientId, const CmdAssignSkillSlotMsg& msg);

    // handlers/stat_allocation_handler.cpp
    void processAllocateStat(uint16_t clientId, const CmdAllocateStatMsg& msg);
    void processEnchant(uint16_t clientId, const CmdEnchantMsg& msg);
    void processRepair(uint16_t clientId, const CmdRepairMsg& msg);
    void processExtractCore(uint16_t clientId, const CmdExtractCoreMsg& msg);
    void processCraft(uint16_t clientId, const CmdCraftMsg& msg);
    void recalcEquipmentBonuses(Entity* player);
    void sendPlayerState(uint16_t clientId);
    void sendSkillSync(uint16_t clientId);
    void sendSkillDefs(uint16_t clientId, const std::string& className);
    void sendQuestSync(uint16_t clientId);
    void sendInventorySync(uint16_t clientId);
    void consumePendingSessions();
    void savePlayerToDB(uint16_t clientId, bool forceSaveAll = true);
    void savePlayerToDBAsync(uint16_t clientId, bool forceSaveAll = true);
    void saveInventoryForClient(uint16_t clientId);
    void tickAutoSave(float dt);
    void tickPersistQueue();
    void enqueuePersist(uint16_t clientId, PersistPriority priority, PersistType type);
    void tickMaintenance(float dt);
    void initGauntlet();
    void processGauntletCommand(uint16_t clientId, ByteReader& payload);
    void processBattlefield(uint16_t clientId, const CmdBattlefieldMsg& msg);
    void processArena(uint16_t clientId, const CmdArenaMsg& msg);
    void processPetCommand(uint16_t clientId, const CmdPetMsg& msg);
    void sendPetUpdate(uint16_t clientId, Entity* player);
    void tickPetAutoLoot(float dt);
    void processShopBuy(uint16_t clientId, const CmdShopBuyMsg& msg);
    void processShopSell(uint16_t clientId, const CmdShopSellMsg& msg);
    void processBankDepositItem(uint16_t clientId, const CmdBankDepositItemMsg& msg);
    void processBankWithdrawItem(uint16_t clientId, const CmdBankWithdrawItemMsg& msg);
    void processBankDepositGold(uint16_t clientId, const CmdBankDepositGoldMsg& msg);
    void processBankWithdrawGold(uint16_t clientId, const CmdBankWithdrawGoldMsg& msg);
    void processTeleport(uint16_t clientId, const CmdTeleportMsg& msg);
    void processSocketItem(uint16_t clientId, const CmdSocketItemMsg& msg);
    void processStatEnchant(uint16_t clientId, const CmdStatEnchantMsg& msg);
    void processUseConsumable(uint16_t clientId, const CmdUseConsumableMsg& msg);
    void processRankingQuery(uint16_t clientId, const CmdRankingQueryMsg& msg);
    void broadcastBossKillNotification(const EnemyStats& es,
                                       const EnemyStats::LootOwnerResult& lootResult,
                                       const std::string& scene);
    void tickDungeonInstances(float dt);
    void tickAuroraRotation(float dt);
    void applyAuroraBuffs(uint16_t clientId, Entity* player);
    void removeAuroraBuffs(Entity* player);
    void sendAuroraStatus(uint16_t clientId);
    bool isAuroraScene(const std::string& sceneId) const;
    void distributeDungeonRewards(DungeonInstance* inst);
    void endDungeonInstance(uint32_t instanceId, uint8_t reason);
    void initGMCommands();
    uint16_t findClientByCharacterName(const std::string& name);

    // handlers/collection_handler.cpp
    void checkPlayerCollections(uint16_t clientId, const std::string& triggerType);
    void sendCollectionSync(uint16_t clientId);
    void sendCollectionDefs(uint16_t clientId);

    // handlers/costume_handler.cpp
    void processEquipCostume(uint16_t clientId, const CmdEquipCostumeMsg& msg);
    void processUnequipCostume(uint16_t clientId, const CmdUnequipCostumeMsg& msg);
    void processToggleCostumes(uint16_t clientId, const CmdToggleCostumesMsg& msg);
    void sendCostumeDefs(uint16_t clientId);
    void sendCostumeSync(uint16_t clientId);
    void loadPlayerCostumes(uint16_t clientId, const std::string& characterId);

    // Dungeon instance routing: returns the correct World/ReplicationManager
    // for a client (dungeon instance world if in dungeon, otherwise main world_).
    World& getWorldForClient(uint16_t clientId);
    ReplicationManager& getReplicationForClient(uint16_t clientId);

    // Transfer a player entity from one World to another (dungeon entry/exit).
    // Snapshots all component data, destroys the source entity, creates a new
    // entity in the destination World at spawnPos, and re-registers replication.
    // Returns the new entity's handle (null on failure).
    EntityHandle transferPlayerToWorld(uint16_t clientId,
                                       World& srcWorld, ReplicationManager& srcRepl,
                                       World& dstWorld, ReplicationManager& dstRepl,
                                       Vec2 spawnPos, const std::string& newScene);

    // Dungeon entry flow
    void processStartDungeon(uint16_t clientId, const CmdStartDungeonMsg& msg);
    void processDungeonResponse(uint16_t clientId, const CmdDungeonResponseMsg& msg);
    void startDungeonInstance(DungeonInstance* inst);
    bool checkDungeonTicket(const std::string& characterId);
    void consumeDungeonTicket(const std::string& characterId);
    void spawnDungeonMobs(DungeonInstance* inst);
};

} // namespace fate
