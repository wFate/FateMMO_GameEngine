#include "server/server_app.h"
#include "server/target_validator.h"
#include "engine/core/logger.h"
#include "engine/scene/scene_manager.h"
#include "engine/net/protocol.h"
#include "engine/net/packet_crypto.h"
#include "engine/ecs/persistent_id.h"
#include "game/entity_factory.h"
#include "game/shared/game_types.h"
#include "game/components/game_components.h"
#include "game/components/faction_component.h"
#include "game/components/dropped_item_component.h"
#include "game/components/spawn_point_component.h"
#include "game/shared/item_stat_roller.h"
#include "game/components/pet_component.h"
#include "game/systems/mob_ai_system.h"
#include "game/shared/xp_calculator.h"
#include "game/shared/profanity_filter.h"
#include "engine/net/game_messages.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

#include <chrono>
#include <thread>
#include <cstdlib>
#include <random>

namespace fate {

bool ServerApp::init(uint16_t port) {
    if (!NetSocket::initPlatform()) {
        LOG_ERROR("Server", "Failed to init network platform");
        return false;
    }

    // Initialize AEAD crypto library (libsodium)
    PacketCrypto::initLibrary();

    if (!server_.start(port)) {
        LOG_ERROR("Server", "Failed to start on port %d", port);
        return false;
    }

    // Set callbacks
    server_.onClientConnected = [this](uint16_t id) { onClientConnected(id); };
    server_.onClientDisconnected = [this](uint16_t id) { onClientDisconnected(id); };
    server_.onPacketReceived = [this](uint16_t id, uint8_t type, ByteReader& r) {
        onPacketReceived(id, type, r);
    };

    world_.addSystem<MobAISystem>();

    // Register existing mobs for replication
    world_.forEach<Transform, EnemyStatsComponent>([&](Entity* entity, Transform*, EnemyStatsComponent*) {
        PersistentId pid = PersistentId::generate(1);
        replication_.registerEntity(entity->handle(), pid);
    });

    // DB connection
    const char* dbUrl = std::getenv("DATABASE_URL");
    if (dbUrl) {
        dbConnectionString_ = dbUrl;
    }

    if (dbConnectionString_.empty()) {
        LOG_ERROR("Server", "DATABASE_URL not set; cannot start server without DB");
        return false;
    }

    if (!gameDbConn_.connect(dbConnectionString_)) {
        LOG_ERROR("Server", "Failed to connect to game DB");
        return false;
    }

    // Initialize connection pool
    DbPool::Config poolCfg;
    poolCfg.connectionString = dbConnectionString_;
    poolCfg.minConnections = 5;
    poolCfg.maxConnections = 50;
    if (!dbPool_.initialize(poolCfg)) {
        LOG_ERROR("Server", "Failed to initialize DB connection pool");
        return false;
    }
    dbDispatcher_.init(&dbPool_);

    // Open WAL for crash recovery; replay any entries left from a previous crash
    if (!wal_.open("server_wal.bin")) {
        LOG_ERROR("Server", "Failed to open WAL file");
    } else {
        auto walEntries = wal_.readAll();
        if (!walEntries.empty()) {
            LOG_WARN("Server", "WAL recovery: %zu entries found from previous crash", walEntries.size());
            for (const auto& e : walEntries) {
                LOG_INFO("WAL", "Recovery entry: seq=%llu type=%d char=%s val=%lld",
                         e.sequence, static_cast<int>(e.type), e.characterId.c_str(), e.intValue);
            }
            wal_.truncate(); // Clear after logging
        }
    }

    // Create repositories (all use gameDbConn_ for synchronous operations)
    characterRepo_ = std::make_unique<CharacterRepository>(gameDbConn_.connection());
    inventoryRepo_ = std::make_unique<InventoryRepository>(gameDbConn_.connection());
    skillRepo_     = std::make_unique<SkillRepository>(gameDbConn_.connection());
    guildRepo_     = std::make_unique<GuildRepository>(gameDbConn_.connection());
    socialRepo_    = std::make_unique<SocialRepository>(gameDbConn_.connection());
    marketRepo_    = std::make_unique<MarketRepository>(gameDbConn_.connection());
    tradeRepo_     = std::make_unique<TradeRepository>(gameDbConn_.connection());
    bountyRepo_    = std::make_unique<BountyRepository>(gameDbConn_.connection());
    questRepo_     = std::make_unique<QuestRepository>(gameDbConn_.connection());
    bankRepo_      = std::make_unique<BankRepository>(gameDbConn_.connection());
    petRepo_       = std::make_unique<PetRepository>(gameDbConn_.connection());
    mobStateRepo_  = std::make_unique<ZoneMobStateRepository>(gameDbConn_.connection());

    // Initialize definition caches
    itemDefCache_.initialize(gameDbConn_.connection());
    lootTableCache_.initialize(gameDbConn_.connection(), itemDefCache_);
    mobDefCache_.initialize(gameDbConn_.connection());
    skillDefCache_.initialize(gameDbConn_.connection());
    sceneCache_.initialize(gameDbConn_.connection());
    if (!spawnZoneCache_.initialize(gameDbConn_.connection())) {
        LOG_WARN("Server", "Failed to load spawn zones");
    }
    LOG_INFO("Server", "Caches loaded: %zu items, %zu loot tables, %d mobs, %d skills (%d ranks), %d scenes",
             itemDefCache_.size(), lootTableCache_.tableCount(),
             mobDefCache_.count(), skillDefCache_.skillCount(), skillDefCache_.rankCount(),
             sceneCache_.count());
    LOG_INFO("Server", "Spawn zones: %d rules", spawnZoneCache_.count());

    // Initialize Gauntlet system
    initGauntlet();

    // Auth server startup (warning only — game server can run without auth in dev)
    if (!authServer_.start(authPort_, tlsCertPath_, tlsKeyPath_, dbConnectionString_)) {
        LOG_WARN("Server", "Auth server failed to start on port %d; continuing without auth", authPort_);
    }

    LOG_INFO("Server", "Started on port %d at %.0f ticks/sec", port, TICK_RATE);
    return true;
}

void ServerApp::run() {
    running_ = true;

    // Wire mob→player combat broadcast callback
    auto* mobAISys = world_.getSystem<MobAISystem>();
    if (mobAISys) {
        mobAISys->onMobAttackResolved = [this](Entity* mob, Entity* player,
            int damage, bool isCrit, bool isKill, bool isMiss)
        {
            if (!mob || !player) return;

            // Get persistent IDs via ReplicationManager
            uint64_t mobPid = replication_.getPersistentId(mob->handle()).value();
            uint64_t playerPid = replication_.getPersistentId(player->handle()).value();

            // Broadcast SvCombatEventMsg to all clients
            SvCombatEventMsg evt;
            evt.attackerId = mobPid;
            evt.targetId   = playerPid;
            evt.damage     = isMiss ? 0 : damage;
            evt.skillId    = 0;  // basic attack
            evt.isCrit     = isCrit ? 1 : 0;
            evt.isKill     = isKill ? 1 : 0;
            uint8_t buf[64];
            ByteWriter w(buf, sizeof(buf));
            evt.write(w);
            server_.broadcast(Channel::ReliableOrdered, PacketType::SvCombatEvent, buf, w.size());

            // Sync player state (HP/Fury) after mob damage — covers fury-on-hit for Warriors
            {
                auto* sc = player->getComponent<CharacterStatsComponent>();
                if (sc && sc->stats.classDef.usesFury()) {
                    uint16_t targetClientId = 0;
                    server_.connections().forEach([&](const ClientConnection& conn) {
                        if (conn.playerEntityId == playerPid) {
                            targetClientId = conn.clientId;
                        }
                    });
                    if (targetClientId != 0) {
                        sendPlayerState(targetClientId);
                    }
                }
            }

            // If player was killed, send SvDeathNotifyMsg to that player's client
            if (isKill) {
                auto* sc = player->getComponent<CharacterStatsComponent>();
                int32_t xpLost = 0;
                if (sc) {
                    // Calculate XP that was lost (die() already applied the loss)
                    float lossPct = CharacterStats::getXPLossPercent(sc->stats.level);
                    // Approximate: lossPct * XP before death (currentXP is already reduced)
                    xpLost = static_cast<int32_t>(sc->stats.currentXP * lossPct / (1.0f - lossPct));
                }

                // Find which client owns this player entity
                uint16_t targetClientId = 0;
                server_.connections().forEach([&](const ClientConnection& conn) {
                    if (conn.playerEntityId == playerPid) {
                        targetClientId = conn.clientId;
                    }
                });
                if (targetClientId != 0) {
                    SvDeathNotifyMsg deathMsg;
                    deathMsg.deathSource = 0;  // PvE
                    deathMsg.respawnTimer = 5.0f;
                    deathMsg.xpLost = xpLost;
                    deathMsg.honorLost = 0;
                    uint8_t dbuf[32];
                    ByteWriter dw(dbuf, sizeof(dbuf));
                    deathMsg.write(dw);
                    server_.sendTo(targetClientId, Channel::ReliableOrdered,
                                   PacketType::SvDeathNotify, dbuf, dw.size());
                    LOG_INFO("Server", "Mob killed player '%s' (client %d)",
                             sc ? sc->stats.characterName.c_str() : "?", targetClientId);
                }
            }
        };
    }

    // Spawn server-side mobs from DB spawn zones
    spawnManager_.initialize("WhisperingWoods", world_, replication_, spawnZoneCache_, mobDefCache_);

    auto lastTick = std::chrono::high_resolution_clock::now();

#ifdef _WIN32
    timeBeginPeriod(1);
#endif

    while (running_) {
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastTick).count();

        if (elapsed >= TICK_INTERVAL) {
            lastTick = now;
            gameTime_ += TICK_INTERVAL;
            tick(TICK_INTERVAL);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

void ServerApp::shutdown() {
    // Save all connected players before stopping
    server_.connections().forEach([this](ClientConnection& c) {
        savePlayerToDB(c.clientId);
    });

    wal_.close();
    authServer_.stop();
    dbPool_.shutdown();
    gameDbConn_.disconnect();

    server_.stop();
    NetSocket::shutdownPlatform();
    LOG_INFO("Server", "Shutdown complete");
}

void ServerApp::tick(float dt) {
    // Reset per-tick move counters
    for (auto& [id, count] : moveCountThisTick_) count = 0;
    for (auto& [id, count] : skillCommandsThisTick_) count = 0;

    // Advance DB circuit breaker time so cooldowns are evaluated correctly
    dbPool_.updateBreakerTime(static_cast<double>(gameTime_));

    // 1. Drain auth results first — pending sessions must be stored BEFORE
    //    poll() processes Connect packets, otherwise the token lookup fails.
    consumePendingSessions();

    // 2. Drain incoming packets (Connect packets match pending sessions)
    server_.poll(gameTime_);

    // 3. World update (systems)
    world_.update(dt);
    spawnManager_.tick(dt, gameTime_, world_, replication_);

    // 3b. Tick status effects (DoTs, buffs/debuffs) and crowd control for all entities
    world_.forEach<StatusEffectComponent>([&](Entity* entity, StatusEffectComponent* seComp) {
        seComp->effects.tick(dt);
    });
    world_.forEach<CrowdControlComponent>([&](Entity*, CrowdControlComponent* ccComp) {
        ccComp->cc.tick(dt);
    });

    // 3c. Tick player timers (PK decay, combat timer, respawn invuln)
    world_.forEach<CharacterStatsComponent>([&](Entity*, CharacterStatsComponent* cs) {
        cs->stats.tickTimers(dt);
    });

    // 3d. Process Dying → Dead transitions (two-tick death lifecycle)
    world_.forEach<CharacterStatsComponent>([](Entity*, CharacterStatsComponent* cs) {
        cs->stats.advanceDeathTick();
    });

    // Despawn expired ground items
    {
        std::vector<EntityHandle> toDestroy;
        world_.forEach<DroppedItemComponent>([&](Entity* e, DroppedItemComponent* drop) {
            if (gameTime_ - drop->spawnTime > drop->despawnAfter) {
                toDestroy.push_back(e->handle());
            }
        });
        for (auto handle : toDestroy) {
            replication_.unregisterEntity(handle);
            world_.destroyEntity(handle);
        }
    }

    // Boss spawn tick (0.25s interval)
    bossTickTimer_ += dt;
    if (bossTickTimer_ >= 0.25f) {
        bossTickTimer_ = 0.0f;

        std::vector<EntityHandle> bossZoneHandles;
        world_.forEach<Transform, BossSpawnPointComponent>(
            [&](Entity* e, Transform*, BossSpawnPointComponent*) {
                bossZoneHandles.push_back(e->handle());
            }
        );

        for (auto handle : bossZoneHandles) {
            Entity* zoneEntity = world_.getEntity(handle);
            if (!zoneEntity) continue;
            auto* bossComp = zoneEntity->getComponent<BossSpawnPointComponent>();
            auto* zoneT = zoneEntity->getComponent<Transform>();
            if (!bossComp || !zoneT) continue;

            // Initialize: load persisted death state
            if (!bossComp->initialized) {
                bossComp->initialized = true;
                auto* sc = SceneManager::instance().currentScene();
                std::string sceneName = sc ? sc->name() : "unknown";
                auto deaths = mobStateRepo_->loadZoneDeaths(sceneName, bossComp->bossDefId);
                for (const auto& death : deaths) {
                    if (!death.hasRespawned()) {
                        bossComp->respawnAt = gameTime_ + death.getRemainingRespawnTime();
                        bossComp->bossAlive = false;
                    }
                }
                // Spawn boss if no pending respawn
                if (bossComp->respawnAt <= 0.0f && !bossComp->spawnCoordinates.empty()) {
                    thread_local std::mt19937 bossRng{std::random_device{}()};
                    std::uniform_int_distribution<int> coordDist(
                        0, static_cast<int>(bossComp->spawnCoordinates.size()) - 1);
                    int idx = coordDist(bossRng);
                    bossComp->lastSpawnIndex = idx;
                    Vec2 spawnPos = bossComp->spawnCoordinates[idx];

                    Entity* boss = EntityFactory::createMob(
                        world_, bossComp->bossDefId, bossComp->levelOverride > 0 ? bossComp->levelOverride : 1,
                        500, 50, spawnPos, true, true);
                    bossComp->bossEntityHandle = boss->handle();
                    bossComp->bossAlive = true;

                    PersistentId pid = PersistentId::generate(1);
                    replication_.registerEntity(boss->handle(), pid);
                }
                continue;
            }

            // Detect death
            if (bossComp->bossAlive && bossComp->bossEntityHandle) {
                Entity* boss = world_.getEntity(bossComp->bossEntityHandle);
                if (boss) {
                    auto* es = boss->getComponent<EnemyStatsComponent>();
                    if (es && !es->stats.isAlive) {
                        bossComp->bossAlive = false;
                        bossComp->respawnAt = gameTime_ + 300.0f;

                        auto* sc = SceneManager::instance().currentScene();
                        std::string sceneName = sc ? sc->name() : "unknown";
                        DeadMobRecord rec;
                        rec.enemyId = bossComp->bossDefId;
                        rec.mobIndex = 0;
                        rec.diedAtUnix = static_cast<int64_t>(std::time(nullptr));
                        rec.respawnSeconds = 300;
                        mobStateRepo_->saveZoneDeaths(sceneName, bossComp->bossDefId, {rec});
                    }
                }
            }

            // Process respawn
            if (!bossComp->bossAlive && bossComp->respawnAt > 0.0f && gameTime_ >= bossComp->respawnAt) {
                bossComp->respawnAt = 0.0f;

                if (!bossComp->spawnCoordinates.empty()) {
                    thread_local std::mt19937 bossRng{std::random_device{}()};
                    int idx = 0;
                    if (bossComp->spawnCoordinates.size() > 1) {
                        do {
                            std::uniform_int_distribution<int> coordDist(
                                0, static_cast<int>(bossComp->spawnCoordinates.size()) - 1);
                            idx = coordDist(bossRng);
                        } while (idx == bossComp->lastSpawnIndex);
                    }
                    bossComp->lastSpawnIndex = idx;
                    Vec2 spawnPos = bossComp->spawnCoordinates[idx];

                    Entity* boss = EntityFactory::createMob(
                        world_, bossComp->bossDefId, bossComp->levelOverride > 0 ? bossComp->levelOverride : 1,
                        500, 50, spawnPos, true, true);
                    bossComp->bossEntityHandle = boss->handle();
                    bossComp->bossAlive = true;

                    PersistentId pid = PersistentId::generate(1);
                    replication_.registerEntity(boss->handle(), pid);

                    auto* sc = SceneManager::instance().currentScene();
                    std::string sceneName = sc ? sc->name() : "unknown";
                    mobStateRepo_->clearZoneDeaths(sceneName, bossComp->bossDefId);
                }
            }
        }
    }

    // 4. Replicate entity state to connected clients
    replication_.update(world_, server_);

    // 5. Retransmit unacked reliable packets
    server_.processRetransmits(gameTime_);

    // 5b. Drain async DB completions
    dbDispatcher_.drainCompletions();

    // 5c. Gauntlet event cycle
    gauntletManager_.tick(gameTime_);

    // 5d. Auto-save and periodic maintenance
    tickAutoSave(dt);
    tickMaintenance(dt);
    wal_.flush();

    // 6. Check timeouts
    auto timedOut = server_.checkTimeouts(gameTime_);
    for (uint16_t id : timedOut) {
        LOG_INFO("Server", "Client %d timed out", id);
        if (server_.onClientDisconnected) server_.onClientDisconnected(id);
        server_.connections().removeClient(id);
    }

    // 7. Clean expired pending sessions
    for (auto it = pendingSessions_.begin(); it != pendingSessions_.end(); ) {
        if (static_cast<double>(gameTime_) > it->second.expires_at) {
            it = pendingSessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void ServerApp::consumePendingSessions() {
    AuthResult result;
    while (authServer_.popAuthResult(result)) {
        // Duplicate login: kick existing session for this account
        auto existing = activeAccountSessions_.find(result.session.account_id);
        if (existing != activeAccountSessions_.end()) {
            uint16_t oldClientId = existing->second;
            LOG_INFO("Server", "Duplicate login for account %d; kicking client %d",
                     result.session.account_id, oldClientId);
            // Erase BEFORE calling onClientDisconnected to avoid double-erase
            // and invalidated iterator (onClientDisconnected also erases this key)
            activeAccountSessions_.erase(existing);

            savePlayerToDB(oldClientId);

            auto* oldClient = server_.connections().findById(oldClientId);
            if (oldClient) {
                // onClientDisconnected handles entity cleanup + tracking map cleanup
                // It will try to erase from activeAccountSessions_ again but the key
                // is already gone, so that's a harmless no-op
                if (server_.onClientDisconnected) server_.onClientDisconnected(oldClientId);
                server_.connections().removeClient(oldClientId);
            }
        }

        // Store pending session with 30s expiry
        result.session.expires_at = static_cast<double>(gameTime_) + 30.0;
        pendingSessions_[result.token] = result.session;
    }
}

// ---------------------------------------------------------------------------
// Convert CachedSkillDef + per-rank DB rows → SkillDefinition for SkillManager
// ---------------------------------------------------------------------------
static SkillDefinition convertCachedToSkillDef(const CachedSkillDef& src,
                                                 const std::vector<CachedSkillRank>& ranks) {
    SkillDefinition def;
    def.skillId   = src.skillId;
    def.skillName = src.skillName;
    def.className = src.classRequired;

    // SkillType
    if (src.skillType == "Passive") def.skillType = SkillType::Passive;
    else                            def.skillType = SkillType::Active;

    // SkillTargetType
    if      (src.targetType == "Self")           def.targetType = SkillTargetType::Self;
    else if (src.targetType == "SingleAlly")     def.targetType = SkillTargetType::SingleAlly;
    else if (src.targetType == "AreaAroundSelf") def.targetType = SkillTargetType::AreaAroundSelf;
    else if (src.targetType == "AreaAtTarget")   def.targetType = SkillTargetType::AreaAtTarget;
    else if (src.targetType == "Cone")           def.targetType = SkillTargetType::Cone;
    else if (src.targetType == "Line")           def.targetType = SkillTargetType::Line;
    else                                         def.targetType = SkillTargetType::SingleEnemy;

    // DamageType
    if      (src.damageType == "Magic")     def.damageType = DamageType::Magic;
    else if (src.damageType == "Fire")      def.damageType = DamageType::Fire;
    else if (src.damageType == "Water")     def.damageType = DamageType::Water;
    else if (src.damageType == "Lightning") def.damageType = DamageType::Lightning;
    else if (src.damageType == "Poison")    def.damageType = DamageType::Poison;
    else if (src.damageType == "Void")      def.damageType = DamageType::Void;
    else if (src.damageType == "True")      def.damageType = DamageType::True;
    else                                    def.damageType = DamageType::Physical;

    // ResourceType
    if (src.resourceType == "Fury") def.resourceType = ResourceType::Fury;
    else                            def.resourceType = ResourceType::Mana;

    // Scalar fields
    def.range            = src.range;
    def.levelRequirement = src.levelRequired;
    def.castTime         = src.castTime;
    def.aoeRadius        = src.aoeRadius;
    def.canCrit          = src.canCrit;
    def.usesHitRate      = src.usesHitRate;
    def.furyOnHit        = src.furyOnHit;
    def.scalesWithResource = src.scalesWithResource;
    def.isUltimate       = src.isUltimate;
    def.description      = src.description;

    // Boolean flags
    def.appliesBleed  = src.appliesBleed;
    def.appliesBurn   = src.appliesBurn;
    def.appliesPoison = src.appliesPoison;
    def.appliesSlow   = src.appliesSlow;
    def.appliesFreeze = src.appliesFreeze;

    def.grantsInvulnerability = src.grantsInvulnerability;
    def.grantsStunImmunity    = src.grantsStunImmunity;
    def.grantsCritGuarantee   = src.grantsCritGuarantee;
    def.removesDebuffs        = src.removesDebuffs;

    def.teleportDistance    = src.teleportDistance;
    def.dashDistance        = src.dashDistance;
    def.transformDamageMult = 0.0f;
    def.transformSpeedBonus = 0.0f;

    // Per-rank arrays from DB skill_ranks rows
    def.maxRank = static_cast<int>(ranks.size());
    for (const auto& r : ranks) {
        def.costPerRank.push_back(static_cast<float>(r.resourceCost));
        def.cooldownPerRank.push_back(r.cooldownSeconds);
        def.damagePerRank.push_back(static_cast<float>(r.damagePercent));
        def.maxTargetsPerRank.push_back(r.maxTargets);
        def.stunDurationPerRank.push_back(r.stunDuration);
        def.effectDurationPerRank.push_back(r.effectDuration);
        def.effectValuePerRank.push_back(r.effectValue);
        def.executeThresholdPerRank.push_back(r.executeThreshold);
        def.passiveDamageReductionPerRank.push_back(r.passiveDamageReduction);
        def.passiveCritBonusPerRank.push_back(r.passiveCritBonus);
        def.passiveSpeedBonusPerRank.push_back(r.passiveSpeedBonus);
        def.passiveHPBonusPerRank.push_back(static_cast<int>(r.passiveHPBonus));
        def.passiveStatBonusPerRank.push_back(r.passiveStatBonus);
    }

    // Fill scalar convenience fields from rank 1 if available
    if (!ranks.empty()) {
        def.mpCost          = ranks[0].resourceCost;
        def.cooldownSeconds = ranks[0].cooldownSeconds;
        def.baseDamage      = ranks[0].damagePercent;
        def.transformDamageMult = ranks[0].transformDamageMult;
        def.transformSpeedBonus = ranks[0].transformSpeedBonus;
    }

    return def;
}

void ServerApp::onClientConnected(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client) return;

    // Drain any pending auth results that arrived between ticks
    consumePendingSessions();

    // Look up auth token in pending sessions
    auto it = pendingSessions_.find(client->authToken);
    if (it == pendingSessions_.end()) {
        LOG_WARN("Server", "Client %d has invalid or expired auth token; rejecting", clientId);
        server_.sendConnectReject(client->address, "Invalid or expired auth token");
        server_.connections().removeClient(clientId);
        return;
    }

    // Consume the pending session
    PendingSession session = it->second;
    pendingSessions_.erase(it);

    client->account_id = session.account_id;
    client->character_id = session.character_id;

    // Load character from DB
    auto recOpt = characterRepo_->loadCharacter(session.character_id);
    if (!recOpt) {
        LOG_ERROR("Server", "Client %d: failed to load character '%s'; rejecting",
                  clientId, session.character_id.c_str());
        server_.sendConnectReject(client->address, "Character not found");
        server_.connections().removeClient(clientId);
        return;
    }
    const CharacterRecord& rec = *recOpt;

    // Determine ClassType from class_name string
    ClassType classType = ClassType::Warrior;
    if (rec.class_name == "Mage") {
        classType = ClassType::Mage;
    } else if (rec.class_name == "Archer") {
        classType = ClassType::Archer;
    }

    // Create player entity
    Entity* player = EntityFactory::createPlayer(world_, rec.character_name, classType, false, Faction::None);

    // Override stats with DB values
    auto* charStatsComp = player->getComponent<CharacterStatsComponent>();
    if (charStatsComp) {
        CharacterStats& s = charStatsComp->stats;
        s.level        = rec.level;
        s.currentXP    = rec.current_xp;
        s.xpToNextLevel = static_cast<int64_t>(rec.xp_to_next_level);
        s.honor        = rec.honor;
        s.pvpKills     = rec.pvp_kills;
        s.pvpDeaths    = rec.pvp_deaths;
        s.isDead       = rec.is_dead;
        s.lifeState    = rec.is_dead ? LifeState::Dead : LifeState::Alive;
        // Fix legacy "Scene2" default — migrate to real scene name
        s.currentScene = (rec.current_scene == "Scene2" || rec.current_scene.empty())
            ? "WhisperingWoods" : rec.current_scene;

        // Initial stat calc (without equipment — equipment loaded below)
        s.recalculateStats();
        s.recalculateXPRequirement();

        // Stash DB HP/MP/fury values; actual clamping happens AFTER equipment
        // bonuses are applied (equipment can raise maxHP/maxMP).
        s.currentFury = rec.current_fury;
    }

    // Set position (DB stores tile coords, engine uses pixel coords)
    auto* t = player->getComponent<Transform>();
    if (t) {
        t->position = Coords::toPixel({rec.position_x, rec.position_y});
    }

    // Set gold on inventory
    auto* inv = player->getComponent<InventoryComponent>();
    if (inv) {
        inv->inventory.addGold(rec.gold);

        // Load inventory items from DB
        auto invSlots = inventoryRepo_->loadInventory(session.character_id);
        for (const auto& slot : invSlots) {
            ItemInstance item;
            item.instanceId   = slot.instance_id;
            item.itemId       = slot.item_id;
            item.quantity      = slot.quantity;
            item.enchantLevel = slot.enchant_level;
            item.isProtected  = slot.is_protected;
            item.isSoulbound  = slot.is_soulbound;
            item.isBroken     = slot.is_broken;
            item.rolledStats  = ItemStatRoller::parseRolledStats(slot.rolled_stats);

            // Look up display info from item definition cache
            const auto* def = itemDefCache_.getDefinition(slot.item_id);
            if (def) {
                item.displayName = def->displayName;
                item.rarity = parseItemRarity(def->rarity);
            }

            // Socket
            if (!slot.socket_stat.empty() && slot.socket_value > 0) {
                item.socket.statType = ItemStatRoller::getStatType(slot.socket_stat);
                item.socket.value = slot.socket_value;
                item.socket.isEmpty = false;
            }

            if (slot.is_equipped && !slot.equipped_slot.empty()) {
                // Parse equipment slot name to enum and equip directly
                EquipmentSlot eqSlot = EquipmentSlot::None;
                const auto& s = slot.equipped_slot;
                if (s == "Weapon")    eqSlot = EquipmentSlot::Weapon;
                else if (s == "SubWeapon") eqSlot = EquipmentSlot::SubWeapon;
                else if (s == "Hat")       eqSlot = EquipmentSlot::Hat;
                else if (s == "Armor")     eqSlot = EquipmentSlot::Armor;
                else if (s == "Gloves")    eqSlot = EquipmentSlot::Gloves;
                else if (s == "Shoes")     eqSlot = EquipmentSlot::Shoes;
                else if (s == "Belt")      eqSlot = EquipmentSlot::Belt;
                else if (s == "Cloak")     eqSlot = EquipmentSlot::Cloak;
                else if (s == "Ring")      eqSlot = EquipmentSlot::Ring;
                else if (s == "Necklace")  eqSlot = EquipmentSlot::Necklace;

                if (eqSlot != EquipmentSlot::None) {
                    // Place directly into equipment slot (bypasses callbacks during load)
                    auto equipMap = inv->inventory.getEquipmentMap();
                    equipMap[eqSlot] = item;
                    inv->inventory.setSerializedState(inv->inventory.getGold(),
                        std::vector<ItemInstance>(inv->inventory.getSlots()),
                        std::move(equipMap));
                } else {
                    inv->inventory.addItem(item); // fallback
                }
            } else if (slot.slot_index >= 0) {
                // Place in specific inventory slot
                inv->inventory.addItemToSlot(slot.slot_index, item);
            } else {
                inv->inventory.addItem(item);
            }
        }
        LOG_INFO("Server", "Loaded %zu inventory items for %s",
                 invSlots.size(), rec.character_name.c_str());

        // Apply equipment bonuses from equipped items, then recalculate derived stats
        recalcEquipmentBonuses(player);
    }

    // Now that equipment bonuses are applied, clamp HP/MP to final maxHP/maxMP
    if (charStatsComp) {
        CharacterStats& s = charStatsComp->stats;
        s.currentHP = (std::min)(static_cast<int>(rec.current_hp), s.maxHP);
        s.currentMP = (std::min)(static_cast<int>(rec.current_mp), s.maxMP);

        // If player was not dead but DB has HP=0 (stale save), heal to full
        if (!s.isDead && s.currentHP <= 0) {
            s.currentHP = s.maxHP;
            s.currentMP = s.maxMP;
            LOG_INFO("Server", "Healed '%s' to full HP on connect (stale DB HP=0)",
                     rec.character_name.c_str());
        }
    }

    // Assign persistent ID based on hash of character_id
    uint64_t pidVal = std::hash<std::string>{}(rec.character_id);
    if (pidVal == 0) pidVal = 1;
    PersistentId pid(pidVal);

    // Register with replication
    replication_.registerEntity(player->handle(), pid);

    // Link client to their player entity
    client->playerEntityId = pid.value();

    // Track active account session
    activeAccountSessions_[session.account_id] = clientId;

    // Load skills from DB
    auto* skillComp = player->getComponent<SkillManagerComponent>();
    if (skillComp) {
        auto skills = skillRepo_->loadCharacterSkills(rec.character_id);
        for (const auto& s : skills) {
            skillComp->skills.learnSkill(s.skillId, s.unlockedRank);
            // activateSkillRank activates the NEXT rank each call
            for (int r = 0; r < s.activatedRank; ++r)
                skillComp->skills.activateSkillRank(s.skillId);
        }
        auto skillBar = skillRepo_->loadSkillBar(rec.character_id);
        for (int i = 0; i < static_cast<int>(skillBar.size()) && i < 20; ++i) {
            if (!skillBar[i].empty())
                skillComp->skills.assignSkillToSlot(skillBar[i], i);
        }
        auto sp = skillRepo_->loadSkillPoints(rec.character_id);
        // Restore earned skill points; activateSkillRank above already consumed spent ones
        for (int i = 0; i < sp.totalEarned; ++i)
            skillComp->skills.grantSkillPoint();

        // Register all skill definitions so executeSkill can look them up.
        // getSkillsForClass returns class-specific + classless skills.
        auto classDefs = skillDefCache_.getSkillsForClass(rec.class_name);
        for (const auto* cached : classDefs) {
            auto ranks = skillDefCache_.getRanks(cached->skillId);
            skillComp->skills.registerSkillDefinition(
                convertCachedToSkillDef(*cached, ranks));
        }
        LOG_INFO("Server", "Registered %zu skill definitions for %s (%s)",
                 classDefs.size(), rec.character_name.c_str(), rec.class_name.c_str());
    }

    // Load guild membership
    auto* guildComp = player->getComponent<GuildComponent>();
    if (guildComp) {
        guildComp->guild.serverInitialize(rec.character_id);
        int guildId = guildRepo_->getPlayerGuildId(rec.character_id);
        if (guildId > 0) {
            auto guildInfo = guildRepo_->getGuildInfo(guildId);
            int rank = guildRepo_->getPlayerRank(rec.character_id);
            if (guildInfo) {
                guildComp->guild.setGuildData(guildInfo->guildId, guildInfo->guildName,
                                               {}, static_cast<GuildRank>(rank),
                                               guildInfo->guildLevel);
            }
        }
    }

    // Load friends and blocks
    auto* friendsComp = player->getComponent<FriendsComponent>();
    if (friendsComp) {
        auto friends = socialRepo_->getFriends(rec.character_id);
        auto blocks = socialRepo_->getBlockedPlayers(rec.character_id);
        // FriendsManager is initialized from these records via the networking layer
        // For now, the data is loaded and ready for when sync messages are sent
        friendsComp->friends.initialize(rec.character_id);
    }

    // Load quest progress
    auto* questComp = player->getComponent<QuestComponent>();
    if (questComp) {
        auto activeQuests = questRepo_->loadQuestProgress(rec.character_id);
        auto completedIds = questRepo_->loadCompletedQuests(rec.character_id);

        // Convert DB records to QuestManager format
        std::vector<uint32_t> completedU32;
        completedU32.reserve(completedIds.size());
        for (const auto& id : completedIds) {
            try { completedU32.push_back(static_cast<uint32_t>(std::stoul(id))); }
            catch (...) { /* skip non-numeric quest IDs */ }
        }

        std::vector<ActiveQuest> activeQuestObjs;
        for (const auto& q : activeQuests) {
            ActiveQuest aq;
            try { aq.questId = static_cast<uint32_t>(std::stoul(q.questId)); }
            catch (...) { continue; }
            // Restore progress — single-objective quests store count in index 0
            if (q.currentCount > 0) {
                aq.objectiveProgress.push_back(static_cast<uint16_t>(q.currentCount));
            }
            activeQuestObjs.push_back(std::move(aq));
        }

        questComp->quests.setSerializedState(std::move(completedU32), std::move(activeQuestObjs));
    }

    // Load bank storage
    auto* bankComp = player->getComponent<BankStorageComponent>();
    if (bankComp) {
        int64_t bankGold = bankRepo_->loadBankGold(rec.character_id);
        bankComp->storage.depositGold(bankGold, 0.0f); // no fee on load
        // Bank items loaded on-demand when player opens banker NPC
    }

    // Load equipped pet
    auto* petComp = player->getComponent<PetComponent>();
    if (petComp) {
        auto equippedPet = petRepo_->loadEquippedPet(rec.character_id);
        if (equippedPet) {
            petComp->equippedPet.petDefinitionId = equippedPet->petDefId;
            petComp->equippedPet.petName = equippedPet->petName;
            petComp->equippedPet.level = equippedPet->level;
            petComp->equippedPet.currentXP = equippedPet->currentXP;
            petComp->equippedPet.autoLootEnabled = equippedPet->autoLootEnabled;
            petComp->equippedPet.isSoulbound = equippedPet->isSoulbound;
            petComp->dbPetId = equippedPet->id;
        }
    }

    // Update last_online timestamp
    socialRepo_->updateLastOnline(rec.character_id);

    // Initialize movement tracking — first CmdMove accepted unconditionally
    // (client position may differ from DB-saved position due to tile↔pixel rounding)
    lastValidPositions_[clientId] = t ? t->position : Vec2{0.0f, 0.0f};
    lastMoveTime_[clientId] = gameTime_;
    moveCountThisTick_[clientId] = 0;
    skillCommandsThisTick_[clientId] = 0;
    needsFirstMoveSync_.insert(clientId);

    // Stagger auto-save (offset by clientId to spread DB load)
    float saveOffset = static_cast<float>(clientId % 60);
    nextAutoSaveTime_[clientId] = gameTime_ + saveOffset + AUTO_SAVE_INTERVAL;

    // Wire onEquipmentChanged callback: recalculate stats when equipment changes
    if (inv) {
        inv->inventory.onEquipmentChanged = [this, clientId](EquipmentSlot) {
            auto* cl = server_.connections().findById(clientId);
            if (!cl || cl->playerEntityId == 0) return;
            PersistentId p(cl->playerEntityId);
            EntityHandle eh = replication_.getEntityHandle(p);
            Entity* ent = world_.getEntity(eh);
            if (!ent) return;
            recalcEquipmentBonuses(ent);
            sendPlayerState(clientId);
        };
    }

    // Wire onDied callback: clear all effects on death
    if (charStatsComp) {
        auto* deathSEComp = player->getComponent<StatusEffectComponent>();
        auto* deathCCComp = player->getComponent<CrowdControlComponent>();
        charStatsComp->stats.onDied = [deathSEComp, deathCCComp]() {
            if (deathSEComp) deathSEComp->effects.removeAllEffects();
            if (deathCCComp) deathCCComp->cc.removeAllCC();
        };
    }

    // Wire DoT tick callback: apply DoT damage to this player entity
    auto* seComp = player->getComponent<StatusEffectComponent>();
    if (seComp && charStatsComp) {
        seComp->effects.onDoTTick = [this, clientId](EffectType type, int damage) {
            auto* cl = server_.connections().findById(clientId);
            if (!cl || cl->playerEntityId == 0) return;
            PersistentId p(cl->playerEntityId);
            EntityHandle eh = replication_.getEntityHandle(p);
            Entity* ent = world_.getEntity(eh);
            if (!ent) return;
            auto* cs = ent->getComponent<CharacterStatsComponent>();
            if (!cs || !cs->stats.isAlive()) return;

            cs->stats.currentHP = (std::max)(0, cs->stats.currentHP - damage);

            // Broadcast DoT damage as a combat event
            SvCombatEventMsg evt;
            evt.attackerId = 0; // DoT source — could track via StatusEffect.sourceEntityId
            evt.targetId   = cl->playerEntityId;
            evt.damage     = damage;
            evt.skillId    = 0;
            evt.isCrit     = 0;
            evt.isKill     = cs->stats.currentHP <= 0 ? 1 : 0;
            uint8_t buf[64]; ByteWriter w(buf, sizeof(buf));
            evt.write(w);
            server_.broadcast(Channel::Unreliable, PacketType::SvCombatEvent, buf, w.size());

            if (cs->stats.currentHP <= 0) {
                cs->stats.die(DeathSource::PvE);
                SvDeathNotifyMsg deathMsg;
                deathMsg.deathSource = 0; // PvE
                deathMsg.respawnTimer = 5.0f;
                deathMsg.xpLost = 0;
                deathMsg.honorLost = 0;
                uint8_t dbuf[32]; ByteWriter dw(dbuf, sizeof(dbuf));
                deathMsg.write(dw);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvDeathNotify, dbuf, dw.size());
            }
        };
    }

    // Wire level-up callback: send SvLevelUpMsg + full state on level-up
    if (charStatsComp) {
        charStatsComp->stats.onLevelUp = [this, clientId]() {
            auto* cl = server_.connections().findById(clientId);
            if (!cl || cl->playerEntityId == 0) return;
            PersistentId p(cl->playerEntityId);
            EntityHandle eh = replication_.getEntityHandle(p);
            Entity* ent = world_.getEntity(eh);
            if (!ent) return;
            auto* cs = ent->getComponent<CharacterStatsComponent>();
            if (!cs) return;
            const auto& st = cs->stats;

            SvLevelUpMsg lvl;
            lvl.newLevel     = st.level;
            lvl.newMaxHP     = st.maxHP;
            lvl.newMaxMP     = st.maxMP;
            lvl.newCurrentHP = st.currentHP;
            lvl.newCurrentMP = st.currentMP;
            lvl.newArmor     = st.getArmor();
            lvl.newCritRate  = st.getCritRate();
            lvl.newSpeed     = st.getSpeed();
            lvl.newDamageMult = st.getDamageMultiplier();

            uint8_t buf[64]; ByteWriter w(buf, sizeof(buf));
            lvl.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvLevelUp, buf, w.size());

            // Also send full player state
            sendPlayerState(clientId);
        };
    }

    // Send initial player state and full sync
    sendPlayerState(clientId);
    sendSkillSync(clientId);
    sendQuestSync(clientId);
    sendInventorySync(clientId);

    // If player reconnects while dead, notify client so death overlay shows
    if (rec.is_dead) {
        SvDeathNotifyMsg deathMsg;
        deathMsg.deathSource = 0;
        deathMsg.respawnTimer = 0.0f;  // timer already expired, can respawn immediately
        deathMsg.xpLost = 0;           // penalty was already applied on original death
        deathMsg.honorLost = 0;
        uint8_t buf[32]; ByteWriter w(buf, sizeof(buf));
        deathMsg.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvDeathNotify, buf, w.size());
        LOG_INFO("Server", "Client %d reconnected dead — sent death notification", clientId);
    }

    // Send AEAD encryption keys (KeyExchange)
    if (PacketCrypto::isAvailable()) {
        auto keys = PacketCrypto::generateSessionKeys();
        // Server encrypts with rxKey, decrypts with txKey (mirror of client)
        client->crypto.setKeys(keys.rxKey, keys.txKey);

        // Send txKey (client's encrypt key) and rxKey (client's decrypt key)
        uint8_t keyBuf[64];
        ByteWriter kw(keyBuf, sizeof(keyBuf));
        kw.writeBytes(keys.txKey.data(), 32);
        kw.writeBytes(keys.rxKey.data(), 32);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::KeyExchange, keyBuf, kw.size());
        LOG_INFO("Server", "Sent AEAD session keys to client %d", clientId);
    }

    LOG_INFO("Server", "Client %d connected: account=%d char='%s' level=%d guild=%d",
             clientId, session.account_id, rec.character_name.c_str(), rec.level,
             guildComp ? guildComp->guild.guildId : 0);
}

void ServerApp::savePlayerToDB(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* e = world_.getEntity(h);
    if (!e) return;

    CharacterRecord rec;
    rec.character_id = client->character_id;
    rec.account_id   = client->account_id;

    auto* charStatsComp = e->getComponent<CharacterStatsComponent>();
    if (charStatsComp) {
        const CharacterStats& s = charStatsComp->stats;
        rec.character_name   = s.characterName;
        rec.class_name       = s.className;
        rec.level            = s.level;
        rec.current_xp       = s.currentXP;
        rec.xp_to_next_level = static_cast<int>(s.xpToNextLevel);
        rec.current_hp       = s.currentHP;
        rec.max_hp           = s.maxHP;
        rec.current_mp       = s.currentMP;
        rec.max_mp           = s.maxMP;
        rec.current_fury     = s.currentFury;
        rec.honor            = s.honor;
        rec.pvp_kills        = s.pvpKills;
        rec.pvp_deaths       = s.pvpDeaths;
        rec.is_dead          = s.isDead;
    }

    auto* t = e->getComponent<Transform>();
    if (t) {
        // Convert pixel coords to tile coords for DB (matches Unity format)
        Vec2 tilePos = Coords::toTile(t->position);
        rec.position_x = tilePos.x;
        rec.position_y = tilePos.y;
    }

    // Save current scene from the player's own stats (not SceneManager, which
    // is a client-side concept — the server has no loaded scene).
    auto* statsForScene = e->getComponent<CharacterStatsComponent>();
    rec.current_scene = (statsForScene && !statsForScene->stats.currentScene.empty())
        ? statsForScene->stats.currentScene
        : "WhisperingWoods";

    auto* inv = e->getComponent<InventoryComponent>();
    if (inv) {
        rec.gold = inv->inventory.getGold();
    }

    if (!characterRepo_->saveCharacter(rec)) {
        // Retry once
        if (!characterRepo_->saveCharacter(rec)) {
            LOG_ERROR("Server", "DATA LOSS: failed to save character '%s' (client %d) after retry",
                      rec.character_id.c_str(), clientId);
        }
    }

    // Save skills
    auto* skillComp = e->getComponent<SkillManagerComponent>();
    if (skillComp) {
        // Collect learned skills from vector
        std::vector<CharacterSkillRecord> skillRecords;
        for (const auto& learned : skillComp->skills.getLearnedSkills()) {
            CharacterSkillRecord sr;
            sr.skillId = learned.skillId;
            sr.unlockedRank = learned.unlockedRank;
            sr.activatedRank = learned.activatedRank;
            skillRecords.push_back(std::move(sr));
        }
        skillRepo_->saveAllCharacterSkills(rec.character_id, skillRecords);

        // Save skill bar
        std::vector<std::string> bar;
        bar.reserve(20);
        for (int i = 0; i < 20; ++i) {
            bar.push_back(skillComp->skills.getSkillInSlot(i));
        }
        skillRepo_->saveSkillBar(rec.character_id, bar);

        // Save skill points
        int earned = skillComp->skills.earnedPoints();
        int spent = earned - skillComp->skills.availablePoints();
        skillRepo_->saveSkillPoints(rec.character_id, earned, spent);
    }

    // Save quest progress
    auto* questComp = e->getComponent<QuestComponent>();
    if (questComp) {
        std::vector<QuestProgressRecord> questRecords;
        for (const auto& aq : questComp->quests.getActiveQuests()) {
            QuestProgressRecord qr;
            qr.questId = std::to_string(aq.questId);
            qr.status = "active";
            qr.currentCount = aq.objectiveProgress.empty() ? 0 : aq.objectiveProgress[0];
            qr.targetCount = 1; // Will be updated by quest definition lookup if needed
            questRecords.push_back(std::move(qr));
        }
        questRepo_->saveAllQuestProgress(rec.character_id, questRecords);
    }

    // Save bank gold (items saved on-demand when deposited/withdrawn)
    auto* bankComp = e->getComponent<BankStorageComponent>();
    if (bankComp) {
        int64_t bankGold = bankComp->storage.getStoredGold();
        if (bankGold > 0) {
            bankRepo_->depositGold(rec.character_id, 0); // ensure row exists
            // Direct set via raw query would be cleaner, but depositGold upserts
        }
    }

    // Save pet state
    auto* petComp = e->getComponent<PetComponent>();
    if (petComp && petComp->hasPet()) {
        PetRecord petRec;
        petRec.id = petComp->dbPetId;
        petRec.characterId = rec.character_id;
        petRec.petDefId = petComp->equippedPet.petDefinitionId;
        petRec.petName = petComp->equippedPet.petName;
        petRec.level = petComp->equippedPet.level;
        petRec.currentXP = petComp->equippedPet.currentXP;
        petRec.isEquipped = true;
        petRec.isSoulbound = petComp->equippedPet.isSoulbound;
        petRec.autoLootEnabled = petComp->equippedPet.autoLootEnabled;
        petRepo_->savePet(petRec);
    }

    // Update last_online
    socialRepo_->updateLastOnline(rec.character_id);
}

void ServerApp::savePlayerToDBAsync(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* e = world_.getEntity(h);
    if (!e) return;

    // ---- Snapshot all data on game thread ----
    CharacterRecord rec;
    rec.character_id = client->character_id;
    rec.account_id   = client->account_id;

    auto* charStatsComp = e->getComponent<CharacterStatsComponent>();
    if (charStatsComp) {
        const CharacterStats& s = charStatsComp->stats;
        rec.character_name   = s.characterName;
        rec.class_name       = s.className;
        rec.level            = s.level;
        rec.current_xp       = s.currentXP;
        rec.xp_to_next_level = static_cast<int>(s.xpToNextLevel);
        rec.current_hp       = s.currentHP;
        rec.max_hp           = s.maxHP;
        rec.current_mp       = s.currentMP;
        rec.max_mp           = s.maxMP;
        rec.current_fury     = s.currentFury;
        rec.honor            = s.honor;
        rec.pvp_kills        = s.pvpKills;
        rec.pvp_deaths       = s.pvpDeaths;
        rec.is_dead          = s.isDead;
    }

    auto* t = e->getComponent<Transform>();
    if (t) {
        Vec2 tilePos = Coords::toTile(t->position);
        rec.position_x = tilePos.x;
        rec.position_y = tilePos.y;
    }

    auto* statsForScene2 = e->getComponent<CharacterStatsComponent>();
    rec.current_scene = (statsForScene2 && !statsForScene2->stats.currentScene.empty())
        ? statsForScene2->stats.currentScene
        : "WhisperingWoods";

    auto* inv = e->getComponent<InventoryComponent>();
    if (inv) rec.gold = inv->inventory.getGold();

    // Snapshot skills
    std::vector<CharacterSkillRecord> skillRecords;
    int skillEarned = 0, skillSpent = 0;
    std::vector<std::string> skillBar(20, "");
    auto* skillComp = e->getComponent<SkillManagerComponent>();
    if (skillComp) {
        for (const auto& learned : skillComp->skills.getLearnedSkills()) {
            CharacterSkillRecord sr;
            sr.skillId = learned.skillId;
            sr.unlockedRank = learned.unlockedRank;
            sr.activatedRank = learned.activatedRank;
            skillRecords.push_back(std::move(sr));
        }
        skillEarned = skillComp->skills.earnedPoints();
        skillSpent = skillEarned - skillComp->skills.availablePoints();
        for (int i = 0; i < 20; ++i)
            skillBar[i] = skillComp->skills.getSkillInSlot(i);
    }

    std::string charId = client->character_id;

    // ---- Dispatch to fiber worker (non-blocking) ----
    // Acquire per-player lock inside the fiber to serialize against concurrent
    // game-thread mutations (trade execution, loot, market) that may modify
    // inventory/gold while the async save is writing state.
    dbDispatcher_.dispatchVoid([this, rec, charId, skillRecords, skillEarned, skillSpent, skillBar]
                               (pqxx::connection& conn) {
        std::lock_guard<std::mutex> lock(playerLocks_.get(charId));
        CharacterRepository charRepo(conn);
        charRepo.saveCharacter(rec);

        SkillRepository skillRepo(conn);
        skillRepo.saveAllCharacterSkills(charId, skillRecords);
        skillRepo.saveSkillBar(charId, skillBar);
        skillRepo.saveSkillPoints(charId, skillEarned, skillSpent);

        SocialRepository socialRepo(conn);
        socialRepo.updateLastOnline(charId);
    });
}

void ServerApp::saveInventoryForClient(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* e = world_.getEntity(h);
    if (!e) return;

    auto* inv = e->getComponent<InventoryComponent>();
    if (!inv) return;

    std::vector<InventorySlotRecord> slots;
    const auto& items = inv->inventory.getSlots();
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (!items[i].isValid()) continue;
        InventorySlotRecord s;
        s.instance_id   = items[i].instanceId;
        s.character_id  = client->character_id;
        s.item_id       = items[i].itemId;
        s.slot_index    = i;
        s.rolled_stats  = ItemStatRoller::rolledStatsToJson(items[i].rolledStats);
        s.enchant_level = items[i].enchantLevel;
        s.is_protected  = items[i].isProtected;
        s.is_soulbound  = items[i].isSoulbound;
        s.is_broken     = items[i].isBroken;
        s.quantity      = items[i].quantity;
        slots.push_back(std::move(s));
    }

    inventoryRepo_->saveInventory(client->character_id, slots);
}

void ServerApp::onClientDisconnected(uint16_t clientId) {
    LOG_INFO("Server", "Client %d disconnected", clientId);

    // Save player data first
    savePlayerToDB(clientId);

    // Remove from active account sessions
    auto* client = server_.connections().findById(clientId);
    if (client && client->account_id != 0) {
        activeAccountSessions_.erase(client->account_id);
    }

    if (client && client->playerEntityId != 0) {
        PersistentId pid(client->playerEntityId);
        EntityHandle h = replication_.getEntityHandle(pid);
        // Unregister from replication BEFORE destroying — prevents dangling
        // handle in spatial index on next tick's rebuildSpatialIndex()
        replication_.unregisterEntity(h);
        if (h) {
            world_.destroyEntity(h);
            world_.processDestroyQueue();
        }
    }

    // Cancel any active trade session for the disconnecting player
    if (client && !client->character_id.empty()) {
        auto tradeSession = tradeRepo_->getActiveSession(client->character_id);
        if (tradeSession) {
            tradeRepo_->cancelSession(tradeSession->sessionId);
            // Notify the other player that the trade was cancelled
            std::string otherCharId = (client->character_id == tradeSession->playerACharacterId)
                ? tradeSession->playerBCharacterId : tradeSession->playerACharacterId;
            server_.connections().forEach([&](ClientConnection& c) {
                if (c.character_id == otherCharId) {
                    SvTradeUpdateMsg cancelMsg;
                    cancelMsg.updateType = 6; // cancelled
                    cancelMsg.resultCode = 9; // partner disconnected
                    cancelMsg.otherPlayerName = "Trade cancelled — other player disconnected";
                    uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                    cancelMsg.write(w);
                    server_.sendTo(c.clientId, Channel::ReliableOrdered,
                                   PacketType::SvTradeUpdate, buf, w.size());
                }
            });
            LOG_INFO("Server", "Cancelled trade session %d due to client %d disconnect",
                     tradeSession->sessionId, clientId);
        }
    }

    // Release per-player mutex entry (after all synchronous saves complete)
    if (client && !client->character_id.empty()) {
        playerLocks_.erase(client->character_id);
    }

    // Clean up tracking maps
    lastValidPositions_.erase(clientId);
    lastMoveTime_.erase(clientId);
    moveCountThisTick_.erase(clientId);
    skillCommandsThisTick_.erase(clientId);
    nextAutoSaveTime_.erase(clientId);
    needsFirstMoveSync_.erase(clientId);
    lastAutoAttackTime_.erase(clientId);
    skillCooldowns_.erase(clientId);
    rateLimiters_.erase(clientId);
    nonceManager_.removeClient(clientId);
}

void ServerApp::onPacketReceived(uint16_t clientId, uint8_t type, ByteReader& payload) {
    // Token bucket rate limiting — checked before any packet processing
    {
        double now = static_cast<double>(gameTime_);
        auto result = rateLimiters_[clientId].check(type, now);
        if (result == RateLimitResult::Disconnect) {
            LOG_WARN("Server", "Client %d disconnected for rate limit abuse (type=0x%02X)", clientId, type);
            server_.connections().removeClient(clientId);
            onClientDisconnected(clientId);
            return;
        }
        if (result == RateLimitResult::Dropped) {
            return;
        }
    }

    // Handle game packets — stub for now, will be implemented in Phase 6C/6D
    switch (type) {
        case PacketType::CmdMove: {
            auto move = CmdMove::read(payload);

            // Rate limit check
            int maxPerTick = static_cast<int>(MAX_MOVES_PER_SEC / TICK_RATE);
            if (maxPerTick < 1) maxPerTick = 1;
            moveCountThisTick_[clientId]++;
            if (moveCountThisTick_[clientId] > maxPerTick) {
                // Silently drop excess moves (was spamming logs)
                break;
            }

            auto* client = server_.connections().findById(clientId);
            if (client && client->playerEntityId != 0) {
                PersistentId pid(client->playerEntityId);
                EntityHandle h = replication_.getEntityHandle(pid);
                Entity* e = world_.getEntity(h);
                if (!e) break;

                // First move after connect: accept unconditionally (position desync)
                if (needsFirstMoveSync_.count(clientId)) {
                    needsFirstMoveSync_.erase(clientId);
                    auto* t = e->getComponent<Transform>();
                    if (t) t->position = move.position;
                    lastValidPositions_[clientId] = move.position;
                    lastMoveTime_[clientId] = gameTime_;
                    break;
                }

                // Compute time delta since last move
                float now = gameTime_;
                float timeDelta = now - lastMoveTime_[clientId];
                if (timeDelta < 0.001f) timeDelta = 0.001f;

                // Check distance against max allowed
                Vec2 lastPos = lastValidPositions_[clientId];
                float dist = lastPos.distance(move.position);
                float maxDist = MAX_MOVE_SPEED * timeDelta;

                if (dist > maxDist + RUBBER_BAND_THRESHOLD) {
                    // Rubber-band: reject move and send correction
                    LOG_WARN("Server", "Client %d moved too far (%.1f > %.1f), rubber-banding",
                             clientId, dist, maxDist);
                    SvMovementCorrectionMsg correction;
                    correction.correctedPosition = lastPos;
                    correction.rubberBand = 1;
                    uint8_t buf[32];
                    ByteWriter w(buf, sizeof(buf));
                    correction.write(w);
                    server_.sendTo(clientId, Channel::Unreliable,
                                   PacketType::SvMovementCorrection, buf, w.size());
                } else {
                    // Accept position
                    auto* t = e->getComponent<Transform>();
                    if (t) t->position = move.position;
                    lastValidPositions_[clientId] = move.position;
                    lastMoveTime_[clientId] = now;
                }
            }
            break;
        }
        case PacketType::CmdAction: {
            auto action = CmdAction::read(payload);
            processAction(clientId, action);
            break;
        }
        case PacketType::CmdChat: {
            auto chat = CmdChat::read(payload);

            // Server-side profanity filter
            if (chat.message.empty() || chat.message.size() > 200) return;

            auto filterResult = ProfanityFilter::filterChatMessage(chat.message, FilterMode::Censor);
            chat.message = filterResult.filteredText;

            LOG_INFO("Server", "Chat from client %d (ch=%d): %s",
                     clientId, chat.channel, chat.message.c_str());

            // Get sender info
            std::string senderName = "Unknown";
            uint8_t senderFaction = 0;
            auto* client = server_.connections().findById(clientId);
            if (client && client->playerEntityId != 0) {
                PersistentId pid(client->playerEntityId);
                EntityHandle h = replication_.getEntityHandle(pid);
                Entity* e = world_.getEntity(h);
                if (e) {
                    auto* nameplate = e->getComponent<NameplateComponent>();
                    if (nameplate) senderName = nameplate->displayName;
                    auto* factionComp = e->getComponent<FactionComponent>();
                    if (factionComp) senderFaction = static_cast<uint8_t>(factionComp->faction);
                }
            }

            // Build and broadcast chat message
            SvChatMessageMsg msg;
            msg.channel    = chat.channel;
            msg.senderName = senderName;
            msg.message    = chat.message;
            msg.faction    = senderFaction;

            uint8_t buf[512];
            ByteWriter w(buf, sizeof(buf));
            msg.write(w);
            server_.broadcast(Channel::ReliableOrdered, PacketType::SvChatMessage, buf, w.size());
            break;
        }
        case PacketType::CmdMarket: {
            uint8_t subAction = payload.readU8();
            auto* client = server_.connections().findById(clientId);
            if (!client || client->playerEntityId == 0) break;

            switch (subAction) {
                case MarketAction::ListItem: {
                    std::string instanceId = payload.readString();
                    int64_t priceGold = detail::readI64(payload);

                    PersistentId pid(client->playerEntityId);
                    EntityHandle h = replication_.getEntityHandle(pid);
                    Entity* e = world_.getEntity(h);
                    if (!e) break;

                    auto* inv = e->getComponent<InventoryComponent>();
                    if (!inv) break;

                    auto sendMarketError = [&](const std::string& msg) {
                        SvMarketResultMsg resp;
                        resp.action = MarketAction::ListItem;
                        resp.resultCode = 1;
                        resp.message = msg;
                        uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                        resp.write(w);
                        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvMarketResult, buf, w.size());
                    };

                    // Validate price
                    if (priceGold <= 0 || priceGold > MarketConstants::MAX_LISTING_PRICE) {
                        sendMarketError("Invalid price"); break;
                    }

                    // Validate listing count
                    int activeCount = marketRepo_->countActiveListings(client->character_id);
                    if (activeCount >= MarketConstants::MAX_LISTINGS_PER_PLAYER) {
                        sendMarketError("Maximum listings reached (7)"); break;
                    }

                    // Find item in inventory by instance ID
                    int slot = inv->inventory.findByInstanceId(instanceId);
                    if (slot < 0) { sendMarketError("Item not found in inventory"); break; }

                    ItemInstance item = inv->inventory.getSlot(slot);
                    if (item.isBound()) { sendMarketError("Soulbound items cannot be listed"); break; }
                    if (inv->inventory.isSlotLocked(slot)) { sendMarketError("Item is locked for trade"); break; }

                    // Look up item definition for listing metadata
                    const auto* def = itemDefCache_.getDefinition(item.itemId);
                    std::string itemName = def ? def->displayName : item.itemId;
                    std::string category = def ? def->itemType : "";
                    std::string subtype  = def ? def->subtype : "";
                    std::string rarity   = def ? def->rarity : "Common";
                    int itemLevel        = def ? def->levelReq : 1;

                    std::string rolledJson = ItemStatRoller::rolledStatsToJson(item.rolledStats);
                    std::string socketStat;
                    int socketVal = 0;
                    if (item.hasSocket()) {
                        // Convert StatType to string for DB
                        switch (item.socket.statType) {
                            case StatType::Strength:     socketStat = "STR"; break;
                            case StatType::Dexterity:    socketStat = "DEX"; break;
                            case StatType::Intelligence: socketStat = "INT"; break;
                            default: break;
                        }
                        socketVal = item.socket.value;
                    }

                    // Create listing in DB
                    int listingId = marketRepo_->createListing(
                        client->character_id, "", // seller name filled by DB or we fetch it
                        instanceId, item.itemId, itemName,
                        item.quantity, item.enchantLevel,
                        rolledJson, socketStat, socketVal, priceGold,
                        category, subtype, rarity, itemLevel);

                    if (listingId > 0) {
                        // WAL: record item removal before mutating inventory
                        wal_.appendItemRemove(client->character_id, slot);
                        // Remove from inventory
                        inv->inventory.removeItem(slot);

                        SvMarketResultMsg resp;
                        resp.action = MarketAction::ListItem;
                        resp.resultCode = 0;
                        resp.listingId = listingId;
                        resp.message = itemName + " listed for " + std::to_string(priceGold) + " gold";
                        uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                        resp.write(w);
                        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvMarketResult, buf, w.size());
                        sendPlayerState(clientId);
                        saveInventoryForClient(clientId);
                    } else {
                        sendMarketError("Failed to create listing");
                    }
                    break;
                }
                case MarketAction::BuyItem: {
                    int32_t listingId = payload.readI32();

                    PersistentId pid(client->playerEntityId);
                    EntityHandle h = replication_.getEntityHandle(pid);
                    Entity* e = world_.getEntity(h);
                    if (!e) break;

                    auto* inv = e->getComponent<InventoryComponent>();
                    if (!inv) break;

                    auto sendMarketError = [&](const std::string& msg) {
                        SvMarketResultMsg resp;
                        resp.action = MarketAction::BuyItem;
                        resp.resultCode = 1;
                        resp.message = msg;
                        uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                        resp.write(w);
                        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvMarketResult, buf, w.size());
                    };

                    // Get listing
                    auto listing = marketRepo_->getListing(listingId);
                    if (!listing || !listing->isActive) { sendMarketError("Listing not found or expired"); break; }

                    // Can't buy own listing
                    if (listing->sellerCharacterId == client->character_id) {
                        sendMarketError("Cannot buy your own listing"); break;
                    }

                    // Check buyer gold
                    if (inv->inventory.getGold() < listing->priceGold) {
                        sendMarketError("Not enough gold"); break;
                    }

                    // Check buyer inventory space
                    if (inv->inventory.freeSlots() <= 0) {
                        sendMarketError("Inventory full"); break;
                    }

                    // Execute purchase in a transaction via the pool
                    try {
                        auto guard = dbPool_.acquire_guard();
                        pqxx::work txn(guard.connection());

                        // Deactivate listing
                        marketRepo_->deactivateListing(txn, listingId);

                        // Calculate tax
                        int64_t tax = static_cast<int64_t>(listing->priceGold * MarketConstants::TAX_RATE);
                        int64_t sellerReceived = listing->priceGold - tax;

                        // WAL: record gold deduction before mutating
                        wal_.appendGoldChange(client->character_id, -listing->priceGold);
                        // Deduct buyer gold
                        inv->inventory.removeGold(listing->priceGold);

                        // Add item to buyer inventory
                        ItemInstance boughtItem;
                        boughtItem.instanceId   = listing->itemInstanceId;
                        boughtItem.itemId       = listing->itemId;
                        boughtItem.quantity      = listing->quantity;
                        boughtItem.enchantLevel = listing->enchantLevel;
                        boughtItem.rolledStats  = ItemStatRoller::parseRolledStats(listing->rolledStatsJson);
                        // Look up display info from item definition cache
                        if (auto* def = itemDefCache_.getDefinition(listing->itemId)) {
                            boughtItem.displayName = def->displayName;
                            boughtItem.rarity = parseItemRarity(def->rarity);
                        }
                        // WAL: record item add (slot=-1 = auto-slot; recovery matches by instanceId)
                        wal_.appendItemAdd(client->character_id, -1, boughtItem.instanceId);
                        inv->inventory.addItem(boughtItem);

                        // Credit seller gold (update DB directly — seller may be offline)
                        txn.exec_params(
                            "UPDATE characters SET gold = gold + $2 WHERE character_id = $1",
                            listing->sellerCharacterId, sellerReceived);

                        // Add tax to jackpot
                        txn.exec_params(
                            "UPDATE jackpot_pool SET current_pool = current_pool + $1, "
                            "last_updated_at = NOW() WHERE id = 1", tax);

                        txn.commit();

                        // Log transaction
                        marketRepo_->logTransaction(listingId, listing->sellerCharacterId,
                                                     listing->sellerCharacterName,
                                                     client->character_id, "",
                                                     listing->itemId, listing->itemName,
                                                     listing->quantity, listing->enchantLevel,
                                                     listing->rolledStatsJson,
                                                     listing->priceGold, tax, sellerReceived);

                        SvMarketResultMsg resp;
                        resp.action = MarketAction::BuyItem;
                        resp.resultCode = 0;
                        resp.listingId = listingId;
                        resp.message = "Purchased " + listing->itemName + " for " + std::to_string(listing->priceGold) + " gold";
                        uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                        resp.write(w);
                        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvMarketResult, buf, w.size());
                        sendPlayerState(clientId);
                        saveInventoryForClient(clientId);
                    } catch (const std::exception& ex) {
                        LOG_ERROR("Server", "Market buy failed: %s", ex.what());
                        sendMarketError("Purchase failed — please try again");
                    }
                    break;
                }
                case MarketAction::CancelListing: {
                    int32_t listingId = payload.readI32();
                    marketRepo_->cancelListing(listingId, client->character_id);
                    SvMarketResultMsg resp;
                    resp.action = MarketAction::CancelListing;
                    resp.resultCode = 0;
                    resp.listingId = listingId;
                    resp.message = "Listing cancelled";
                    uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                    resp.write(w);
                    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvMarketResult, buf, w.size());
                    break;
                }
                default:
                    LOG_WARN("Server", "Unknown market sub-action %d from client %d", subAction, clientId);
                    break;
            }
            break;
        }
        case PacketType::CmdBounty: {
            uint8_t subAction = payload.readU8();
            auto* client = server_.connections().findById(clientId);
            if (!client || client->playerEntityId == 0) break;

            switch (subAction) {
                case BountyAction::PlaceBounty: {
                    std::string targetCharId = payload.readString();
                    int64_t amount = detail::readI64(payload);

                    // Validate via BountyManager logic
                    int placerGuildId = guildRepo_->getPlayerGuildId(client->character_id);
                    int targetGuildId = guildRepo_->getPlayerGuildId(targetCharId);
                    int activeCount = bountyRepo_->getActiveBountyCount();
                    bool targetHasBounty = bountyRepo_->hasActiveBounty(targetCharId);

                    BountyResult canPlace = BountyManager::canPlaceBounty(
                        client->character_id, targetCharId, amount,
                        placerGuildId, targetGuildId, activeCount, targetHasBounty);

                    SvBountyUpdateMsg resp;
                    resp.updateType = 4; // result
                    if (canPlace != BountyResult::Success) {
                        resp.resultCode = static_cast<uint8_t>(canPlace);
                        resp.message = BountyManager::getResultMessage(canPlace, targetCharId);
                    } else {
                        BountyResult dbResult;
                        bountyRepo_->placeBounty(targetCharId, targetCharId,
                                                  client->character_id, "",
                                                  amount, dbResult);
                        resp.resultCode = static_cast<uint8_t>(dbResult);
                        resp.message = BountyManager::getResultMessage(dbResult, targetCharId);
                    }
                    uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                    resp.write(w);
                    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvBountyUpdate, buf, w.size());
                    break;
                }
                case BountyAction::CancelBounty: {
                    std::string targetCharId = payload.readString();
                    int64_t taxAmount = 0;
                    BountyResult dbResult;
                    int64_t refund = bountyRepo_->cancelContribution(
                        targetCharId, client->character_id, taxAmount, dbResult);

                    SvBountyUpdateMsg resp;
                    resp.updateType = 4;
                    resp.resultCode = static_cast<uint8_t>(dbResult);
                    resp.message = BountyManager::getResultMessage(dbResult, targetCharId);
                    uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                    resp.write(w);
                    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvBountyUpdate, buf, w.size());

                    // Refund gold if successful
                    if (dbResult == BountyResult::Success && refund > 0) {
                        PersistentId pid(client->playerEntityId);
                        EntityHandle h = replication_.getEntityHandle(pid);
                        Entity* e = world_.getEntity(h);
                        if (e) {
                            auto* inv = e->getComponent<InventoryComponent>();
                            if (inv) {
                                // WAL: record gold refund before mutating
                                wal_.appendGoldChange(client->character_id, refund);
                                inv->inventory.addGold(refund);
                                sendPlayerState(clientId);
                            }
                        }
                    }
                    break;
                }
                default:
                    LOG_WARN("Server", "Unknown bounty sub-action %d from client %d", subAction, clientId);
                    break;
            }
            break;
        }
        case PacketType::CmdGuild: {
            uint8_t subAction = payload.readU8();
            auto* client = server_.connections().findById(clientId);
            if (!client || client->playerEntityId == 0) break;

            PersistentId pid(client->playerEntityId);
            EntityHandle h = replication_.getEntityHandle(pid);
            Entity* e = world_.getEntity(h);
            if (!e) break;

            switch (subAction) {
                case GuildAction::Create: {
                    std::string guildName = payload.readString();
                    auto* inv = e->getComponent<InventoryComponent>();
                    if (!inv || inv->inventory.getGold() < GuildConstants::CREATION_COST) {
                        SvGuildUpdateMsg resp;
                        resp.updateType = 5; resp.resultCode = 1;
                        resp.message = "Not enough gold (need 100,000)";
                        uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                        resp.write(w);
                        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGuildUpdate, buf, w.size());
                        break;
                    }

                    GuildDbResult dbResult;
                    int guildId = guildRepo_->createGuild(guildName, client->character_id,
                                                           GuildConstants::DEFAULT_MAX_MEMBERS, dbResult);
                    SvGuildUpdateMsg resp;
                    if (dbResult == GuildDbResult::Success) {
                        // WAL: record gold deduction before mutating
                        wal_.appendGoldChange(client->character_id, -static_cast<int64_t>(GuildConstants::CREATION_COST));
                        inv->inventory.removeGold(GuildConstants::CREATION_COST);
                        auto* guildComp = e->getComponent<GuildComponent>();
                        if (guildComp) {
                            guildComp->guild.setGuildData(guildId, guildName, {},
                                                           GuildRank::Owner, 1);
                        }
                        resp.updateType = 0; resp.resultCode = 0;
                        resp.guildName = guildName;
                        resp.message = "Guild created!";
                        sendPlayerState(clientId);
                    } else {
                        resp.updateType = 5;
                        resp.resultCode = static_cast<uint8_t>(dbResult);
                        resp.message = dbResult == GuildDbResult::NameTaken ? "Guild name already taken" : "Failed to create guild";
                    }
                    uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                    resp.write(w);
                    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGuildUpdate, buf, w.size());
                    break;
                }
                case GuildAction::Leave: {
                    auto* guildComp = e->getComponent<GuildComponent>();
                    if (!guildComp || !guildComp->guild.isInGuild()) break;

                    GuildDbResult dbResult;
                    guildRepo_->removeMember(guildComp->guild.guildId, client->character_id, dbResult);
                    if (dbResult == GuildDbResult::Success) {
                        guildComp->guild.clearGuildData();
                        SvGuildUpdateMsg resp;
                        resp.updateType = 2; resp.resultCode = 0;
                        resp.message = "You left the guild";
                        uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                        resp.write(w);
                        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGuildUpdate, buf, w.size());
                    }
                    break;
                }
                default:
                    LOG_INFO("Server", "Guild sub-action %d from client %d (not yet implemented)", subAction, clientId);
                    break;
            }
            break;
        }
        case PacketType::CmdSocial: {
            uint8_t subAction = payload.readU8();
            auto* client = server_.connections().findById(clientId);
            if (!client || client->playerEntityId == 0) break;

            switch (subAction) {
                case SocialAction::SendFriendRequest: {
                    std::string targetCharId = payload.readString();
                    if (socialRepo_->isBlocked(targetCharId, client->character_id)) {
                        // Target has blocked us — silently fail
                        break;
                    }
                    socialRepo_->sendFriendRequest(client->character_id, targetCharId);
                    SvSocialUpdateMsg resp;
                    resp.updateType = 0; resp.resultCode = 0;
                    resp.message = "Friend request sent";
                    uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                    resp.write(w);
                    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvSocialUpdate, buf, w.size());
                    break;
                }
                case SocialAction::AcceptFriend: {
                    std::string fromCharId = payload.readString();
                    socialRepo_->acceptFriendRequest(client->character_id, fromCharId);
                    SvSocialUpdateMsg resp;
                    resp.updateType = 1; resp.resultCode = 0;
                    resp.message = "Friend request accepted";
                    uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                    resp.write(w);
                    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvSocialUpdate, buf, w.size());
                    break;
                }
                case SocialAction::RemoveFriend: {
                    std::string friendCharId = payload.readString();
                    socialRepo_->removeFriend(client->character_id, friendCharId);
                    break;
                }
                case SocialAction::BlockPlayer: {
                    std::string targetCharId = payload.readString();
                    socialRepo_->blockPlayer(client->character_id, targetCharId);
                    break;
                }
                case SocialAction::UnblockPlayer: {
                    std::string targetCharId = payload.readString();
                    socialRepo_->unblockPlayer(client->character_id, targetCharId);
                    break;
                }
                default:
                    LOG_INFO("Server", "Social sub-action %d from client %d (not yet implemented)", subAction, clientId);
                    break;
            }
            break;
        }
        case PacketType::CmdTrade: {
            uint8_t subAction = payload.readU8();
            auto* client = server_.connections().findById(clientId);
            if (!client || client->playerEntityId == 0) break;

            PersistentId pid(client->playerEntityId);
            EntityHandle h = replication_.getEntityHandle(pid);
            Entity* e = world_.getEntity(h);
            if (!e) break;
            auto* charStats = e->getComponent<CharacterStatsComponent>();

            auto sendTradeResult = [&](uint8_t type, uint8_t code, const std::string& msg) {
                SvTradeUpdateMsg resp;
                resp.updateType = type;
                resp.resultCode = code;
                resp.otherPlayerName = msg;
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvTradeUpdate, buf, w.size());
            };

            switch (subAction) {
                case TradeAction::Initiate: {
                    std::string targetCharId = payload.readString();

                    // Check if already in trade
                    if (tradeRepo_->isPlayerInTrade(client->character_id)) {
                        sendTradeResult(6, 1, "Already in a trade"); break;
                    }
                    if (tradeRepo_->isPlayerInTrade(targetCharId)) {
                        sendTradeResult(6, 2, "Target is already trading"); break;
                    }

                    // Get current scene
                    auto* sc = SceneManager::instance().currentScene();
                    std::string scene = sc ? sc->name() : "unknown";

                    int sessionId = tradeRepo_->createSession(client->character_id, targetCharId, scene);
                    if (sessionId > 0) {
                        sendTradeResult(1, 0, "Trade session started");
                        // Notify target player via system chat + trade invite
                        std::string senderName = charStats ? charStats->stats.characterName : "Someone";
                        server_.connections().forEach([&](ClientConnection& c) {
                            if (c.character_id == targetCharId) {
                                // System chat notification
                                SvChatMessageMsg chatMsg;
                                chatMsg.channel    = static_cast<uint8_t>(ChatChannel::System);
                                chatMsg.senderName = "[Trade]";
                                chatMsg.message    = senderName + " wants to trade with you.";
                                chatMsg.faction    = 0;
                                uint8_t chatBuf[512]; ByteWriter cw(chatBuf, sizeof(chatBuf));
                                chatMsg.write(cw);
                                server_.sendTo(c.clientId, Channel::ReliableOrdered,
                                               PacketType::SvChatMessage, chatBuf, cw.size());

                                // Trade invite update
                                SvTradeUpdateMsg invite;
                                invite.updateType = 0; // invited
                                invite.sessionId  = sessionId;
                                invite.otherPlayerName = senderName;
                                invite.resultCode = 0;
                                uint8_t tbuf[256]; ByteWriter tw(tbuf, sizeof(tbuf));
                                invite.write(tw);
                                server_.sendTo(c.clientId, Channel::ReliableOrdered,
                                               PacketType::SvTradeUpdate, tbuf, tw.size());
                            }
                        });
                    } else {
                        sendTradeResult(6, 3, "Failed to create trade session");
                    }
                    break;
                }
                case TradeAction::AddItem: {
                    uint8_t slotIdx = payload.readU8();
                    int32_t sourceSlot = payload.readI32();
                    std::string instanceId = payload.readString();
                    int32_t quantity = payload.readI32();

                    auto session = tradeRepo_->getActiveSession(client->character_id);
                    if (!session) { sendTradeResult(6, 1, "Not in a trade"); break; }

                    // Validate item exists and is tradeable
                    auto* inv = e->getComponent<InventoryComponent>();
                    if (!inv) break;
                    int invSlot = inv->inventory.findByInstanceId(instanceId);
                    if (invSlot < 0) { sendTradeResult(6, 4, "Item not found"); break; }
                    ItemInstance item = inv->inventory.getSlot(invSlot);
                    if (item.isBound()) { sendTradeResult(6, 5, "Item is soulbound"); break; }

                    tradeRepo_->addItemToTrade(session->sessionId, client->character_id,
                                                slotIdx, sourceSlot, instanceId, quantity);
                    // Unlock both sides when items change
                    tradeRepo_->unlockBothPlayers(session->sessionId);
                    sendTradeResult(2, 0, "Item added");
                    break;
                }
                case TradeAction::RemoveItem: {
                    uint8_t slotIdx = payload.readU8();
                    auto session = tradeRepo_->getActiveSession(client->character_id);
                    if (!session) break;
                    tradeRepo_->removeItemFromTrade(session->sessionId, client->character_id, slotIdx);
                    tradeRepo_->unlockBothPlayers(session->sessionId);
                    sendTradeResult(2, 0, "Item removed");
                    break;
                }
                case TradeAction::SetGold: {
                    int64_t gold = detail::readI64(payload);
                    auto session = tradeRepo_->getActiveSession(client->character_id);
                    if (!session) break;

                    auto* inv = e->getComponent<InventoryComponent>();
                    if (!inv || inv->inventory.getGold() < gold) {
                        sendTradeResult(6, 6, "Not enough gold"); break;
                    }

                    tradeRepo_->setPlayerGold(session->sessionId, client->character_id, gold);
                    tradeRepo_->unlockBothPlayers(session->sessionId);
                    sendTradeResult(2, 0, "Gold set");
                    break;
                }
                case TradeAction::Lock: {
                    auto session = tradeRepo_->getActiveSession(client->character_id);
                    if (!session) break;
                    tradeRepo_->setPlayerLocked(session->sessionId, client->character_id, true);
                    sendTradeResult(3, 0, "Locked");
                    break;
                }
                case TradeAction::Unlock: {
                    auto session = tradeRepo_->getActiveSession(client->character_id);
                    if (!session) break;
                    tradeRepo_->unlockBothPlayers(session->sessionId);
                    sendTradeResult(3, 0, "Unlocked");
                    break;
                }
                case TradeAction::Confirm: {
                    auto session = tradeRepo_->getActiveSession(client->character_id);
                    if (!session || !session->bothLocked()) {
                        sendTradeResult(6, 7, "Both players must lock first"); break;
                    }

                    tradeRepo_->setPlayerConfirmed(session->sessionId, client->character_id, true);

                    // Reload session to check if both confirmed
                    session = tradeRepo_->loadSession(session->sessionId);
                    if (session && session->bothConfirmed()) {
                        // Acquire both player locks in consistent address order to
                        // prevent deadlocks, then execute the trade atomically.
                        std::mutex& lockA = playerLocks_.get(session->playerACharacterId);
                        std::mutex& lockB = playerLocks_.get(session->playerBCharacterId);
                        std::scoped_lock tradeLock(
                            (&lockA < &lockB) ? lockA : lockB,
                            (&lockA < &lockB) ? lockB : lockA
                        );
                        // Execute trade atomically
                        try {
                            auto guard = dbPool_.acquire_guard();
                            pqxx::work txn(guard.connection());

                            // Get both sides' offers
                            auto offersA = tradeRepo_->getTradeOffers(session->sessionId, session->playerACharacterId);
                            auto offersB = tradeRepo_->getTradeOffers(session->sessionId, session->playerBCharacterId);

                            // Transfer items A→B
                            for (const auto& offer : offersA) {
                                tradeRepo_->transferItem(txn, offer.itemInstanceId, session->playerBCharacterId);
                            }
                            // Transfer items B→A
                            for (const auto& offer : offersB) {
                                tradeRepo_->transferItem(txn, offer.itemInstanceId, session->playerACharacterId);
                            }

                            // Transfer gold
                            if (session->playerAGold > 0) {
                                tradeRepo_->updateGold(txn, session->playerACharacterId, -session->playerAGold);
                                tradeRepo_->updateGold(txn, session->playerBCharacterId, session->playerAGold);
                            }
                            if (session->playerBGold > 0) {
                                tradeRepo_->updateGold(txn, session->playerBCharacterId, -session->playerBGold);
                                tradeRepo_->updateGold(txn, session->playerACharacterId, session->playerBGold);
                            }

                            // Complete session
                            tradeRepo_->completeSession(txn, session->sessionId);
                            txn.commit();

                            // Log history
                            tradeRepo_->logTradeHistory(session->sessionId,
                                session->playerACharacterId, session->playerBCharacterId,
                                session->playerAGold, session->playerBGold, "[]", "[]");

                            sendTradeResult(5, 0, "Trade completed!");
                            LOG_INFO("Server", "Trade %d completed: %s <-> %s",
                                     session->sessionId,
                                     session->playerACharacterId.c_str(),
                                     session->playerBCharacterId.c_str());
                            saveInventoryForClient(clientId);
                            // Save other player's inventory too
                            std::string otherCharId = (client->character_id == session->playerACharacterId)
                                ? session->playerBCharacterId : session->playerACharacterId;
                            server_.connections().forEach([&](ClientConnection& c) {
                                if (c.character_id == otherCharId) {
                                    saveInventoryForClient(c.clientId);
                                }
                            });
                        } catch (const std::exception& ex) {
                            LOG_ERROR("Server", "Trade execution failed: %s", ex.what());
                            sendTradeResult(6, 8, "Trade failed — please try again");
                        }
                    } else {
                        sendTradeResult(4, 0, "Confirmed — waiting for other player");
                    }
                    break;
                }
                case TradeAction::Cancel: {
                    auto session = tradeRepo_->getActiveSession(client->character_id);
                    if (session) {
                        tradeRepo_->cancelSession(session->sessionId);
                    }
                    sendTradeResult(6, 0, "Trade cancelled");
                    break;
                }
                default:
                    LOG_WARN("Server", "Unknown trade sub-action %d from client %d", subAction, clientId);
                    break;
            }
            break;
        }
        case PacketType::CmdGauntlet: {
            processGauntletCommand(clientId, payload);
            break;
        }
        case PacketType::CmdQuestAction: {
            uint8_t subAction = payload.readU8();
            std::string questIdStr = payload.readString();
            auto* client = server_.connections().findById(clientId);
            if (!client || client->playerEntityId == 0) break;

            PersistentId pid(client->playerEntityId);
            EntityHandle h = replication_.getEntityHandle(pid);
            Entity* e = world_.getEntity(h);
            if (!e) break;

            auto* questComp = e->getComponent<QuestComponent>();
            auto* charStats = e->getComponent<CharacterStatsComponent>();
            if (!questComp || !charStats) break;

            uint32_t questId = 0;
            try { questId = static_cast<uint32_t>(std::stoul(questIdStr)); }
            catch (...) { break; }

            switch (subAction) {
                case QuestAction::Accept: {
                    bool accepted = questComp->quests.acceptQuest(questId, charStats->stats.level);
                    if (accepted) {
                        questRepo_->saveQuestProgress(client->character_id, questIdStr, "active", 0, 1);
                        SvQuestUpdateMsg resp;
                        resp.updateType = 0;
                        resp.questId = questIdStr;
                        resp.message = "Quest accepted";
                        uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                        resp.write(w);
                        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvQuestUpdate, buf, w.size());
                    }
                    break;
                }
                case QuestAction::Abandon: {
                    questComp->quests.abandonQuest(questId);
                    questRepo_->abandonQuest(client->character_id, questIdStr);
                    SvQuestUpdateMsg resp;
                    resp.updateType = 3;
                    resp.questId = questIdStr;
                    resp.message = "Quest abandoned";
                    uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                    resp.write(w);
                    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvQuestUpdate, buf, w.size());
                    break;
                }
                case QuestAction::TurnIn: {
                    auto* inv = e->getComponent<InventoryComponent>();
                    if (!inv) break;
                    bool turnedIn = questComp->quests.turnInQuest(questId, charStats->stats, inv->inventory);
                    if (turnedIn) {
                        questRepo_->completeQuest(client->character_id, questIdStr);
                        SvQuestUpdateMsg resp;
                        resp.updateType = 2;
                        resp.questId = questIdStr;
                        resp.message = "Quest completed!";
                        uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                        resp.write(w);
                        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvQuestUpdate, buf, w.size());
                        sendPlayerState(clientId);
                    }
                    break;
                }
                default: break;
            }
            break;
        }
        case PacketType::CmdZoneTransition: {
            auto cmd = CmdZoneTransition::read(payload);
            auto* client = server_.connections().findById(clientId);
            if (!client || client->playerEntityId == 0) break;

            PersistentId pid(client->playerEntityId);
            EntityHandle h = replication_.getEntityHandle(pid);
            Entity* e = world_.getEntity(h);
            if (!e) break;

            // Level gate: check SceneCache for minimum level requirement
            const SceneInfoRecord* targetScene = sceneCache_.getByName(cmd.targetScene);
            if (targetScene) {
                auto* charStats = e->getComponent<CharacterStatsComponent>();
                if (charStats && charStats->stats.level < targetScene->minLevel) {
                    SvChatMessageMsg chatMsg;
                    chatMsg.channel = 6; // System channel
                    chatMsg.senderName = "[System]";
                    chatMsg.message = "You must be level " + std::to_string(targetScene->minLevel)
                                    + " to enter " + targetScene->sceneName;
                    chatMsg.faction = 0;
                    uint8_t buf[512]; ByteWriter w(buf, sizeof(buf));
                    chatMsg.write(w);
                    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvChatMessage, buf, w.size());
                    break; // block transition
                }
            }

            // Transition allowed — send SvZoneTransition back to client
            LOG_INFO("Server", "Client %d zone transition -> '%s'", clientId, cmd.targetScene.c_str());

            // Look up spawn position from scene cache
            float spawnX = 0.0f, spawnY = 0.0f;
            auto* targetSceneDef = sceneCache_.get(cmd.targetScene);
            if (targetSceneDef) {
                spawnX = targetSceneDef->defaultSpawnX;
                spawnY = targetSceneDef->defaultSpawnY;
            }

            SvZoneTransitionMsg resp;
            resp.targetScene = cmd.targetScene;
            resp.spawnX = spawnX;
            resp.spawnY = spawnY;
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            resp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvZoneTransition, buf, w.size());

            // Update player's current scene on the entity
            {
                auto* client = server_.connections().findById(clientId);
                if (client && client->playerEntityId != 0) {
                    PersistentId pid(client->playerEntityId);
                    EntityHandle h = replication_.getEntityHandle(pid);
                    Entity* e = world_.getEntity(h);
                    if (e) {
                        auto* sc = e->getComponent<CharacterStatsComponent>();
                        if (sc) sc->stats.currentScene = cmd.targetScene;
                    }

                    // Clear AOI state so the replication system sends fresh
                    // SvEntityEnter messages for all entities near the new position.
                    // Without this, entities in both old and new AOI stay in "stayed"
                    // and only get SvEntityUpdate (which the client ignores because
                    // it cleared its ghostEntities_ on zone transition).
                    client->aoi.previous.clear();
                    client->aoi.current.clear();
                    client->aoi.entered.clear();
                    client->aoi.left.clear();
                    client->aoi.stayed.clear();
                    client->lastAckedState.clear();
                }
            }

            // Update movement tracking for the new scene position
            lastValidPositions_[clientId] = {spawnX, spawnY};
            lastMoveTime_[clientId] = gameTime_;
            needsFirstMoveSync_.insert(clientId);

            // Save updated scene to DB asynchronously
            savePlayerToDBAsync(clientId);
            break;
        }
        case PacketType::CmdRespawn: {
            auto msg = CmdRespawnMsg::read(payload);
            auto* client = server_.connections().findById(clientId);
            if (!client || client->playerEntityId == 0) break;

            PersistentId pid(client->playerEntityId);
            EntityHandle h = replication_.getEntityHandle(pid);
            Entity* e = world_.getEntity(h);
            if (!e) break;

            auto* sc = e->getComponent<CharacterStatsComponent>();
            if (!sc) break;

            // Reject respawn if player is not actually dead (prevents double-respawn exploits)
            if (!sc->stats.isDead) {
                LOG_WARN("Server", "Client %d respawn rejected: not dead", clientId);
                break;
            }

            // Check timer for type 0 (town) and type 1 (map spawn)
            if (msg.respawnType <= 1 && sc->stats.respawnTimeRemaining > 0.0f) {
                LOG_WARN("Server", "Client %d respawn rejected: timer still %.1fs",
                         clientId, sc->stats.respawnTimeRemaining);
                break;
            }

            // Type 2: Phoenix Down — validate and consume
            if (msg.respawnType == 2) {
                auto* invComp = e->getComponent<InventoryComponent>();
                if (!invComp) break;
                int slot = invComp->inventory.findItemById("phoenix_down");
                if (slot < 0) {
                    LOG_WARN("Server", "Client %d tried Phoenix Down respawn but has none", clientId);
                    break;
                }
                invComp->inventory.removeItemQuantity(slot, 1);
                LOG_INFO("Server", "Client %d used Phoenix Down to respawn", clientId);
            }

            // Determine respawn position from DB-cached scene definitions
            auto* t = e->getComponent<Transform>();
            Vec2 respawnPos = t ? t->position : Vec2{0, 0};

            // Use player's actual currentScene (not SceneManager which is a client concept)
            std::string sceneName = sc->stats.currentScene.empty() ? "WhisperingWoods" : sc->stats.currentScene;

            if (msg.respawnType == 0 && sceneName != "Town") {
                // Town respawn from another scene — zone transition + respawn
                auto* townScene = sceneCache_.get("Town");
                float spX = townScene ? townScene->defaultSpawnX : 0.0f;
                float spY = townScene ? townScene->defaultSpawnY : 0.0f;

                if (sc->stats.isDead) sc->stats.respawn();
                sc->stats.currentScene = "Town";
                if (t) t->position = {spX, spY};
                lastValidPositions_[clientId] = {spX, spY};
                lastMoveTime_[clientId] = gameTime_;

                // Clear AOI so client gets fresh SvEntityEnter in the new scene
                client->aoi.previous.clear();
                client->aoi.current.clear();
                client->aoi.entered.clear();
                client->aoi.left.clear();
                client->aoi.stayed.clear();
                client->lastAckedState.clear();

                // Send zone transition to Town
                SvZoneTransitionMsg ztResp;
                ztResp.targetScene = "Town";
                ztResp.spawnX = spX;
                ztResp.spawnY = spY;
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                ztResp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvZoneTransition, buf, w.size());

                LOG_INFO("Server", "Client %d town-respawned via zone transition to Town at (%.0f, %.0f)",
                         clientId, spX, spY);
                savePlayerToDBAsync(clientId);
                break;
            } else if (msg.respawnType == 0) {
                // Already in Town — just respawn at Town's spawn point
                auto* townScene = sceneCache_.get("Town");
                if (townScene) {
                    respawnPos = {townScene->defaultSpawnX, townScene->defaultSpawnY};
                }
            } else if (msg.respawnType == 1) {
                // Map spawn — use current scene's default spawn from DB
                auto* scene = sceneCache_.get(sceneName);
                if (scene) {
                    respawnPos = {scene->defaultSpawnX, scene->defaultSpawnY};
                }
            }
            // Type 2 (Phoenix Down): respawnPos stays at death position

            // Execute respawn
            if (sc->stats.isDead) sc->stats.respawn();
            if (t) t->position = respawnPos;
            // Update movement tracking so server doesn't rubber-band after teleport
            lastValidPositions_[clientId] = respawnPos;
            lastMoveTime_[clientId] = gameTime_;

            // Send SvRespawnMsg to client
            SvRespawnMsg resp;
            resp.respawnType = msg.respawnType;
            resp.spawnX = respawnPos.x;
            resp.spawnY = respawnPos.y;
            uint8_t buf[32]; ByteWriter w(buf, sizeof(buf));
            resp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvRespawn, buf, w.size());

            LOG_INFO("Server", "Client %d respawned (type %d) at (%.0f, %.0f)",
                     clientId, msg.respawnType, respawnPos.x, respawnPos.y);
            break;
        }
        case PacketType::CmdUseSkill: {
            auto msg = CmdUseSkillMsg::read(payload);
            processUseSkill(clientId, msg);
            break;
        }
        case PacketType::CmdEquip: {
            auto msg = CmdEquipMsg::read(payload);
            processEquip(clientId, msg);
            break;
        }
        default:
            LOG_WARN("Server", "Unknown packet type 0x%02X from client %d", type, clientId);
            break;
    }
}

void ServerApp::processUseSkill(uint16_t clientId, const CmdUseSkillMsg& msg) {
    // Find caster's player entity
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    if (msg.targetId != 0) {
        if (!TargetValidator::isInAOI(client->aoi, msg.targetId, replication_)) {
            LOG_WARN("Net", "Client %u targeted entity %llu not in AOI", clientId, msg.targetId);
            return;
        }
    }

    // Per-tick skill command cap: silently drop excess skill commands
    skillCommandsThisTick_[clientId]++;
    if (skillCommandsThisTick_[clientId] > 1) {
        return; // Only 1 skill command per client per tick
    }

    PersistentId casterPid(client->playerEntityId);
    EntityHandle casterHandle = replication_.getEntityHandle(casterPid);
    Entity* caster = world_.getEntity(casterHandle);
    if (!caster) return;

    auto* skillComp = caster->getComponent<SkillManagerComponent>();
    if (!skillComp) {
        LOG_WARN("Server", "Client %d has no SkillManagerComponent", clientId);
        return;
    }

    auto* casterStatsComp = caster->getComponent<CharacterStatsComponent>();
    if (!casterStatsComp) return;

    // Check if caster is dead or dying
    if (!casterStatsComp->stats.isAlive()) {
        LOG_WARN("Server", "Client %d tried to use skill while dead", clientId);
        return;
    }

    auto* casterTransform = caster->getComponent<Transform>();
    if (!casterTransform) return;

    // Caster status effect and crowd control components
    auto* casterSEComp = caster->getComponent<StatusEffectComponent>();
    auto* casterCCComp = caster->getComponent<CrowdControlComponent>();

    // Build execution context
    SkillExecutionContext ctx;
    ctx.casterEntityId = casterHandle.value;
    ctx.casterStats = &casterStatsComp->stats;
    ctx.casterSEM = casterSEComp ? &casterSEComp->effects : nullptr;
    ctx.casterCC = casterCCComp ? &casterCCComp->cc : nullptr;

    // Find target if specified
    Entity* target = nullptr;
    PersistentId targetPid(msg.targetId);
    bool targetIsPlayer = false;
    bool targetIsBoss = false;

    if (msg.targetId != 0) {
        EntityHandle targetHandle = replication_.getEntityHandle(targetPid);
        target = world_.getEntity(targetHandle);
    }

    // Reject skill if target was specified but no longer exists (died/disconnected)
    if (msg.targetId != 0 && !target) {
        LOG_WARN("Server", "Client %d used skill on non-existent target %llu", clientId, msg.targetId);
        return;
    }

    if (target) {
        ctx.targetEntityId = target->handle().value;

        auto* targetTransform = target->getComponent<Transform>();
        if (targetTransform && casterTransform) {
            ctx.distanceToTarget = casterTransform->position.distance(targetTransform->position);
        }

        // Target status effects and CC
        auto* targetSEComp = target->getComponent<StatusEffectComponent>();
        auto* targetCCComp = target->getComponent<CrowdControlComponent>();
        ctx.targetSEM = targetSEComp ? &targetSEComp->effects : nullptr;
        ctx.targetCC = targetCCComp ? &targetCCComp->cc : nullptr;

        // Determine target type: mob or player
        auto* targetEnemyStats = target->getComponent<EnemyStatsComponent>();
        auto* targetCharStats = target->getComponent<CharacterStatsComponent>();

        if (targetEnemyStats) {
            // Target is a mob
            ctx.targetMobStats = &targetEnemyStats->stats;
            ctx.targetIsPlayer = false;
            ctx.targetLevel = targetEnemyStats->stats.level;
            ctx.targetArmor = targetEnemyStats->stats.armor;
            ctx.targetMagicResist = targetEnemyStats->stats.magicResist;
            ctx.targetCurrentHP = targetEnemyStats->stats.currentHP;
            ctx.targetMaxHP = targetEnemyStats->stats.maxHP;
            ctx.targetAlive = targetEnemyStats->stats.isAlive;

            // Check if target is a boss
            auto* mobNameplate = target->getComponent<MobNameplateComponent>();
            if (mobNameplate && mobNameplate->isBoss) {
                ctx.targetIsBoss = true;
                targetIsBoss = true;
            }
        } else if (targetCharStats) {
            // Target is a player — validate PvP rules
            bool inSameParty = false;
            auto* casterPartyComp = caster->getComponent<PartyComponent>();
            auto* targetPartyComp = target->getComponent<PartyComponent>();
            if (casterPartyComp && targetPartyComp
                && casterPartyComp->party.isInParty() && targetPartyComp->party.isInParty()
                && casterPartyComp->party.partyId == targetPartyComp->party.partyId) {
                inSameParty = true;
            }

            bool inSafeZone = !casterStatsComp->stats.isInPvPZone;

            if (!TargetValidator::canAttackPlayer(casterStatsComp->stats, targetCharStats->stats,
                                                  inSameParty, inSafeZone)) {
                return;
            }

            ctx.targetPlayerStats = &targetCharStats->stats;
            ctx.targetIsPlayer = true;
            targetIsPlayer = true;
            ctx.targetLevel = targetCharStats->stats.level;
            ctx.targetArmor = targetCharStats->stats.getArmor();
            ctx.targetMagicResist = targetCharStats->stats.getMagicResist();
            ctx.targetCurrentHP = targetCharStats->stats.currentHP;
            ctx.targetMaxHP = targetCharStats->stats.maxHP;
            ctx.targetAlive = targetCharStats->stats.isAlive();
        }
    }

    // Track whether the skill was a miss by hooking onSkillFailed temporarily
    bool wasMiss = false;
    auto prevOnFailed = skillComp->skills.onSkillFailed;
    skillComp->skills.onSkillFailed = [&wasMiss, &prevOnFailed](const std::string& id, std::string reason) {
        wasMiss = true;
        if (prevOnFailed) prevOnFailed(id, reason);
    };

    // Validate skill cooldown
    auto& clientCooldowns = skillCooldowns_[clientId];
    auto cooldownIt = clientCooldowns.find(msg.skillId);
    if (cooldownIt != clientCooldowns.end()) {
        const CachedSkillRank* rank = skillDefCache_.getRank(msg.skillId, msg.rank);
        float cooldown = rank ? rank->cooldownSeconds : 1.0f;
        if (gameTime_ - cooldownIt->second < cooldown * 0.8f) {
            LOG_DEBUG("Server", "Client %d skill '%s' rejected: cooldown (%.1f < %.1f)",
                      clientId, msg.skillId.c_str(), gameTime_ - cooldownIt->second, cooldown);
            return; // reject — too fast
        }
    }
    clientCooldowns[msg.skillId] = gameTime_;

    // Execute the skill
    int damage = skillComp->skills.executeSkill(msg.skillId, msg.rank, ctx);

    // Restore callback
    skillComp->skills.onSkillFailed = prevOnFailed;

    // Determine kill state and build hitFlags
    bool isKill = false;
    int32_t targetNewHP = 0;
    int32_t overkill = 0;
    if (target) {
        auto* tgtEnemyStats = target->getComponent<EnemyStatsComponent>();
        auto* tgtCharStats = target->getComponent<CharacterStatsComponent>();
        if (tgtEnemyStats) {
            isKill = !tgtEnemyStats->stats.isAlive;
            targetNewHP = tgtEnemyStats->stats.currentHP;
            if (isKill && damage > 0)
                overkill = damage - (ctx.targetCurrentHP > 0 ? ctx.targetCurrentHP : 0);
        } else if (tgtCharStats) {
            isKill = !tgtCharStats->stats.isAlive();
            targetNewHP = tgtCharStats->stats.currentHP;
            if (isKill && damage > 0)
                overkill = damage - (ctx.targetCurrentHP > 0 ? ctx.targetCurrentHP : 0);
        }
    }

    // Build hitFlags bitmask
    uint8_t hitFlags = 0;
    if (wasMiss && damage == 0) {
        hitFlags |= HitFlags::MISS;
    } else if (damage > 0) {
        hitFlags |= HitFlags::HIT;
    }
    if (isKill) hitFlags |= HitFlags::KILLED;
    // TODO: expose isCrit from executeSkill return value; for now crit flag not set

    // Resolve authoritative cooldown duration
    const CachedSkillRank* rankInfo = skillDefCache_.getRank(msg.skillId, msg.rank);
    uint16_t cooldownMs = 0;
    if (rankInfo) cooldownMs = static_cast<uint16_t>(rankInfo->cooldownSeconds * 1000.0f);

    // Build and broadcast SvSkillResultMsg
    SvSkillResultMsg result;
    result.casterId     = casterPid.value();
    result.targetId     = msg.targetId;
    result.skillId      = msg.skillId;
    result.damage       = damage;
    result.overkill     = (std::max)(0, static_cast<int>(overkill));
    result.targetNewHP  = targetNewHP;
    result.hitFlags     = hitFlags;
    result.resourceCost = 0; // resource already deducted inside executeSkill
    result.cooldownMs   = cooldownMs;
    result.casterNewMP  = static_cast<uint16_t>(casterStatsComp->stats.currentMP);

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    result.write(w);
    server_.broadcast(Channel::ReliableOrdered, PacketType::SvSkillResult, buf, w.size());

    // PK status transition: flag caster as aggressor if attacking an innocent player
    if (targetIsPlayer && damage > 0) {
        casterStatsComp->stats.enterCombat();
        auto* tgtCS = target->getComponent<CharacterStatsComponent>();
        if (tgtCS) {
            tgtCS->stats.enterCombat();
            // Attacker becomes Aggressor if target is innocent
            if (tgtCS->stats.pkStatus == PKStatus::White) {
                casterStatsComp->stats.flagAsAggressor();
            }
            // If attacker kills a non-flagged player → Murderer
            if (isKill && tgtCS->stats.pkStatus == PKStatus::White) {
                casterStatsComp->stats.flagAsMurderer();
            }
        }
    }

    // Handle mob death (XP, loot, etc.) — same pattern as processAction kill path
    if (isKill && target) {
        auto* targetEnemyStats = target->getComponent<EnemyStatsComponent>();
        if (targetEnemyStats) {
            EnemyStats& es = targetEnemyStats->stats;
            auto* targetTransform = target->getComponent<Transform>();
            Vec2 deathPos = targetTransform ? targetTransform->position : Vec2{0, 0};

            // Award XP to the killer (scaled by level gap)
            if (casterStatsComp) {
                int xp = XPCalculator::calculateXPReward(
                    es.xpReward, es.level, casterStatsComp->stats.level);
                if (xp > 0) {
                    // WAL: record XP gain before mutating
                    auto* casterClient = server_.connections().findById(clientId);
                    if (casterClient) wal_.appendXPGain(casterClient->character_id, static_cast<int64_t>(xp));
                    casterStatsComp->stats.addXP(xp);
                    LOG_INFO("Server", "Client %d gained %d XP from '%s' (base=%d, mob Lv%d, player Lv%d)",
                             clientId, xp, es.enemyName.c_str(), es.xpReward,
                             es.level, casterStatsComp->stats.level);
                } else {
                    LOG_INFO("Server", "Client %d: trivial mob '%s' Lv%d — no XP (player Lv%d)",
                             clientId, es.enemyName.c_str(), es.level, casterStatsComp->stats.level);
                }
            }

            // Determine top damager for loot ownership (party-aware)
            auto partyLookup = [this](uint32_t entityId) -> int {
                EntityHandle h(entityId);
                auto* entity = world_.getEntity(h);
                if (!entity) return -1;
                auto* pc = entity->getComponent<PartyComponent>();
                if (!pc || !pc->party.isInParty()) return -1;
                return pc->party.partyId;
            };
            auto lootResult = es.getTopDamagerPartyAware(partyLookup);
            uint32_t baseOwner = lootResult.topDamagerId;
            if (baseOwner == 0) baseOwner = casterHandle.value;

            std::string killScene;
            if (casterStatsComp) killScene = casterStatsComp->stats.currentScene;
            broadcastBossKillNotification(es, lootResult, killScene);

            // Prepare per-item random loot mode: collect all alive party members in scene
            std::vector<uint32_t> partyEntityIds;
            bool useRandomPerItem = false;
            if (lootResult.isParty) {
                EntityHandle topHandle(baseOwner);
                auto* topEntity = world_.getEntity(topHandle);
                if (topEntity) {
                    auto* pc = topEntity->getComponent<PartyComponent>();
                    if (pc && pc->party.lootMode == PartyLootMode::Random) {
                        useRandomPerItem = true;
                        auto sceneMembers = pc->party.getMembersInScene(es.sceneId);
                        for (const auto& charId : sceneMembers) {
                            server_.connections().forEach([&](const ClientConnection& c) {
                                if (c.character_id == charId && c.playerEntityId != 0)
                                    partyEntityIds.push_back(static_cast<uint32_t>(c.playerEntityId));
                            });
                        }
                    }
                    // FreeForAll: baseOwner stays as-is; pickup validation allows any party member
                }
            }

            // Per-item owner picker: random party member each time (Random mode),
            // or always baseOwner (FreeForAll / solo).
            thread_local std::mt19937 lootRng{std::random_device{}()};
            auto pickOwner = [&]() -> uint32_t {
                if (useRandomPerItem && !partyEntityIds.empty()) {
                    std::uniform_int_distribution<size_t> pick(0, partyEntityIds.size() - 1);
                    return partyEntityIds[pick(lootRng)];
                }
                return baseOwner;
            };

            // Roll loot table
            if (!es.lootTableId.empty()) {
                auto drops = lootTableCache_.rollLoot(es.lootTableId);

                constexpr float kItemSpacing = 10.0f;
                constexpr int kMaxPerRow = 4;
                thread_local std::mt19937 dropRng{std::random_device{}()};
                std::uniform_real_distribution<float> jitter(-3.0f, 3.0f);

                int totalDrops = static_cast<int>(drops.size());
                int cols = (std::min)(totalDrops, kMaxPerRow);
                float gridWidth = (cols - 1) * kItemSpacing;

                for (size_t i = 0; i < drops.size(); ++i) {
                    int col = static_cast<int>(i) % kMaxPerRow;
                    int row = static_cast<int>(i) / kMaxPerRow;
                    Vec2 offset = {
                        (col * kItemSpacing) - (gridWidth * 0.5f) + jitter(dropRng),
                        row * kItemSpacing + jitter(dropRng)
                    };
                    Vec2 dropPos = {deathPos.x + offset.x, deathPos.y + offset.y};

                    Entity* dropEntity = EntityFactory::createDroppedItem(world_, dropPos, false);
                    auto* dropComp = dropEntity->getComponent<DroppedItemComponent>();
                    if (dropComp) {
                        dropComp->itemId = drops[i].item.itemId;
                        dropComp->quantity = drops[i].item.quantity;
                        dropComp->enchantLevel = drops[i].item.enchantLevel;
                        dropComp->rolledStatsJson = ItemStatRoller::rolledStatsToJson(drops[i].item.rolledStats);
                        dropComp->ownerEntityId = pickOwner();  // random per item
                        dropComp->spawnTime = gameTime_;

                        const auto* def = itemDefCache_.getDefinition(drops[i].item.itemId);
                        if (def) dropComp->rarity = def->rarity;
                    }

                    PersistentId dropPid = PersistentId::generate(1);
                    replication_.registerEntity(dropEntity->handle(), dropPid);
                }
            }

            // Roll gold drop
            if (es.goldDropChance > 0.0f) {
                thread_local std::mt19937 goldRng{std::random_device{}()};
                std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
                if (chanceDist(goldRng) <= es.goldDropChance && es.maxGoldDrop > 0) {
                    std::uniform_int_distribution<int> goldDist(es.minGoldDrop, es.maxGoldDrop);
                    int goldAmount = goldDist(goldRng);

                    Entity* goldEntity = EntityFactory::createDroppedItem(world_, deathPos, true);
                    auto* goldComp = goldEntity->getComponent<DroppedItemComponent>();
                    if (goldComp) {
                        goldComp->isGold = true;
                        goldComp->goldAmount = goldAmount;
                        goldComp->ownerEntityId = pickOwner();  // random per item
                        goldComp->spawnTime = gameTime_;
                    }

                    PersistentId goldPid = PersistentId::generate(1);
                    replication_.registerEntity(goldEntity->handle(), goldPid);
                }
            }

            // Hide mob sprite (SpawnSystem handles respawn)
            auto* mobSprite = target->getComponent<SpriteComponent>();
            if (mobSprite) mobSprite->enabled = false;

            // Notify gauntlet of mob kill
            if (gauntletManager_.isPlayerInActiveInstance(static_cast<uint32_t>(clientId))) {
                auto* inst = gauntletManager_.getInstanceForPlayer(static_cast<uint32_t>(clientId));
                if (inst) {
                    bool isBoss = (es.monsterType == "Boss" || es.monsterType == "RaidBoss");
                    gauntletManager_.notifyMobKill(static_cast<uint32_t>(clientId),
                                                    es.level, isBoss, inst->divisionId);
                }
            }

            LOG_INFO("Server", "Client %d killed mob '%s' with skill '%s'",
                     clientId, es.enemyName.c_str(), msg.skillId.c_str());
        }

        // Handle player death (PvP)
        auto* targetCharStats = target->getComponent<CharacterStatsComponent>();
        if (targetCharStats && !targetCharStats->stats.isAlive()) {
            // Find which client owns the killed player entity
            uint16_t targetClientId = 0;
            server_.connections().forEach([&](const ClientConnection& conn) {
                if (conn.playerEntityId == msg.targetId) {
                    targetClientId = conn.clientId;
                }
            });
            if (targetClientId != 0) {
                SvDeathNotifyMsg deathMsg;
                deathMsg.deathSource = 1;  // PvP
                deathMsg.respawnTimer = 5.0f;
                deathMsg.xpLost = 0;
                deathMsg.honorLost = 0;

                uint8_t deathBuf[32];
                ByteWriter dw(deathBuf, sizeof(deathBuf));
                deathMsg.write(dw);
                server_.sendTo(targetClientId, Channel::ReliableOrdered,
                               PacketType::SvDeathNotify, deathBuf, dw.size());
            }
        }
    }

    // Send updated player state (HP/MP/Fury may have changed)
    sendPlayerState(clientId);

    LOG_INFO("Server", "Client %d used skill '%s' rank %d -> dmg=%d kill=%d miss=%d",
             clientId, msg.skillId.c_str(), msg.rank, damage, isKill ? 1 : 0,
             wasMiss ? 1 : 0);
}

void ServerApp::processAction(uint16_t clientId, const CmdAction& action) {
    // Find attacker's player entity
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    if (action.targetId != 0) {
        if (!TargetValidator::isInAOI(client->aoi, action.targetId, replication_)) {
            LOG_WARN("Net", "Client %u targeted entity %llu not in AOI", clientId, action.targetId);
            return;
        }
    }

    PersistentId attackerPid(client->playerEntityId);
    EntityHandle attackerHandle = replication_.getEntityHandle(attackerPid);
    Entity* attacker = world_.getEntity(attackerHandle);
    if (!attacker) return;

    // Find target entity
    PersistentId targetPid(action.targetId);
    EntityHandle targetHandle = replication_.getEntityHandle(targetPid);
    Entity* target = world_.getEntity(targetHandle);
    if (!target) {
        LOG_WARN("Server", "Client %d action on invalid target %llu", clientId, action.targetId);
        return;
    }

    if (action.actionType == 0) {
        // Basic attack
        auto* attackerTransform = attacker->getComponent<Transform>();
        auto* targetTransform = target->getComponent<Transform>();
        if (!attackerTransform || !targetTransform) return;

        // Validate range
        float attackRange = 1.0f; // default
        auto* charStats = attacker->getComponent<CharacterStatsComponent>();
        if (charStats) {
            attackRange = charStats->stats.classDef.attackRange;
        }

        // Validate player is alive
        if (charStats && !charStats->stats.isAlive()) return;

        float maxRange = attackRange * 32.0f + 16.0f;
        float dist = attackerTransform->position.distance(targetTransform->position);
        if (dist > maxRange) {
            LOG_WARN("Server", "Client %d attack out of range (%.1f > %.1f)", clientId, dist, maxRange);
            return;
        }

        // Check target is a living enemy
        auto* enemyStats = target->getComponent<EnemyStatsComponent>();

        // Validate auto-attack cooldown (shared for both PvE and PvP)
        float weaponSpeed = charStats ? charStats->stats.weaponAttackSpeed : 1.0f;
        float cooldown = (weaponSpeed > 0.0f) ? (1.0f / weaponSpeed) : 1.5f;
        auto lastIt = lastAutoAttackTime_.find(clientId);
        if (lastIt != lastAutoAttackTime_.end() && gameTime_ - lastIt->second < cooldown * 0.8f) return;
        lastAutoAttackTime_[clientId] = gameTime_;

        if (enemyStats) {
        if (!enemyStats->stats.isAlive) return;

        // Same-scene check: player and mob must be in the same scene
        if (charStats && !enemyStats->stats.sceneId.empty() &&
            charStats->stats.currentScene != enemyStats->stats.sceneId) return;

        // Calculate damage
        int damage = 10; // default
        bool isCrit = false;
        if (charStats) {
            damage = charStats->stats.calculateDamage(false, isCrit);
        }

        // Apply damage
        enemyStats->stats.takeDamageFrom(attackerHandle.value, damage);
        bool killed = !enemyStats->stats.isAlive;

        // Build and broadcast combat event
        SvCombatEventMsg evt;
        evt.attackerId = attackerPid.value();
        evt.targetId   = targetPid.value();
        evt.damage     = damage;
        evt.skillId    = action.skillId;
        evt.isCrit     = isCrit ? 1 : 0;
        evt.isKill     = killed ? 1 : 0;

        uint8_t buf[64];
        ByteWriter w(buf, sizeof(buf));
        evt.write(w);
        server_.broadcast(Channel::ReliableOrdered, PacketType::SvCombatEvent, buf, w.size());

        // Fury generation on auto-attack hit
        if (charStats && charStats->stats.classDef.primaryResource == ResourceType::Fury && damage > 0) {
            float furyGain = isCrit ? charStats->stats.classDef.furyPerCriticalHit
                                    : charStats->stats.classDef.furyPerBasicAttack;
            charStats->stats.addFury(furyGain);
            sendPlayerState(clientId);
        }

        if (killed) {
            auto* targetEnemyStats = target->getComponent<EnemyStatsComponent>();
            EnemyStats& es = targetEnemyStats->stats;
            Vec2 deathPos = targetTransform ? targetTransform->position : Vec2{0, 0};

            // Award XP to the killer (scaled by level gap)
            if (charStats) {
                int xp = XPCalculator::calculateXPReward(
                    es.xpReward, es.level, charStats->stats.level);
                if (xp > 0) {
                    // WAL: record XP gain before mutating
                    auto* attackerClient = server_.connections().findById(clientId);
                    if (attackerClient) wal_.appendXPGain(attackerClient->character_id, static_cast<int64_t>(xp));
                    charStats->stats.addXP(xp);
                    LOG_INFO("Server", "Client %d gained %d XP from '%s' (base=%d, mob Lv%d, player Lv%d)",
                             clientId, xp, es.enemyName.c_str(), es.xpReward,
                             es.level, charStats->stats.level);
                } else {
                    LOG_INFO("Server", "Client %d: trivial mob '%s' Lv%d — no XP (player Lv%d)",
                             clientId, es.enemyName.c_str(), es.level, charStats->stats.level);
                }
            }

            // Determine top damager for loot ownership (party-aware)
            auto partyLookup = [this](uint32_t entityId) -> int {
                EntityHandle h(entityId);
                auto* entity = world_.getEntity(h);
                if (!entity) return -1;
                auto* pc = entity->getComponent<PartyComponent>();
                if (!pc || !pc->party.isInParty()) return -1;
                return pc->party.partyId;
            };
            auto lootResult = es.getTopDamagerPartyAware(partyLookup);
            uint32_t baseOwner = lootResult.topDamagerId;
            if (baseOwner == 0) baseOwner = attackerHandle.value;

            std::string killScene;
            if (charStats) killScene = charStats->stats.currentScene;
            broadcastBossKillNotification(es, lootResult, killScene);

            // Prepare per-item random loot mode: collect all alive party members in scene
            std::vector<uint32_t> partyEntityIds;
            bool useRandomPerItem = false;
            if (lootResult.isParty) {
                EntityHandle topHandle(baseOwner);
                auto* topEntity = world_.getEntity(topHandle);
                if (topEntity) {
                    auto* pc = topEntity->getComponent<PartyComponent>();
                    if (pc && pc->party.lootMode == PartyLootMode::Random) {
                        useRandomPerItem = true;
                        auto sceneMembers = pc->party.getMembersInScene(es.sceneId);
                        for (const auto& charId : sceneMembers) {
                            server_.connections().forEach([&](const ClientConnection& c) {
                                if (c.character_id == charId && c.playerEntityId != 0)
                                    partyEntityIds.push_back(static_cast<uint32_t>(c.playerEntityId));
                            });
                        }
                    }
                    // FreeForAll: baseOwner stays as-is; pickup validation allows any party member
                }
            }

            // Per-item owner picker: random party member each time (Random mode),
            // or always baseOwner (FreeForAll / solo).
            thread_local std::mt19937 lootRng{std::random_device{}()};
            auto pickOwner = [&]() -> uint32_t {
                if (useRandomPerItem && !partyEntityIds.empty()) {
                    std::uniform_int_distribution<size_t> pick(0, partyEntityIds.size() - 1);
                    return partyEntityIds[pick(lootRng)];
                }
                return baseOwner;
            };

            // Roll loot table
            if (!es.lootTableId.empty()) {
                auto drops = lootTableCache_.rollLoot(es.lootTableId);

                constexpr float kItemSpacing = 10.0f;
                constexpr int kMaxPerRow = 4;
                thread_local std::mt19937 dropRng{std::random_device{}()};
                std::uniform_real_distribution<float> jitter(-3.0f, 3.0f);

                int totalDrops = static_cast<int>(drops.size());
                int cols = (std::min)(totalDrops, kMaxPerRow);
                float gridWidth = (cols - 1) * kItemSpacing;

                for (size_t i = 0; i < drops.size(); ++i) {
                    int col = static_cast<int>(i) % kMaxPerRow;
                    int row = static_cast<int>(i) / kMaxPerRow;
                    Vec2 offset = {
                        (col * kItemSpacing) - (gridWidth * 0.5f) + jitter(dropRng),
                        row * kItemSpacing + jitter(dropRng)
                    };
                    Vec2 dropPos = {deathPos.x + offset.x, deathPos.y + offset.y};

                    Entity* dropEntity = EntityFactory::createDroppedItem(world_, dropPos, false);
                    auto* dropComp = dropEntity->getComponent<DroppedItemComponent>();
                    if (dropComp) {
                        dropComp->itemId = drops[i].item.itemId;
                        dropComp->quantity = drops[i].item.quantity;
                        dropComp->enchantLevel = drops[i].item.enchantLevel;
                        dropComp->rolledStatsJson = ItemStatRoller::rolledStatsToJson(drops[i].item.rolledStats);
                        dropComp->ownerEntityId = pickOwner();  // random per item
                        dropComp->spawnTime = gameTime_;

                        const auto* def = itemDefCache_.getDefinition(drops[i].item.itemId);
                        if (def) dropComp->rarity = def->rarity;
                    }

                    PersistentId dropPid = PersistentId::generate(1);
                    replication_.registerEntity(dropEntity->handle(), dropPid);
                }
            }

            // Roll gold drop
            if (es.goldDropChance > 0.0f) {
                thread_local std::mt19937 goldRng{std::random_device{}()};
                std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
                if (chanceDist(goldRng) <= es.goldDropChance && es.maxGoldDrop > 0) {
                    std::uniform_int_distribution<int> goldDist(es.minGoldDrop, es.maxGoldDrop);
                    int goldAmount = goldDist(goldRng);

                    Entity* goldEntity = EntityFactory::createDroppedItem(world_, deathPos, true);
                    auto* goldComp = goldEntity->getComponent<DroppedItemComponent>();
                    if (goldComp) {
                        goldComp->isGold = true;
                        goldComp->goldAmount = goldAmount;
                        goldComp->ownerEntityId = pickOwner();  // random per item
                        goldComp->spawnTime = gameTime_;
                    }

                    PersistentId goldPid = PersistentId::generate(1);
                    replication_.registerEntity(goldEntity->handle(), goldPid);
                }
            }

            // Hide mob sprite (SpawnSystem handles respawn)
            auto* mobSprite = target->getComponent<SpriteComponent>();
            if (mobSprite) mobSprite->enabled = false;

            // Notify gauntlet of mob kill (if player is in an active instance)
            if (gauntletManager_.isPlayerInActiveInstance(static_cast<uint32_t>(clientId))) {
                // Find which division the player is in
                auto* inst = gauntletManager_.getInstanceForPlayer(static_cast<uint32_t>(clientId));
                if (inst) {
                    bool isBoss = (es.monsterType == "Boss" || es.monsterType == "RaidBoss");
                    gauntletManager_.notifyMobKill(static_cast<uint32_t>(clientId),
                                                    es.level, isBoss, inst->divisionId);
                }
            }

            LOG_INFO("Server", "Client %d killed mob '%s'", clientId, es.enemyName.c_str());
        }

        // Send updated player state (XP, HP may have changed)
        sendPlayerState(clientId);
        } else {
        // PvP auto-attack: target is another player
        auto* targetCharStats = target->getComponent<CharacterStatsComponent>();
        if (targetCharStats) {
            // Determine party membership for PvP validation
            bool inSameParty = false;
            auto* attackerPartyComp = attacker->getComponent<PartyComponent>();
            auto* targetPartyComp = target->getComponent<PartyComponent>();
            if (attackerPartyComp && targetPartyComp
                && attackerPartyComp->party.isInParty() && targetPartyComp->party.isInParty()
                && attackerPartyComp->party.partyId == targetPartyComp->party.partyId) {
                inSameParty = true;
            }

            // Determine safe zone status
            bool inSafeZone = !charStats->stats.isInPvPZone;

            // Full PvP target validation (faction, party, safe zone, PK status, alive)
            if (!TargetValidator::canAttackPlayer(charStats->stats, targetCharStats->stats,
                                                  inSameParty, inSafeZone)) return;

            // Same-scene check
            if (charStats && !charStats->stats.currentScene.empty() &&
                charStats->stats.currentScene != targetCharStats->stats.currentScene) return;

            // Calculate PvP damage using shared formulas
            int damage = 10;
            bool isCrit = false;
            if (charStats) {
                damage = charStats->stats.calculateDamage(false, isCrit);
            }

            // Apply PvP damage multiplier (0.05x)
            damage = static_cast<int>(std::round(damage * CombatSystem::getPvPDamageMultiplier()));
            if (damage < 1) damage = 1;

            // Apply armor reduction on target
            damage = CombatSystem::applyArmorReduction(damage, targetCharStats->stats.getArmor());

            // Apply damage
            targetCharStats->stats.takeDamage(damage);
            bool killed = !targetCharStats->stats.isAlive();

            // PK status transitions for auto-attacks (same as skill path)
            if (charStats) {
                charStats->stats.enterCombat();
                targetCharStats->stats.enterCombat();
                if (targetCharStats->stats.pkStatus == PKStatus::White) {
                    charStats->stats.flagAsAggressor();
                }
                if (killed && targetCharStats->stats.pkStatus == PKStatus::White) {
                    charStats->stats.flagAsMurderer();
                }
            }

            // Broadcast SvCombatEvent
            SvCombatEventMsg pvpEvt;
            pvpEvt.attackerId = attackerPid.value();
            pvpEvt.targetId   = targetPid.value();
            pvpEvt.damage     = damage;
            pvpEvt.skillId    = 0;
            pvpEvt.isCrit     = isCrit ? 1 : 0;
            pvpEvt.isKill     = killed ? 1 : 0;
            uint8_t pvpBuf[64];
            ByteWriter pvpW(pvpBuf, sizeof(pvpBuf));
            pvpEvt.write(pvpW);
            server_.broadcast(Channel::ReliableOrdered, PacketType::SvCombatEvent, pvpBuf, pvpW.size());

            // Fury generation on auto-attack hit
            if (charStats && charStats->stats.classDef.primaryResource == ResourceType::Fury && damage > 0) {
                float furyGain = isCrit ? charStats->stats.classDef.furyPerCriticalHit
                                        : charStats->stats.classDef.furyPerBasicAttack;
                charStats->stats.addFury(furyGain);
                sendPlayerState(clientId);
            }

            if (killed) {
                // Award PvP kill to attacker
                if (charStats) {
                    charStats->stats.pvpKills++;
                }
                // Record PvP death on target
                targetCharStats->stats.pvpDeaths++;

                // Send death notification to the killed player
                uint16_t targetClientId = 0;
                server_.connections().forEach([&](const ClientConnection& conn) {
                    if (conn.playerEntityId == targetPid.value()) targetClientId = conn.clientId;
                });
                if (targetClientId != 0) {
                    SvDeathNotifyMsg deathMsg;
                    deathMsg.deathSource = 1; // PvP
                    deathMsg.xpLost = 0;
                    deathMsg.honorLost = 0;
                    deathMsg.respawnTimer = 5.0f;
                    uint8_t dbuf[64];
                    ByteWriter dw(dbuf, sizeof(dbuf));
                    deathMsg.write(dw);
                    server_.sendTo(targetClientId, Channel::ReliableOrdered,
                                   PacketType::SvDeathNotify, dbuf, dw.size());
                }

                LOG_INFO("Server", "Client %d killed player in PvP", clientId);
            }

            sendPlayerState(clientId);
        }
        } // end else (PvP branch)
    } else if (action.actionType == 3) {
        // Pickup
        PersistentId itemPid(action.targetId);
        EntityHandle itemHandle = replication_.getEntityHandle(itemPid);
        Entity* itemEntity = world_.getEntity(itemHandle);
        if (!itemEntity) return;

        auto* dropComp = itemEntity->getComponent<DroppedItemComponent>();
        if (!dropComp) return;

        // Validate proximity
        auto* playerT = attacker->getComponent<Transform>();
        auto* itemT = itemEntity->getComponent<Transform>();
        if (!playerT || !itemT) return;
        float dist = playerT->position.distance(itemT->position);
        if (dist > 48.0f) return;

        // Validate loot rights — allow owner always; allow party members only in FreeForAll mode
        if (dropComp->ownerEntityId != 0 && dropComp->ownerEntityId != attackerHandle.value) {
            bool sameParty = false;
            auto* attackerParty = attacker->getComponent<PartyComponent>();
            if (attackerParty && attackerParty->party.isInParty()) {
                EntityHandle ownerHandle(dropComp->ownerEntityId);
                auto* ownerEntity = world_.getEntity(ownerHandle);
                if (ownerEntity) {
                    auto* ownerParty = ownerEntity->getComponent<PartyComponent>();
                    if (ownerParty && ownerParty->party.isInParty()
                        && ownerParty->party.partyId == attackerParty->party.partyId
                        && attackerParty->party.lootMode == PartyLootMode::FreeForAll) {
                        sameParty = true;
                    }
                }
            }
            if (!sameParty) return;
        }

        // Claim the drop atomically (single-threaded: simple bool flag prevents
        // two pickups of the same item in the same tick)
        if (!dropComp->tryClaim(attackerHandle.value)) {
            return; // Already claimed by another player this tick
        }

        // Process pickup
        auto* inv = attacker->getComponent<InventoryComponent>();
        if (!inv) return;

        SvLootPickupMsg pickupMsg;

        if (dropComp->isGold) {
            // WAL: record gold pickup before mutating
            {
                auto* lootClient = server_.connections().findById(clientId);
                if (lootClient) wal_.appendGoldChange(lootClient->character_id, static_cast<int64_t>(dropComp->goldAmount));
            }
            inv->inventory.addGold(dropComp->goldAmount);
            pickupMsg.isGold = 1;
            pickupMsg.goldAmount = dropComp->goldAmount;
            pickupMsg.displayName = "Gold";
        } else {
            const auto* def = itemDefCache_.getDefinition(dropComp->itemId);

            ItemInstance item;
            item.instanceId = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
            item.itemId = dropComp->itemId;
            item.quantity = dropComp->quantity;
            item.enchantLevel = dropComp->enchantLevel;
            item.rolledStats = ItemStatRoller::parseRolledStats(dropComp->rolledStatsJson);
            item.rarity = parseItemRarity(dropComp->rarity);
            item.displayName = def ? def->displayName : dropComp->itemId;
            // WAL: record item pickup before mutating (slot=-1 = auto-slot)
            {
                auto* lootClient = server_.connections().findById(clientId);
                if (lootClient) wal_.appendItemAdd(lootClient->character_id, -1, item.instanceId);
            }
            if (!inv->inventory.addItem(item)) {
                dropComp->releaseClaim();
                return;
            }

            pickupMsg.itemId = dropComp->itemId;
            pickupMsg.quantity = dropComp->quantity;
            pickupMsg.rarity = dropComp->rarity;
            pickupMsg.displayName = def ? def->displayName : dropComp->itemId;
            if (dropComp->enchantLevel > 0) {
                pickupMsg.displayName += " +" + std::to_string(dropComp->enchantLevel);
            }
        }

        // Send pickup notification
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        pickupMsg.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvLootPickup, buf, w.size());

        sendPlayerState(clientId);

        // Destroy the dropped item
        replication_.unregisterEntity(itemHandle);
        world_.destroyEntity(itemHandle);
    } else {
        LOG_INFO("Server", "Unhandled action type %d from client %d", action.actionType, clientId);
    }
}

void ServerApp::recalcEquipmentBonuses(Entity* player) {
    auto* charStats = player->getComponent<CharacterStatsComponent>();
    auto* inv = player->getComponent<InventoryComponent>();
    if (!charStats || !inv) return;

    charStats->stats.clearEquipmentBonuses();

    for (const auto& [slot, item] : inv->inventory.getEquipmentMap()) {
        if (!item.isValid()) continue;

        // Look up base stats from item definition
        const auto* def = itemDefCache_.getDefinition(item.itemId);
        int baseWeaponMin = 0, baseWeaponMax = 0, baseArmor = 0;
        float baseAttackSpeed = 0.0f;
        if (def) {
            baseWeaponMin = def->damageMin;
            baseWeaponMax = def->damageMax;
            baseArmor = def->armor;
            baseAttackSpeed = def->getFloatAttribute("attack_speed", 0.0f);
        }

        charStats->stats.applyItemBonuses(item, baseWeaponMin, baseWeaponMax,
                                           baseArmor, baseAttackSpeed);
    }

    charStats->stats.recalculateStats();

    // Clamp HP/MP to new max (unequipping can lower maxHP below currentHP)
    if (charStats->stats.currentHP > charStats->stats.maxHP)
        charStats->stats.currentHP = charStats->stats.maxHP;
    if (charStats->stats.currentMP > charStats->stats.maxMP)
        charStats->stats.currentMP = charStats->stats.maxMP;
}

void ServerApp::processEquip(uint16_t clientId, const CmdEquipMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* player = world_.getEntity(h);
    if (!player) return;

    auto* charStats = player->getComponent<CharacterStatsComponent>();
    auto* inv = player->getComponent<InventoryComponent>();
    if (!charStats || !inv) return;

    // Block equipment changes during combat
    if (charStats->stats.isInCombat()) {
        LOG_WARN("Server", "Client %d tried to change equipment in combat", clientId);
        return;
    }

    // Block equipment changes while dead or dying
    if (!charStats->stats.isAlive()) return;

    auto targetSlot = static_cast<EquipmentSlot>(msg.equipSlot);

    bool success = false;
    if (msg.action == 0) {
        // Equip
        success = inv->inventory.equipItem(msg.inventorySlot, targetSlot);
    } else {
        // Unequip
        success = inv->inventory.unequipItem(targetSlot);
    }

    if (success) {
        recalcEquipmentBonuses(player);
        sendPlayerState(clientId);
        sendInventorySync(clientId);
        LOG_INFO("Server", "Client %d %s slot %d",
                 clientId, msg.action == 0 ? "equipped" : "unequipped", msg.equipSlot);
    }
}

void ServerApp::sendPlayerState(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* e = world_.getEntity(h);
    if (!e) return;

    auto* charStats = e->getComponent<CharacterStatsComponent>();
    if (!charStats) return;

    const auto& s = charStats->stats;

    // Get gold from inventory if available
    int64_t gold = 0;
    auto* inv = e->getComponent<InventoryComponent>();
    if (inv) gold = inv->inventory.getGold();

    SvPlayerStateMsg msg;
    msg.currentHP   = s.currentHP;
    msg.maxHP       = s.maxHP;
    msg.currentMP   = s.currentMP;
    msg.maxMP       = s.maxMP;
    msg.currentFury = s.currentFury;
    msg.currentXP   = s.currentXP;
    msg.gold        = gold;
    msg.level       = s.level;
    msg.honor       = s.honor;
    msg.pvpKills    = s.pvpKills;
    msg.pvpDeaths   = s.pvpDeaths;

    // Derived stats (server-authoritative snapshot)
    msg.armor       = s.getArmor();
    msg.magicResist = s.getMagicResist();
    msg.critRate    = s.getCritRate();
    msg.hitRate     = s.getHitRate();
    msg.evasion     = s.getEvasion();
    msg.speed       = s.getSpeed();
    msg.damageMult  = s.getDamageMultiplier();
    msg.pkStatus    = static_cast<uint8_t>(s.pkStatus);

    uint8_t buf[128];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvPlayerState, buf, w.size());
}

void ServerApp::sendSkillSync(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* e = world_.getEntity(h);
    if (!e) return;

    auto* skillComp = e->getComponent<SkillManagerComponent>();
    if (!skillComp) return;

    SvSkillSyncMsg msg;
    for (const auto& learned : skillComp->skills.getLearnedSkills()) {
        SkillSyncEntry entry;
        entry.skillId       = learned.skillId;
        entry.unlockedRank  = static_cast<uint8_t>(learned.unlockedRank);
        entry.activatedRank = static_cast<uint8_t>(learned.activatedRank);
        msg.skills.push_back(entry);
    }
    msg.skillBar.resize(20);
    for (int i = 0; i < 20; ++i) {
        msg.skillBar[i] = skillComp->skills.getSkillInSlot(i);
    }

    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvSkillSync, buf, w.size());
}

void ServerApp::sendQuestSync(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* e = world_.getEntity(h);
    if (!e) return;

    auto* questComp = e->getComponent<QuestComponent>();
    if (!questComp) return;

    SvQuestSyncMsg msg;
    for (const auto& aq : questComp->quests.getActiveQuests()) {
        QuestSyncEntry entry;
        entry.questId = std::to_string(aq.questId);
        entry.state   = 0; // active
        for (uint16_t progress : aq.objectiveProgress) {
            entry.objectives.push_back({static_cast<int32_t>(progress), 0});
        }
        msg.quests.push_back(std::move(entry));
    }

    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvQuestSync, buf, w.size());
}

void ServerApp::sendInventorySync(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* e = world_.getEntity(h);
    if (!e) return;

    auto* invComp = e->getComponent<InventoryComponent>();
    if (!invComp) return;

    SvInventorySyncMsg msg;

    const auto& slots = invComp->inventory.getSlots();
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        const auto& item = slots[i];
        if (!item.isValid()) continue;
        InventorySyncSlot s;
        s.slotIndex    = i;
        s.itemId       = item.itemId;
        s.quantity     = item.quantity;
        s.enchantLevel = item.enchantLevel;
        s.rolledStats  = ItemStatRoller::rolledStatsToJson(item.rolledStats);
        if (item.hasSocket()) {
            switch (item.socket.statType) {
                case StatType::Strength:     s.socketStat = "STR"; break;
                case StatType::Dexterity:    s.socketStat = "DEX"; break;
                case StatType::Intelligence: s.socketStat = "INT"; break;
                default: break;
            }
            s.socketValue = item.socket.value;
        }
        msg.slots.push_back(std::move(s));
    }

    for (const auto& [slot, item] : invComp->inventory.getEquipmentMap()) {
        if (!item.isValid()) continue;
        InventorySyncEquip eq;
        eq.slot        = static_cast<uint8_t>(slot);
        eq.itemId      = item.itemId;
        eq.quantity    = item.quantity;
        eq.enchantLevel = item.enchantLevel;
        eq.rolledStats  = ItemStatRoller::rolledStatsToJson(item.rolledStats);
        if (item.hasSocket()) {
            switch (item.socket.statType) {
                case StatType::Strength:     eq.socketStat = "STR"; break;
                case StatType::Dexterity:    eq.socketStat = "DEX"; break;
                case StatType::Intelligence: eq.socketStat = "INT"; break;
                default: break;
            }
            eq.socketValue = item.socket.value;
        }
        msg.equipment.push_back(std::move(eq));
    }

    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvInventorySync, buf, w.size());
}

void ServerApp::tickAutoSave(float /*dt*/) {
    server_.connections().forEach([this](ClientConnection& c) {
        auto it = nextAutoSaveTime_.find(c.clientId);
        if (it == nextAutoSaveTime_.end()) return;
        if (gameTime_ < it->second) return;

        // Stagger next save
        it->second = gameTime_ + AUTO_SAVE_INTERVAL;

        // Dispatch save to worker fiber (non-blocking)
        uint16_t clientId = c.clientId;
        savePlayerToDBAsync(clientId);
    });

    // Truncate WAL after successful auto-save cycle — keeps the file tiny
    wal_.truncate();
}

void ServerApp::tickMaintenance(float dt) {
    // Market listing expiry
    marketExpiryTimer_ += dt;
    if (marketExpiryTimer_ >= MARKET_EXPIRY_INTERVAL) {
        marketExpiryTimer_ = 0.0f;
        int expired = marketRepo_->deactivateExpiredListings();
        if (expired > 0) {
            LOG_INFO("Server", "Deactivated %d expired market listings", expired);
        }
    }

    // Bounty expiry
    bountyExpiryTimer_ += dt;
    if (bountyExpiryTimer_ >= BOUNTY_EXPIRY_INTERVAL) {
        bountyExpiryTimer_ = 0.0f;
        auto refunds = bountyRepo_->processExpiredBounties();
        for (const auto& r : refunds) {
            // Try to refund online players directly
            // (Offline refunds would need a separate offline_gold table — deferred)
            LOG_INFO("Server", "Bounty expired: refunding %lld gold to %s",
                     r.refund, r.contributorCharId.c_str());
        }
    }

    // Stale trade session cleanup
    tradeCleanupTimer_ += dt;
    if (tradeCleanupTimer_ >= TRADE_CLEANUP_INTERVAL) {
        tradeCleanupTimer_ = 0.0f;
        int cleaned = tradeRepo_->cleanStaleSessions(30);
        if (cleaned > 0) {
            LOG_INFO("Server", "Cleaned %d stale trade sessions", cleaned);
        }
    }

    // Expire stale economic nonces (>60s old)
    nonceManager_.expireAll(gameTime_);
}

// ============================================================================
// Gauntlet Integration
// ============================================================================

void ServerApp::initGauntlet() {
    // Try loading gauntlet config from DB
    try {
        pqxx::work txn(gameDbConn_.connection());

        // Load division configs
        auto divResult = txn.exec(
            "SELECT division_id, division_name, min_level, max_level, arena_scene_name, "
            "wave_count, seconds_between_waves, respawn_seconds, "
            "team_spawn_a_x, team_spawn_a_y, team_spawn_b_x, team_spawn_b_y, "
            "min_players_to_start, max_players_per_team "
            "FROM gauntlet_config ORDER BY division_id");

        for (const auto& row : divResult) {
            GauntletDivisionSettings settings;
            settings.divisionId       = row["division_id"].as<int>();
            settings.divisionName     = row["division_name"].as<std::string>();
            settings.minLevel         = row["min_level"].as<int>();
            settings.maxLevel         = row["max_level"].as<int>();
            settings.arenaSceneName   = row["arena_scene_name"].as<std::string>();
            settings.waveBreakSeconds = static_cast<float>(row["seconds_between_waves"].as<int>());
            settings.playerRespawnSeconds = static_cast<float>(row["respawn_seconds"].as<int>());
            settings.teamASpawnPoint  = {row["team_spawn_a_x"].as<float>(), row["team_spawn_a_y"].as<float>()};
            settings.teamBSpawnPoint  = {row["team_spawn_b_x"].as<float>(), row["team_spawn_b_y"].as<float>()};
            settings.minPlayersToStart = row["min_players_to_start"].as<int>();
            settings.maxPlayersPerTeam = row["max_players_per_team"].as<int>();

            // Load wave configs for this division
            auto waveResult = txn.exec_params(
                "SELECT wave_number, mob_def_id, mob_count, spawn_delay_seconds, is_boss, bonus_points "
                "FROM gauntlet_waves WHERE division_id = $1 ORDER BY wave_number",
                settings.divisionId);

            for (const auto& wRow : waveResult) {
                int waveNum = wRow["wave_number"].as<int>();
                bool isBoss = wRow["is_boss"].as<bool>();

                if (isBoss) {
                    BossSpawnConfig boss;
                    boss.mobDefId          = wRow["mob_def_id"].as<std::string>();
                    boss.spawnDelaySeconds = wRow["spawn_delay_seconds"].is_null() ? 0.0f : wRow["spawn_delay_seconds"].as<float>();
                    boss.bonusPoints       = wRow["bonus_points"].is_null() ? 50 : wRow["bonus_points"].as<int>();
                    settings.bossSpawns.push_back(std::move(boss));
                } else {
                    BasicWaveConfig wave;
                    wave.waveNumber         = waveNum;
                    wave.maxMobsToSpawn     = wRow["mob_count"].is_null() ? 20 : wRow["mob_count"].as<int>();
                    wave.spawnIntervalSeconds = wRow["spawn_delay_seconds"].is_null() ? 3.0f : wRow["spawn_delay_seconds"].as<float>();

                    // Also store mob mapping
                    LevelMobMapping mapping;
                    mapping.level    = settings.minLevel;
                    mapping.mobDefId = wRow["mob_def_id"].as<std::string>();
                    settings.levelMobMappings.push_back(mapping);

                    settings.basicWaves.push_back(std::move(wave));
                }
            }

            gauntletManager_.addDivisionSettings(std::move(settings));
        }

        // Load rewards
        auto rewardResult = txn.exec(
            "SELECT division_id, is_winner, reward_type, reward_value, quantity "
            "FROM gauntlet_rewards");

        for (const auto& row : rewardResult) {
            GauntletRewardConfig reward;
            reward.divisionId  = row["division_id"].as<int>();
            reward.isWinner    = row["is_winner"].as<bool>();
            std::string type   = row["reward_type"].as<std::string>();
            if (type == "Gold")  reward.rewardType = GauntletRewardType::Gold;
            else if (type == "Honor") reward.rewardType = GauntletRewardType::Honor;
            else if (type == "Token") reward.rewardType = GauntletRewardType::Token;
            else reward.rewardType = GauntletRewardType::Item;
            reward.rewardValue = row["reward_value"].as<std::string>();
            reward.quantity    = row["quantity"].as<int>();
            gauntletManager_.addRewardConfig(std::move(reward));
        }

        // Load performance rewards
        auto perfResult = txn.exec(
            "SELECT division_id, category, reward_type, reward_value, quantity "
            "FROM gauntlet_performance_rewards");

        for (const auto& row : perfResult) {
            GauntletPerformanceRewardConfig reward;
            reward.divisionId  = row["division_id"].as<int>();
            reward.category    = row["category"].as<std::string>();
            std::string type   = row["reward_type"].as<std::string>();
            if (type == "Gold")  reward.rewardType = GauntletRewardType::Gold;
            else if (type == "Honor") reward.rewardType = GauntletRewardType::Honor;
            else if (type == "Token") reward.rewardType = GauntletRewardType::Token;
            else reward.rewardType = GauntletRewardType::Item;
            reward.rewardValue = row["reward_value"].as<std::string>();
            reward.quantity    = row["quantity"].as<int>();
            gauntletManager_.addPerformanceRewardConfig(std::move(reward));
        }

        txn.commit();
        LOG_INFO("Server", "Gauntlet loaded: %zu divisions",
                 gauntletManager_.activeInstances().size() == 0 ?
                 divResult.size() : gauntletManager_.activeInstances().size());
    } catch (const std::exception& e) {
        LOG_WARN("Server", "Gauntlet config load failed (tables may be empty): %s", e.what());
    }

    // Wire callbacks
    gauntletManager_.onAnnouncement = [this](const std::string& message) {
        // Broadcast gauntlet announcement to all clients via system chat
        SvChatMessageMsg msg;
        msg.channel    = static_cast<uint8_t>(ChatChannel::System);
        msg.senderName = "[Gauntlet]";
        msg.message    = message;
        msg.faction    = 0;

        uint8_t buf[512];
        ByteWriter w(buf, sizeof(buf));
        msg.write(w);
        server_.broadcast(Channel::ReliableOrdered, PacketType::SvChatMessage, buf, w.size());
    };

    gauntletManager_.onDivisionStarted = [this](int divisionId, int playerCount) {
        LOG_INFO("Server", "Gauntlet division %d started with %d players", divisionId, playerCount);
    };

    gauntletManager_.onDivisionComplete = [this](int divisionId, const GauntletMatchResult& result) {
        LOG_INFO("Server", "Gauntlet division %d complete: %s wins (%d-%d), %d players",
                 divisionId,
                 result.winningTeam == GauntletTeamId::TeamA ? "TeamA" :
                 result.winningTeam == GauntletTeamId::TeamB ? "TeamB" : "Tie",
                 result.teamAScore, result.teamBScore, result.playerCount);

        // Broadcast result announcement
        if (gauntletManager_.onAnnouncement) {
            std::string msg = "Gauntlet complete! ";
            if (result.endedInTie) {
                msg += "It's a tie!";
            } else {
                msg += (result.winningTeam == GauntletTeamId::TeamA ? "Team A" : "Team B");
                msg += " wins " + std::to_string(result.teamAScore) + "-" + std::to_string(result.teamBScore) + "!";
            }
            gauntletManager_.onAnnouncement(msg);
        }
    };

    gauntletManager_.onConsolationAwarded = [](const std::string& charId, int honor, int tokens) {
        LOG_INFO("Server", "Gauntlet consolation: %s gets %d honor, %d tokens", charId.c_str(), honor, tokens);
        // TODO: Add honor/tokens to player (need to find online player by charId)
    };
}

void ServerApp::processGauntletCommand(uint16_t clientId, ByteReader& payload) {
    uint8_t subAction = payload.readU8();
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* e = world_.getEntity(h);
    if (!e) return;

    auto* charStats = e->getComponent<CharacterStatsComponent>();
    if (!charStats) return;

    switch (subAction) {
        case GauntletAction::Register: {
            if (!gauntletManager_.isSignupOpen()) {
                SvGauntletUpdateMsg resp;
                resp.updateType = 1; resp.resultCode = 1;
                resp.message = "Gauntlet signup is not currently open.";
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGauntletUpdate, buf, w.size());
                break;
            }

            // Auto-detect division from player level
            int playerLevel = charStats->stats.level;
            int divisionId = gauntletManager_.getDivisionForLevel(playerLevel);
            if (divisionId < 0) {
                SvGauntletUpdateMsg resp;
                resp.updateType = 1; resp.resultCode = 2;
                resp.message = "No gauntlet division found for your level.";
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGauntletUpdate, buf, w.size());
                break;
            }

            RegisteredPlayer rp;
            rp.characterId   = client->character_id;
            rp.characterName = charStats->stats.characterName;
            rp.level         = playerLevel;
            rp.netId         = static_cast<uint32_t>(clientId);
            rp.registeredAt  = gameTime_;

            // Save return position
            auto* t = e->getComponent<Transform>();
            if (t) rp.returnPosition = Coords::toTile(t->position);
            auto* sc = SceneManager::instance().currentScene();
            rp.returnScene = sc ? sc->name() : "Scene2";

            bool registered = gauntletManager_.registerPlayer(rp, divisionId);

            SvGauntletUpdateMsg resp;
            resp.updateType = 1;
            resp.resultCode = registered ? 0 : 3;
            resp.message = registered ? "Registered for The Gauntlet!" : "Already registered.";
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            resp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGauntletUpdate, buf, w.size());
            break;
        }
        case GauntletAction::Unregister: {
            bool unregistered = gauntletManager_.unregisterPlayer(client->character_id);
            SvGauntletUpdateMsg resp;
            resp.updateType = 1;
            resp.resultCode = unregistered ? 0 : 1;
            resp.message = unregistered ? "Unregistered from The Gauntlet." : "You are not registered.";
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            resp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGauntletUpdate, buf, w.size());
            break;
        }
        case GauntletAction::GetStatus: {
            SvGauntletUpdateMsg resp;
            if (gauntletManager_.isSignupOpen()) {
                resp.updateType = 0;
                resp.message = "Signup is open! Speak to the Arena Master to register.";
            } else {
                float remaining = gauntletManager_.timeUntilNextEvent(gameTime_);
                int mins = static_cast<int>(remaining / 60.0f);
                resp.updateType = 0;
                resp.message = "Next Gauntlet in " + std::to_string(mins) + " minutes.";
            }
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            resp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGauntletUpdate, buf, w.size());
            break;
        }
        default:
            LOG_WARN("Server", "Unknown gauntlet sub-action %d from client %d", subAction, clientId);
            break;
    }
}

void ServerApp::broadcastBossKillNotification(const EnemyStats& es,
                                               const EnemyStats::LootOwnerResult& lootResult,
                                               const std::string& scene) {
    if (es.monsterType == "Normal") return;

    SvBossLootOwnerMsg bossMsg;
    bossMsg.bossId = es.enemyId;
    bossMsg.wasParty = lootResult.isParty ? 1 : 0;

    // Get individual top damager's damage
    auto it = es.damageByAttacker.find(lootResult.topDamagerId);
    bossMsg.topDamage = (it != es.damageByAttacker.end()) ? it->second : 0;

    // Look up winner name
    EntityHandle winnerH(lootResult.topDamagerId);
    auto* winnerEntity = world_.getEntity(winnerH);
    if (winnerEntity) {
        auto* winnerNameplate = winnerEntity->getComponent<NameplateComponent>();
        if (winnerNameplate) {
            bossMsg.winnerName = winnerNameplate->displayName;
        }
    }

    if (bossMsg.winnerName.empty()) return; // Winner disconnected

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    bossMsg.write(w);

    // Scene-scoped broadcast: only send to clients in the same scene
    server_.connections().forEach([&](ClientConnection& client) {
        if (client.playerEntityId == 0) return;
        EntityHandle ch(static_cast<uint32_t>(client.playerEntityId));
        auto* ce = world_.getEntity(ch);
        if (!ce) return;
        auto* cs = ce->getComponent<CharacterStatsComponent>();
        if (cs && cs->stats.currentScene == scene) {
            server_.sendTo(client.clientId, Channel::ReliableOrdered,
                          PacketType::SvBossLootOwner, buf, w.size());
        }
    });
}

} // namespace fate
