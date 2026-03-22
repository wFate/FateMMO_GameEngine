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
#include "game/shared/pet_system.h"
#include "game/systems/mob_ai_system.h"
#include "game/shared/xp_calculator.h"
#include "game/shared/profanity_filter.h"
#include "game/shared/enchant_system.h"
#include "game/shared/core_extraction.h"
#include "game/shared/honor_system.h"
#include "game/shared/socket_system.h"
#include "game/shared/stat_enchant_system.h"
#include "game/shared/ranking_system.h"
#include "game/shared/bag_definition.h"
#include "engine/net/game_messages.h"
#include <nlohmann/json.hpp>

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
            // H7-FIX: Replay WAL entries to DB
            try {
                pqxx::work txn(gameDbConn_.connection());
                for (const auto& e : walEntries) {
                    LOG_INFO("WAL", "Replaying: seq=%llu type=%d char=%s val=%lld",
                             e.sequence, static_cast<int>(e.type), e.characterId.c_str(), e.intValue);
                    switch (e.type) {
                        case WalEntryType::GoldChange:
                            txn.exec_params("UPDATE characters SET gold = gold + $1 WHERE character_id = $2",
                                e.intValue, e.characterId);
                            break;
                        case WalEntryType::XPGain:
                            txn.exec_params("UPDATE characters SET current_xp = current_xp + $1 WHERE character_id = $2",
                                e.intValue, e.characterId);
                            break;
                        default: break;
                    }
                }
                txn.commit();
                LOG_INFO("WAL", "Replayed %zu entries", walEntries.size());
            } catch (const std::exception& ex) {
                LOG_ERROR("WAL", "Replay failed: %s", ex.what());
            }
            wal_.truncate();
        }
    }

    // Create repositories (pool-based: each operation acquires its own connection)
    characterRepo_ = std::make_unique<CharacterRepository>(dbPool_);
    inventoryRepo_ = std::make_unique<InventoryRepository>(dbPool_);
    skillRepo_     = std::make_unique<SkillRepository>(dbPool_);
    guildRepo_     = std::make_unique<GuildRepository>(dbPool_);
    socialRepo_    = std::make_unique<SocialRepository>(dbPool_);
    marketRepo_    = std::make_unique<MarketRepository>(dbPool_);
    tradeRepo_     = std::make_unique<TradeRepository>(dbPool_);
    bountyRepo_    = std::make_unique<BountyRepository>(dbPool_);
    questRepo_     = std::make_unique<QuestRepository>(dbPool_);
    bankRepo_      = std::make_unique<BankRepository>(dbPool_);
    petRepo_       = std::make_unique<PetRepository>(dbPool_);
    mobStateRepo_  = std::make_unique<ZoneMobStateRepository>(dbPool_);
    pvpKillLogRepo_ = std::make_unique<PvPKillLogRepository>(dbPool_);

    // Initialize definition caches
    itemDefCache_.initialize(gameDbConn_.connection());
    replication_.setItemDefCache(&itemDefCache_);
    dungeonManager_.setItemDefCache(&itemDefCache_);
    lootTableCache_.initialize(gameDbConn_.connection(), itemDefCache_);
    mobDefCache_.initialize(gameDbConn_.connection());
    skillDefCache_.initialize(gameDbConn_.connection());
    sceneCache_.initialize(gameDbConn_.connection());
    if (!spawnZoneCache_.initialize(gameDbConn_.connection())) {
        LOG_WARN("Server", "Failed to load spawn zones");
    }
    if (recipeCache_.loadFromDatabase(gameDbConn_.connection())) {
        LOG_INFO("Server", "Loaded %zu crafting recipes", recipeCache_.size());
    } else {
        LOG_WARN("Server", "Failed to load crafting recipes (table may not exist yet)");
    }
    LOG_INFO("Server", "Caches loaded: %zu items, %zu loot tables, %d mobs, %d skills (%d ranks), %d scenes",
             itemDefCache_.size(), lootTableCache_.tableCount(),
             mobDefCache_.count(), skillDefCache_.skillCount(), skillDefCache_.rankCount(),
             sceneCache_.count());
    LOG_INFO("Server", "Spawn zones: %d rules", spawnZoneCache_.count());

    // Load pet definitions from database
    petDefCache_.initialize(gameDbConn_.connection());
    LOG_INFO("Server", "Loaded %zu pet definitions from DB", petDefCache_.size());

    // Initialize Gauntlet system
    initGauntlet();

    // Register and wire Battlefield event: 2hr cycle, 5min signup, 15min active
    eventScheduler_.registerEvent({"battlefield", 7200.0f, 300.0f, 900.0f});

    eventScheduler_.setCallback("battlefield", EventCallback::OnSignupStart, [this]{
        SvChatMessageMsg msg;
        msg.channel    = static_cast<uint8_t>(ChatChannel::System);
        msg.senderName = "[Battlefield]";
        msg.message    = "Battlefield signup is now open! Speak to the Battlefield Registrar to join.";
        msg.faction    = 0;
        uint8_t buf[512]; ByteWriter w(buf, sizeof(buf));
        msg.write(w);
        server_.broadcast(Channel::ReliableOrdered, PacketType::SvChatMessage, buf, w.size());
        LOG_INFO("Server", "Battlefield signup is now open!");
    });

    eventScheduler_.setCallback("battlefield", EventCallback::OnEventStart, [this]{
        if (!battlefieldManager_.hasMinimumPlayers()) {
            LOG_INFO("Server", "Battlefield cancelled — not enough players from different factions");
            for (const auto& [eid, player] : battlefieldManager_.players()) {
                playerEventLocks_.erase(eid);
            }
            battlefieldManager_.reset();

            SvChatMessageMsg msg;
            msg.channel    = static_cast<uint8_t>(ChatChannel::System);
            msg.senderName = "[Battlefield]";
            msg.message    = "Battlefield has been cancelled — not enough players from different factions.";
            msg.faction    = 0;
            uint8_t buf[512]; ByteWriter w(buf, sizeof(buf));
            msg.write(w);
            server_.broadcast(Channel::ReliableOrdered, PacketType::SvChatMessage, buf, w.size());
            return;
        }
        LOG_INFO("Server", "Battlefield started with %zu players", battlefieldManager_.playerCount());

        // Teleport all registered players to battlefield scene
        server_.connections().forEach([this](ClientConnection& conn) {
            if (conn.playerEntityId == 0) return;
            uint32_t eid = static_cast<uint32_t>(conn.playerEntityId);
            if (!battlefieldManager_.isPlayerRegistered(eid)) return;

            // Update scene on entity
            PersistentId pid(conn.playerEntityId);
            EntityHandle h = replication_.getEntityHandle(pid);
            Entity* e = world_.getEntity(h);
            if (e) {
                auto* sc = e->getComponent<CharacterStatsComponent>();
                if (sc) sc->stats.currentScene = "battlefield";
            }
            playerDirty_[conn.clientId].position = true;

            // Send zone transition to battlefield
            float spawnX = 0.0f, spawnY = 0.0f;
            auto* sceneDef = sceneCache_.get("battlefield");
            if (sceneDef) { spawnX = sceneDef->defaultSpawnX; spawnY = sceneDef->defaultSpawnY; }

            SvZoneTransitionMsg zt;
            zt.targetScene = "battlefield";
            zt.spawnX = spawnX;
            zt.spawnY = spawnY;
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            zt.write(w);
            server_.sendTo(conn.clientId, Channel::ReliableOrdered, PacketType::SvZoneTransition, buf, w.size());

            // Clear AOI state
            conn.aoi.previous.clear();
            conn.aoi.current.clear();
            conn.aoi.entered.clear();
            conn.aoi.left.clear();
            conn.aoi.stayed.clear();
            conn.lastSentState.clear();
        });

        SvChatMessageMsg msg;
        msg.channel    = static_cast<uint8_t>(ChatChannel::System);
        msg.senderName = "[Battlefield]";
        msg.message    = "The Battlefield has begun! Fight for your faction's glory!";
        msg.faction    = 0;
        uint8_t buf[512]; ByteWriter w(buf, sizeof(buf));
        msg.write(w);
        server_.broadcast(Channel::ReliableOrdered, PacketType::SvChatMessage, buf, w.size());
    });

    eventScheduler_.setCallback("battlefield", EventCallback::OnEventEnd, [this]{
        auto winner = battlefieldManager_.getWinningFaction();
        LOG_INFO("Server", "Battlefield ended. Winner: faction %d", static_cast<int>(winner));

        // Announce winner
        std::string winnerMsg;
        if (winner == Faction::None) {
            winnerMsg = "The Battlefield has ended in a tie!";
        } else {
            winnerMsg = "The Battlefield has ended! Faction " +
                        std::to_string(static_cast<int>(winner)) + " wins!";
        }
        SvChatMessageMsg chatMsg;
        chatMsg.channel    = static_cast<uint8_t>(ChatChannel::System);
        chatMsg.senderName = "[Battlefield]";
        chatMsg.message    = winnerMsg;
        chatMsg.faction    = 0;
        uint8_t chatBuf[512]; ByteWriter cw(chatBuf, sizeof(chatBuf));
        chatMsg.write(cw);
        server_.broadcast(Channel::ReliableOrdered, PacketType::SvChatMessage, chatBuf, cw.size());

        // Distribute rewards and teleport players back
        server_.connections().forEach([this, winner](ClientConnection& conn) {
            if (conn.playerEntityId == 0) return;
            uint32_t eid = static_cast<uint32_t>(conn.playerEntityId);
            if (!battlefieldManager_.isPlayerRegistered(eid)) return;

            const auto& bfPlayers = battlefieldManager_.players();
            auto pit = bfPlayers.find(eid);
            if (pit == bfPlayers.end()) return;
            const BattlefieldPlayer& bfPlayer = pit->second;

            bool isWinner = (bfPlayer.faction == winner && winner != Faction::None);
            int honorReward = isWinner ? 20 : 5;

            // Apply honor to entity
            PersistentId pid(conn.playerEntityId);
            EntityHandle h = replication_.getEntityHandle(pid);
            Entity* e = world_.getEntity(h);
            if (e) {
                auto* sc = e->getComponent<CharacterStatsComponent>();
                if (sc) {
                    sc->stats.honor += honorReward;
                    // Teleport back to return scene
                    sc->stats.currentScene = bfPlayer.returnScene;
                }
            }
            playerDirty_[conn.clientId].stats = true;
            playerDirty_[conn.clientId].position = true;
            enqueuePersist(conn.clientId, PersistPriority::HIGH, PersistType::Character);

            // TODO: Add Pendants of Honor to inventory (3 for winners, 1 for losers)

            // Send zone transition back to return scene
            float retX = bfPlayer.returnPosition.x;
            float retY = bfPlayer.returnPosition.y;
            if (retX == 0.0f && retY == 0.0f) {
                auto* sceneDef = sceneCache_.get(bfPlayer.returnScene);
                if (sceneDef) { retX = sceneDef->defaultSpawnX; retY = sceneDef->defaultSpawnY; }
            }

            SvZoneTransitionMsg zt;
            zt.targetScene = bfPlayer.returnScene;
            zt.spawnX = retX;
            zt.spawnY = retY;
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            zt.write(w);
            server_.sendTo(conn.clientId, Channel::ReliableOrdered, PacketType::SvZoneTransition, buf, w.size());

            // Clear AOI state
            conn.aoi.previous.clear();
            conn.aoi.current.clear();
            conn.aoi.entered.clear();
            conn.aoi.left.clear();
            conn.aoi.stayed.clear();
            conn.lastSentState.clear();

            playerEventLocks_.erase(eid);
        });

        battlefieldManager_.reset();
    });

    // Wire ArenaManager callbacks
    arenaManager_.onMatchEnd = [this](uint32_t matchId, bool teamAWins, bool tie) {
        auto* match = arenaManager_.getMatch(matchId);
        if (!match) return;

        for (auto& [eid, stats] : match->players) {
            bool onTeamA = false;
            for (uint32_t id : match->teamA) {
                if (id == eid) { onTeamA = true; break; }
            }

            bool won = (onTeamA && teamAWins) || (!onTeamA && !teamAWins && !tie);
            int honor = 0;
            if (stats.damageDealt > 0) {
                honor = tie ? ArenaMatch::HONOR_TIE
                            : (won ? ArenaMatch::HONOR_WIN : ArenaMatch::HONOR_LOSS);
            }

            // Apply honor to player entity
            uint16_t targetClientId = 0;
            server_.connections().forEach([&](ClientConnection& conn) {
                if (static_cast<uint32_t>(conn.playerEntityId) == eid) {
                    targetClientId = conn.clientId;
                }
            });

            if (targetClientId != 0) {
                auto* client = server_.connections().findById(targetClientId);
                if (client && client->playerEntityId != 0) {
                    PersistentId pid(client->playerEntityId);
                    EntityHandle h = replication_.getEntityHandle(pid);
                    Entity* e = world_.getEntity(h);
                    if (e) {
                        auto* sc = e->getComponent<CharacterStatsComponent>();
                        if (sc) {
                            sc->stats.honor += honor;
                            // Teleport back to return scene
                            sc->stats.currentScene = stats.returnScene;
                        }
                    }
                    playerDirty_[targetClientId].stats = true;
                    playerDirty_[targetClientId].position = true;
                    enqueuePersist(targetClientId, PersistPriority::HIGH, PersistType::Character);

                    // Send zone transition back to return scene
                    float retX = stats.returnPosition.x;
                    float retY = stats.returnPosition.y;
                    if (retX == 0.0f && retY == 0.0f) {
                        auto* sceneDef = sceneCache_.get(stats.returnScene);
                        if (sceneDef) { retX = sceneDef->defaultSpawnX; retY = sceneDef->defaultSpawnY; }
                    }
                    SvZoneTransitionMsg zt;
                    zt.targetScene = stats.returnScene;
                    zt.spawnX = retX;
                    zt.spawnY = retY;
                    uint8_t ztBuf[256]; ByteWriter ztW(ztBuf, sizeof(ztBuf));
                    zt.write(ztW);
                    server_.sendTo(targetClientId, Channel::ReliableOrdered,
                                   PacketType::SvZoneTransition, ztBuf, ztW.size());

                    // Clear AOI state
                    if (client) {
                        client->aoi.previous.clear();
                        client->aoi.current.clear();
                        client->aoi.entered.clear();
                        client->aoi.left.clear();
                        client->aoi.stayed.clear();
                        client->lastSentState.clear();
                    }

                    // Send SvArenaUpdate with match result
                    SvArenaUpdateMsg arenaMsg;
                    arenaMsg.state = static_cast<uint8_t>(ArenaMatchState::Ended);
                    arenaMsg.timeRemaining = 0;
                    arenaMsg.teamAlive = 0;
                    arenaMsg.enemyAlive = 0;
                    arenaMsg.result = tie ? 2 : (won ? 1 : 0); // 0=loss, 1=win, 2=tie
                    arenaMsg.honorReward = honor;
                    uint8_t arBuf[32]; ByteWriter arW(arBuf, sizeof(arBuf));
                    arenaMsg.write(arW);
                    server_.sendTo(targetClientId, Channel::ReliableOrdered,
                                   PacketType::SvArenaUpdate, arBuf, arW.size());
                }
            }

            playerEventLocks_.erase(eid);
        }
    };

    arenaManager_.onGroupUnregistered = [this](const std::vector<uint32_t>& playerIds,
                                               const std::string& reason) {
        for (uint32_t eid : playerIds) {
            playerEventLocks_.erase(eid);
        }
        LOG_INFO("Server", "Arena group unregistered: %s", reason.c_str());
    };

    arenaManager_.onPlayerForfeited = [this](uint32_t entityId, const std::string& reason) {
        LOG_INFO("Server", "Arena player %u forfeited: %s", entityId, reason.c_str());
    };

    // Initialize GM commands
    initGMCommands();

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
    for (auto& [id, inst] : dungeonManager_.allInstances()) {
        if (!inst->expired) {
            inst->world.forEach<StatusEffectComponent>([&](Entity*, StatusEffectComponent* seComp) {
                seComp->effects.tick(dt);
            });
        }
    }
    world_.forEach<CrowdControlComponent>([&](Entity*, CrowdControlComponent* ccComp) {
        ccComp->cc.tick(dt);
    });
    for (auto& [id, inst] : dungeonManager_.allInstances()) {
        if (!inst->expired) {
            inst->world.forEach<CrowdControlComponent>([&](Entity*, CrowdControlComponent* ccComp) {
                ccComp->cc.tick(dt);
            });
        }
    }

    // 3c. Tick player timers (PK decay, combat timer, respawn invuln)
    world_.forEach<CharacterStatsComponent>([&](Entity*, CharacterStatsComponent* cs) {
        cs->stats.tickTimers(dt);
    });
    for (auto& [id, inst] : dungeonManager_.allInstances()) {
        if (!inst->expired) {
            inst->world.forEach<CharacterStatsComponent>([&](Entity*, CharacterStatsComponent* cs) {
                cs->stats.tickTimers(dt);
            });
        }
    }

    // 3d. HP/MP regen tick (server-authoritative)
    // NOTE: No dirty flag for regen — runs on all players, 5-minute auto-save catches it
    regenTimer_ += dt;
    mpRegenTimer_ += dt;
    if (regenTimer_ >= 10.0f || mpRegenTimer_ >= 5.0f) {
        bool doHP = regenTimer_ >= 10.0f;
        bool doMP = mpRegenTimer_ >= 5.0f;
        auto regenLambda = [&](Entity*, CharacterStatsComponent* cs) {
            if (!cs->stats.isAlive()) return;
            // HP regen: 1% maxHP + equipment bonus, every 10 seconds
            if (doHP && cs->stats.currentHP < cs->stats.maxHP) {
                int amount = std::max(1, static_cast<int>(
                    cs->stats.maxHP * 0.01f + cs->stats.equipBonusHPRegen));
                cs->stats.heal(amount);
            }
            // MP regen: WIS-based + equipment bonus, every 5 seconds (mana classes only)
            if (doMP && cs->stats.classDef.usesMana()
                && cs->stats.currentMP < cs->stats.maxMP) {
                int amount = std::max(1, static_cast<int>(
                    cs->stats.getWisdom() * 0.5f + cs->stats.equipBonusMPRegen));
                cs->stats.currentMP = std::min(cs->stats.maxMP, cs->stats.currentMP + amount);
            }
        };
        world_.forEach<CharacterStatsComponent>(regenLambda);
        for (auto& [id, inst] : dungeonManager_.allInstances()) {
            if (!inst->expired) {
                inst->world.forEach<CharacterStatsComponent>(regenLambda);
            }
        }
        if (doHP) regenTimer_ -= 10.0f;
        if (doMP) mpRegenTimer_ -= 5.0f;
    }

    // Pet auto-loot tick
    tickPetAutoLoot(dt);

    // 3e. Process Dying → Dead transitions (two-tick death lifecycle)
    world_.forEach<CharacterStatsComponent>([](Entity*, CharacterStatsComponent* cs) {
        cs->stats.advanceDeathTick();
    });
    for (auto& [id, inst] : dungeonManager_.allInstances()) {
        if (!inst->expired) {
            inst->world.forEach<CharacterStatsComponent>([](Entity*, CharacterStatsComponent* cs) {
                cs->stats.advanceDeathTick();
            });
        }
    }

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

        // Despawn in dungeon instances too
        for (auto& [id, inst] : dungeonManager_.allInstances()) {
            if (!inst->expired) {
                std::vector<EntityHandle> instDestroy;
                inst->world.forEach<DroppedItemComponent>([&](Entity* e, DroppedItemComponent* drop) {
                    if (gameTime_ - drop->spawnTime > drop->despawnAfter) {
                        instDestroy.push_back(e->handle());
                    }
                });
                for (auto handle : instDestroy) {
                    inst->replication.unregisterEntity(handle);
                    inst->world.destroyEntity(handle);
                }
            }
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

    // 5d. Battlefield event scheduler
    eventScheduler_.tick(gameTime_);

    // 5e. Arena matchmaking (every 20 ticks = ~1 second)
    arenaTickCounter_++;
    if (arenaTickCounter_ % 20 == 0) {
        auto newMatches = arenaManager_.tryMatchmaking();
        for (uint32_t matchId : newMatches) {
            auto* match = arenaManager_.getMatch(matchId);
            if (!match) continue;
            LOG_INFO("Server", "Arena match %u created (mode %d)", matchId,
                     static_cast<int>(match->mode));
            for (auto& [eid, stats] : match->players) {
                // Store return position/scene from current player entity state
                uint16_t targetClientId = 0;
                server_.connections().forEach([&](ClientConnection& conn) {
                    if (static_cast<uint32_t>(conn.playerEntityId) == eid) {
                        targetClientId = conn.clientId;
                    }
                });
                if (targetClientId == 0) continue;
                auto* client = server_.connections().findById(targetClientId);
                if (!client || client->playerEntityId == 0) continue;

                PersistentId pid(client->playerEntityId);
                EntityHandle h = replication_.getEntityHandle(pid);
                Entity* player = world_.getEntity(h);
                if (!player) continue;

                auto* sc = player->getComponent<CharacterStatsComponent>();
                auto* transform = player->getComponent<Transform>();
                if (sc) {
                    stats.returnScene = sc->stats.currentScene;
                    stats.returnPosition = transform ? transform->position : Vec2{0.0f, 0.0f};
                    // Teleport player to arena scene
                    sc->stats.currentScene = "arena";
                }
                playerDirty_[targetClientId].position = true;

                float spawnX = 0.0f, spawnY = 0.0f;
                auto* sceneDef = sceneCache_.get("arena");
                if (sceneDef) { spawnX = sceneDef->defaultSpawnX; spawnY = sceneDef->defaultSpawnY; }

                SvZoneTransitionMsg zt;
                zt.targetScene = "arena";
                zt.spawnX = spawnX;
                zt.spawnY = spawnY;
                uint8_t ztBuf[256]; ByteWriter ztW(ztBuf, sizeof(ztBuf));
                zt.write(ztW);
                server_.sendTo(targetClientId, Channel::ReliableOrdered,
                               PacketType::SvZoneTransition, ztBuf, ztW.size());

                // Clear AOI state
                client->aoi.previous.clear();
                client->aoi.current.clear();
                client->aoi.entered.clear();
                client->aoi.left.clear();
                client->aoi.stayed.clear();
                client->lastSentState.clear();

                // Notify client of countdown state
                SvArenaUpdateMsg arenaMsg;
                arenaMsg.state = static_cast<uint8_t>(ArenaMatchState::Countdown);
                arenaMsg.timeRemaining = static_cast<uint16_t>(ArenaMatch::COUNTDOWN_DURATION);
                arenaMsg.teamAlive = 0;
                arenaMsg.enemyAlive = 0;
                arenaMsg.result = 0;
                arenaMsg.honorReward = 0;
                uint8_t arBuf[32]; ByteWriter arW(arBuf, sizeof(arBuf));
                arenaMsg.write(arW);
                server_.sendTo(targetClientId, Channel::ReliableOrdered,
                               PacketType::SvArenaUpdate, arBuf, arW.size());
            }
        }
    }
    arenaManager_.tickMatches(gameTime_);

    // 5g. Auto-save and periodic maintenance
    tickAutoSave(dt);
    tickPersistQueue();
    tickMaintenance(dt);
    tickDungeonInstances(dt);
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

    // H12-FIX: Restore rate limiter from previous session
    {
        auto rlIt = accountRateLimiters_.find(session.account_id);
        if (rlIt != accountRateLimiters_.end()) {
            rateLimiters_[clientId] = std::move(rlIt->second);
            accountRateLimiters_.erase(rlIt);
        }
    }

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
        s.pkStatus     = static_cast<PKStatus>(rec.pk_status);
        s.faction      = static_cast<Faction>(rec.faction);
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
            } else if (slot.bag_slot_index >= 0 && slot.bag_item_slot >= 0) {
                // Item inside a bag — place into bag contents
                // Ensure bag capacity is initialized for this bag slot
                if (inv->inventory.bagSlotCount(slot.bag_slot_index) == 0) {
                    // Determine bag capacity from the bag item in that slot
                    auto bagItem = inv->inventory.getSlot(slot.bag_slot_index);
                    int capacity = 6; // default
                    if (bagItem.isValid()) {
                        const auto* bagDef = itemDefCache_.getDefinition(bagItem.itemId);
                        if (bagDef) {
                            capacity = std::clamp(bagDef->attributes.value("slot_count", 6), 1, 10);
                        }
                    }
                    inv->inventory.setBagCapacity(slot.bag_slot_index, capacity);
                }
                inv->inventory.addItemToBag(slot.bag_slot_index, item);
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

    // Load admin_role from DB (carried through PendingSession from auth flow)
    clientAdminRoles_[clientId] = session.admin_role;

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
            EntityHandle eh = getReplicationForClient(clientId).getEntityHandle(p);
            Entity* ent = getWorldForClient(clientId).getEntity(eh);
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
            EntityHandle eh = getReplicationForClient(clientId).getEntityHandle(p);
            Entity* ent = getWorldForClient(clientId).getEntity(eh);
            if (!ent) return;
            auto* cs = ent->getComponent<CharacterStatsComponent>();
            if (!cs || !cs->stats.isAlive()) return;

            cs->stats.currentHP = (std::max)(0, cs->stats.currentHP - damage);
            playerDirty_[clientId].vitals = true;

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
                // No XP loss in dungeons
                DeathSource deathSrc = dungeonManager_.getInstanceForClient(clientId)
                    ? DeathSource::Dungeon : DeathSource::PvE;
                cs->stats.die(deathSrc);
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
            EntityHandle eh = getReplicationForClient(clientId).getEntityHandle(p);
            Entity* ent = getWorldForClient(clientId).getEntity(eh);
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

void ServerApp::savePlayerToDB(uint16_t clientId, bool forceSaveAll) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
    if (!e) return;

    auto& dirty = playerDirty_[clientId];

    CharacterRecord rec;
    rec.character_id = client->character_id;
    rec.account_id   = client->account_id;

    auto* charStatsComp = e->getComponent<CharacterStatsComponent>();
    if (charStatsComp) {
        const CharacterStats& s = charStatsComp->stats;
        rec.character_name   = s.characterName;
        rec.class_name       = s.className;
        if (forceSaveAll || dirty.stats) {
            rec.level            = s.level;
            rec.current_xp       = s.currentXP;
            rec.xp_to_next_level = static_cast<int>(s.xpToNextLevel);
            rec.honor            = s.honor;
            rec.pvp_kills        = s.pvpKills;
            rec.pvp_deaths       = s.pvpDeaths;
            rec.pk_status        = static_cast<int>(s.pkStatus);
            rec.faction          = static_cast<int>(s.faction);
        }
        if (forceSaveAll || dirty.vitals) {
            rec.current_hp       = s.currentHP;
            rec.max_hp           = s.maxHP;
            rec.current_mp       = s.currentMP;
            rec.max_mp           = s.maxMP;
            rec.current_fury     = s.currentFury;
            rec.is_dead          = s.isDead;
        }
    }

    if (forceSaveAll || dirty.position) {
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
    }

    if (forceSaveAll || dirty.inventory) {
        auto* inv = e->getComponent<InventoryComponent>();
        if (inv) {
            rec.gold = inv->inventory.getGold();
        }
    }

    if (!characterRepo_->saveCharacter(rec)) {
        // Retry once
        if (!characterRepo_->saveCharacter(rec)) {
            LOG_ERROR("Server", "DATA LOSS: failed to save character '%s' (client %d) after retry",
                      rec.character_id.c_str(), clientId);
        }
    }

    // Save skills
    if (forceSaveAll || dirty.skills) {
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
    }

    // Save quest progress
    if (forceSaveAll || dirty.quests) {
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
    }

    // Save bank gold (items saved on-demand when deposited/withdrawn)
    if (forceSaveAll || dirty.bank) {
        auto* bankComp = e->getComponent<BankStorageComponent>();
        if (bankComp) {
            int64_t bankGold = bankComp->storage.getStoredGold();
            if (bankGold > 0) {
                bankRepo_->depositGold(rec.character_id, 0); // ensure row exists
                // Direct set via raw query would be cleaner, but depositGold upserts
            }
        }
    }

    // Save pet state
    if (forceSaveAll || dirty.pet) {
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
    }

    // Update last_online
    socialRepo_->updateLastOnline(rec.character_id);

    if (!forceSaveAll) {
        dirty.clearAll();
    }
}

void ServerApp::enqueuePersist(uint16_t clientId, PersistPriority priority, PersistType type) {
    uint64_t key = (static_cast<uint64_t>(clientId) << 8) | static_cast<uint64_t>(type);
    auto it = pendingPersist_.find(key);
    if (it != pendingPersist_.end()) {
        float elapsed = gameTime_ - it->second;
        if (elapsed < 1.0f) return; // dedup window
    }
    pendingPersist_[key] = gameTime_;
    persistQueue_.enqueue(clientId, priority, type, gameTime_);
}

void ServerApp::tickPersistQueue() {
    if (persistQueue_.empty()) return;
    auto batch = persistQueue_.dequeue(10, gameTime_);
    for (auto& req : batch) {
        savePlayerToDBAsync(req.clientId, false); // false = check dirty flags
    }
}

void ServerApp::savePlayerToDBAsync(uint16_t clientId, bool forceSaveAll) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
    if (!e) return;

    auto& dirty = playerDirty_[clientId];

    // ---- Snapshot all data on game thread ----
    CharacterRecord rec;
    rec.character_id = client->character_id;
    rec.account_id   = client->account_id;

    auto* charStatsComp = e->getComponent<CharacterStatsComponent>();
    if (charStatsComp) {
        const CharacterStats& s = charStatsComp->stats;
        rec.character_name   = s.characterName;
        rec.class_name       = s.className;
        if (forceSaveAll || dirty.stats) {
            rec.level            = s.level;
            rec.current_xp       = s.currentXP;
            rec.xp_to_next_level = static_cast<int>(s.xpToNextLevel);
            rec.honor            = s.honor;
            rec.pvp_kills        = s.pvpKills;
            rec.pvp_deaths       = s.pvpDeaths;
            rec.pk_status        = static_cast<int>(s.pkStatus);
            rec.faction          = static_cast<int>(s.faction);
        }
        if (forceSaveAll || dirty.vitals) {
            rec.current_hp       = s.currentHP;
            rec.max_hp           = s.maxHP;
            rec.current_mp       = s.currentMP;
            rec.max_mp           = s.maxMP;
            rec.current_fury     = s.currentFury;
            rec.is_dead          = s.isDead;
        }
    }

    if (forceSaveAll || dirty.position) {
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
    }

    if (forceSaveAll || dirty.inventory) {
        auto* inv = e->getComponent<InventoryComponent>();
        if (inv) rec.gold = inv->inventory.getGold();
    }

    // Snapshot skills
    bool saveSkills = forceSaveAll || dirty.skills;
    std::vector<CharacterSkillRecord> skillRecords;
    int skillEarned = 0, skillSpent = 0;
    std::vector<std::string> skillBar(20, "");
    if (saveSkills) {
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
    }

    if (!forceSaveAll) {
        dirty.clearAll();
    }

    std::string charId = client->character_id;

    // ---- Dispatch to fiber worker (non-blocking) ----
    // Acquire per-player lock inside the fiber to serialize against concurrent
    // game-thread mutations (trade execution, loot, market) that may modify
    // inventory/gold while the async save is writing state.
    dbDispatcher_.dispatchVoid([this, rec, charId, skillRecords, skillEarned, skillSpent, skillBar, saveSkills]
                               (pqxx::connection& conn) {
        std::lock_guard<std::mutex> lock(playerLocks_.get(charId));
        try {
            pqxx::work txn(conn);
            txn.exec_params(
                "UPDATE characters SET "
                "level = $2, current_xp = $3, xp_to_next_level = $4, "
                "current_scene = $5, position_x = $6, position_y = $7, "
                "current_hp = $8, max_hp = $9, current_mp = $10, max_mp = $11, current_fury = $12, "
                "base_strength = $13, base_vitality = $14, base_intelligence = $15, "
                "base_dexterity = $16, base_wisdom = $17, "
                "gold = $18, honor = $19, pvp_kills = $20, pvp_deaths = $21, "
                "is_dead = $22, death_timestamp = $23, total_playtime_seconds = $24, "
                "pk_status = $25, faction = $26, last_saved_at = NOW(), last_online = NOW() "
                "WHERE character_id = $1",
                rec.character_id,
                rec.level, rec.current_xp, rec.xp_to_next_level,
                rec.current_scene, rec.position_x, rec.position_y,
                rec.current_hp, rec.max_hp, rec.current_mp, rec.max_mp, rec.current_fury,
                rec.base_strength, rec.base_vitality, rec.base_intelligence,
                rec.base_dexterity, rec.base_wisdom,
                rec.gold, rec.honor, rec.pvp_kills, rec.pvp_deaths,
                rec.is_dead, rec.death_timestamp, rec.total_playtime_seconds,
                rec.pk_status, rec.faction);
            if (saveSkills) {
                for (const auto& s : skillRecords)
                    txn.exec_params(
                        "INSERT INTO character_skills (character_id, skill_id, unlocked_rank, activated_rank, learned_at) "
                        "VALUES ($1, $2, $3, $4, NOW()) "
                        "ON CONFLICT (character_id, skill_id) DO UPDATE SET unlocked_rank = $3, activated_rank = $4",
                        charId, s.skillId, s.unlockedRank, s.activatedRank);
                txn.exec_params("DELETE FROM character_skill_bar WHERE character_id = $1", charId);
                for (int i = 0; i < static_cast<int>(skillBar.size()) && i < 20; ++i) {
                    if (skillBar[i].empty()) continue;
                    txn.exec_params("INSERT INTO character_skill_bar (character_id, slot_index, skill_id) VALUES ($1, $2, $3)",
                        charId, i, skillBar[i]);
                }
                txn.exec_params(
                    "INSERT INTO character_skill_points (character_id, total_earned, total_spent, updated_at) "
                    "VALUES ($1, $2, $3, NOW()) "
                    "ON CONFLICT (character_id) DO UPDATE SET total_earned = $2, total_spent = $3, updated_at = NOW()",
                    charId, skillEarned, skillSpent);
            }
            txn.commit();
        } catch (const std::exception& e) {
            LOG_ERROR("Server", "Atomic save failed for %s: %s", charId.c_str(), e.what());
        }
    });
}

void ServerApp::saveInventoryForClient(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
    if (!e) return;

    auto* inv = e->getComponent<InventoryComponent>();
    if (!inv) return;

    std::vector<InventorySlotRecord> slots;

    // Helper to build a record from an ItemInstance
    auto buildRecord = [&](const ItemInstance& item, int slotIdx, int bagSlotIdx, int bagItemSlot) {
        InventorySlotRecord s;
        s.instance_id   = item.instanceId;
        s.character_id  = client->character_id;
        s.item_id       = item.itemId;
        s.slot_index    = slotIdx;
        s.bag_slot_index = bagSlotIdx;
        s.bag_item_slot  = bagItemSlot;
        s.rolled_stats  = ItemStatRoller::rolledStatsToJson(item.rolledStats);
        s.enchant_level = item.enchantLevel;
        s.is_protected  = item.isProtected;
        s.is_soulbound  = item.isSoulbound;
        s.is_broken     = item.isBroken;
        s.quantity      = item.quantity;
        // Socket data
        if (item.hasSocket()) {
            switch (item.socket.statType) {
                case StatType::Strength:     s.socket_stat = "STR"; break;
                case StatType::Dexterity:    s.socket_stat = "DEX"; break;
                case StatType::Intelligence: s.socket_stat = "INT"; break;
                default: break;
            }
            s.socket_value = item.socket.value;
        }
        return s;
    };

    // Save main inventory slots (0-14)
    const auto& items = inv->inventory.getSlots();
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (!items[i].isValid()) continue;
        slots.push_back(buildRecord(items[i], i, -1, -1));

        // Save bag contents for this slot (if any)
        const auto& bagItems = inv->inventory.getBagContents(i);
        for (int j = 0; j < static_cast<int>(bagItems.size()); ++j) {
            if (!bagItems[j].isValid()) continue;
            slots.push_back(buildRecord(bagItems[j], -1, i, j));
        }
    }

    // Save equipped items
    for (const auto& [eqSlot, item] : inv->inventory.getEquipmentMap()) {
        if (!item.isValid()) continue;
        InventorySlotRecord s = buildRecord(item, -1, -1, -1);
        s.is_equipped = true;
        switch (eqSlot) {
            case EquipmentSlot::Weapon:    s.equipped_slot = "Weapon"; break;
            case EquipmentSlot::SubWeapon: s.equipped_slot = "SubWeapon"; break;
            case EquipmentSlot::Hat:       s.equipped_slot = "Hat"; break;
            case EquipmentSlot::Armor:     s.equipped_slot = "Armor"; break;
            case EquipmentSlot::Gloves:    s.equipped_slot = "Gloves"; break;
            case EquipmentSlot::Shoes:     s.equipped_slot = "Shoes"; break;
            case EquipmentSlot::Belt:      s.equipped_slot = "Belt"; break;
            case EquipmentSlot::Cloak:     s.equipped_slot = "Cloak"; break;
            case EquipmentSlot::Ring:      s.equipped_slot = "Ring"; break;
            case EquipmentSlot::Necklace:  s.equipped_slot = "Necklace"; break;
            default: break;
        }
        slots.push_back(std::move(s));
    }

    inventoryRepo_->saveInventory(client->character_id, slots);
}

void ServerApp::onClientDisconnected(uint16_t clientId) {
    LOG_INFO("Server", "Client %d disconnected", clientId);

    // Save player data first
    savePlayerToDB(clientId);

    // Clean persistence dedup entries for this client
    for (uint8_t t = 0; t < 9; ++t) {
        uint64_t key = (static_cast<uint64_t>(clientId) << 8) | t;
        pendingPersist_.erase(key);
    }
    playerDirty_.erase(clientId);

    // Remove from active account sessions
    auto* client = server_.connections().findById(clientId);
    if (client && client->account_id != 0) {
        activeAccountSessions_.erase(client->account_id);
    }

    if (client && client->playerEntityId != 0) {
        uint32_t eid = static_cast<uint32_t>(client->playerEntityId);
        battlefieldManager_.removePlayer(eid);
        arenaManager_.onPlayerDisconnect(eid);
        playerEventLocks_.erase(eid);

        // Clean up party membership on disconnect
        {
            World& w = getWorldForClient(clientId);
            ReplicationManager& r = getReplicationForClient(clientId);
            PersistentId p(client->playerEntityId);
            EntityHandle eh = r.getEntityHandle(p);
            Entity* pe = w.getEntity(eh);
            if (pe) {
                auto* partyComp = pe->getComponent<PartyComponent>();
                if (partyComp && partyComp->party.isInParty()) {
                    partyComp->party.leaveParty();
                }
            }
        }

        World& world = getWorldForClient(clientId);
        ReplicationManager& repl = getReplicationForClient(clientId);

        PersistentId pid(client->playerEntityId);
        EntityHandle h = repl.getEntityHandle(pid);
        // Unregister from replication BEFORE destroying — prevents dangling
        // handle in spatial index on next tick's rebuildSpatialIndex()
        repl.unregisterEntity(h);
        if (h) {
            world.destroyEntity(h);
            world.processDestroyQueue();
        }

        // If in a dungeon instance, remove from instance tracking
        uint32_t instId = dungeonManager_.getInstanceForClient(clientId);
        if (instId) {
            dungeonManager_.removePlayer(instId, clientId);
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
    // H12-FIX: Preserve rate limiter for this account
    if (client && client->account_id != 0) {
        auto rlIt = rateLimiters_.find(clientId);
        if (rlIt != rateLimiters_.end())
            accountRateLimiters_[client->account_id] = std::move(rlIt->second);
    }
    rateLimiters_.erase(clientId);
    nonceManager_.removeClient(clientId);
    clientAdminRoles_.erase(clientId);
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
                EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
                Entity* e = getWorldForClient(clientId).getEntity(h);
                if (!e) break;

                // First move after connect: accept unconditionally (position desync)
                if (needsFirstMoveSync_.count(clientId)) {
                    needsFirstMoveSync_.erase(clientId);
                    auto* t = e->getComponent<Transform>();
                    if (t) t->position = move.position;
                    lastValidPositions_[clientId] = move.position;
                    lastMoveTime_[clientId] = gameTime_;
                    playerDirty_[clientId].position = true;
                    enqueuePersist(clientId, PersistPriority::NORMAL, PersistType::Position);
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
                    playerDirty_[clientId].position = true;
                    enqueuePersist(clientId, PersistPriority::NORMAL, PersistType::Position);
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

            // GM command intercept — check before profanity filter and broadcast
            {
                auto parsed = GMCommandParser::parse(chat.message);
                if (parsed.isCommand) {
                    auto sendSystemMsg = [this](uint16_t targetClientId, const std::string& text) {
                        SvChatMessageMsg sysMsg;
                        sysMsg.channel    = static_cast<uint8_t>(ChatChannel::System);
                        sysMsg.senderName = "System";
                        sysMsg.message    = text;
                        sysMsg.faction    = 0;
                        uint8_t buf[512]; ByteWriter w(buf, sizeof(buf));
                        sysMsg.write(w);
                        server_.sendTo(targetClientId, Channel::ReliableOrdered,
                                       PacketType::SvChatMessage, buf, w.size());
                    };

                    auto* cmd = gmCommands_.findCommand(parsed.commandName);
                    if (!cmd) {
                        sendSystemMsg(clientId, "Unknown command: /" + parsed.commandName);
                        break;
                    }
                    int role = clientAdminRoles_.count(clientId) ? clientAdminRoles_[clientId] : 0;
                    if (!GMCommandRegistry::hasPermission(role, cmd->minRole)) {
                        sendSystemMsg(clientId, "Insufficient permission.");
                        break;
                    }
                    cmd->handler(clientId, parsed.args);
                    break; // don't broadcast GM commands as chat
                }
            }

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
                EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
                Entity* e = getWorldForClient(clientId).getEntity(h);
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
                    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
                    Entity* e = getWorldForClient(clientId).getEntity(h);
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
                        playerDirty_[clientId].inventory = true;
                        enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

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
                    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
                    Entity* e = getWorldForClient(clientId).getEntity(h);
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
                        playerDirty_[clientId].inventory = true;

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

                        enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

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

                    // H1-FIX: Validate placer has enough gold
                    Entity* bountyEntity = getWorldForClient(clientId).getEntity(
                        getReplicationForClient(clientId).getEntityHandle(PersistentId(client->playerEntityId)));
                    auto* bountyInv = bountyEntity ? bountyEntity->getComponent<InventoryComponent>() : nullptr;
                    if (!bountyInv || amount <= 0 || bountyInv->inventory.getGold() < amount) {
                        SvBountyUpdateMsg errResp;
                        errResp.updateType = 4;
                        errResp.resultCode = static_cast<uint8_t>(BountyResult::InsufficientGold);
                        errResp.message = "Not enough gold";
                        uint8_t buf2[256]; ByteWriter w2(buf2, sizeof(buf2));
                        errResp.write(w2);
                        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvBountyUpdate, buf2, w2.size());
                        break;
                    }

                    BountyResult canPlace = BountyManager::canPlaceBounty(
                        client->character_id, targetCharId, amount,
                        placerGuildId, targetGuildId, activeCount, targetHasBounty);

                    SvBountyUpdateMsg resp;
                    resp.updateType = 4; // result
                    if (canPlace != BountyResult::Success) {
                        resp.resultCode = static_cast<uint8_t>(canPlace);
                        resp.message = BountyManager::getResultMessage(canPlace, targetCharId);
                    } else {
                        wal_.appendGoldChange(client->character_id, -amount);
                        bountyInv->inventory.setGold(bountyInv->inventory.getGold() - amount);
                        playerDirty_[clientId].inventory = true;
                        enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

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
                        EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
                        Entity* e = getWorldForClient(clientId).getEntity(h);
                        if (e) {
                            auto* inv = e->getComponent<InventoryComponent>();
                            if (inv) {
                                // WAL: record gold refund before mutating
                                wal_.appendGoldChange(client->character_id, refund);
                                inv->inventory.addGold(refund);
                                playerDirty_[clientId].inventory = true;
                                enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);
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
            EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
            Entity* e = getWorldForClient(clientId).getEntity(h);
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
                        playerDirty_[clientId].inventory = true;
                        playerDirty_[clientId].guild = true;
                        enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);
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
            EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
            Entity* e = getWorldForClient(clientId).getEntity(h);
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
                    if (inv->inventory.isSlotLocked(invSlot)) { sendTradeResult(6, 4, "Item already in trade"); break; }
                    ItemInstance item = inv->inventory.getSlot(invSlot);
                    if (item.isBound()) { sendTradeResult(6, 5, "Item is soulbound"); break; }

                    tradeRepo_->addItemToTrade(session->sessionId, client->character_id,
                                                slotIdx, sourceSlot, instanceId, quantity);
                    inv->inventory.lockSlotForTrade(invSlot);
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
                    if (gold < 0) { sendTradeResult(6, 6, "Invalid gold amount"); break; }
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

                            // Sync in-memory inventories to match DB state
                            auto findInvForCharId = [&](const std::string& charId) -> InventoryComponent* {
                                InventoryComponent* result = nullptr;
                                server_.connections().forEach([&](ClientConnection& c) {
                                    if (c.character_id == charId && c.playerEntityId != 0) {
                                        PersistentId p(c.playerEntityId);
                                        EntityHandle eh = getReplicationForClient(c.clientId).getEntityHandle(p);
                                        Entity* ent = getWorldForClient(c.clientId).getEntity(eh);
                                        if (ent) result = ent->getComponent<InventoryComponent>();
                                    }
                                });
                                return result;
                            };
                            auto* invA = findInvForCharId(session->playerACharacterId);
                            auto* invB = findInvForCharId(session->playerBCharacterId);
                            if (invA && invB) {
                                for (const auto& offer : offersA) {
                                    int slot = invA->inventory.findByInstanceId(offer.itemInstanceId);
                                    if (slot >= 0) {
                                        ItemInstance item = invA->inventory.getSlot(slot);
                                        invA->inventory.removeItem(slot);
                                        invB->inventory.addItem(item);
                                    }
                                }
                                for (const auto& offer : offersB) {
                                    int slot = invB->inventory.findByInstanceId(offer.itemInstanceId);
                                    if (slot >= 0) {
                                        ItemInstance item = invB->inventory.getSlot(slot);
                                        invB->inventory.removeItem(slot);
                                        invA->inventory.addItem(item);
                                    }
                                }
                                if (session->playerAGold > 0) {
                                    invA->inventory.setGold(invA->inventory.getGold() - session->playerAGold);
                                    invB->inventory.setGold(invB->inventory.getGold() + session->playerAGold);
                                }
                                if (session->playerBGold > 0) {
                                    invB->inventory.setGold(invB->inventory.getGold() - session->playerBGold);
                                    invA->inventory.setGold(invA->inventory.getGold() + session->playerBGold);
                                }
                            }
                            if (invA) invA->inventory.unlockAllTradeSlots();
                            if (invB) invB->inventory.unlockAllTradeSlots();

                            sendTradeResult(5, 0, "Trade completed!");
                            playerDirty_[clientId].inventory = true;
                            enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);
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
                                    playerDirty_[c.clientId].inventory = true;
                                    enqueuePersist(c.clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);
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
                    auto* inv = e->getComponent<InventoryComponent>();
                    if (inv) inv->inventory.unlockAllTradeSlots();
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
            EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
            Entity* e = getWorldForClient(clientId).getEntity(h);
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
                        playerDirty_[clientId].quests = true;
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
                    playerDirty_[clientId].quests = true;
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
                        playerDirty_[clientId].quests = true;
                        playerDirty_[clientId].stats = true;
                        playerDirty_[clientId].inventory = true;
                        enqueuePersist(clientId, PersistPriority::HIGH, PersistType::Quests);
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
            EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
            Entity* e = getWorldForClient(clientId).getEntity(h);
            if (!e) break;

            // Validate target scene exists (lookup by scene_id, not display name)
            const SceneInfoRecord* targetScene = sceneCache_.get(cmd.targetScene);
            if (!targetScene) {
                LOG_WARN("Server", "Client %d zone transition to unknown scene '%s'",
                         clientId, cmd.targetScene.c_str());
                break; // reject — scene does not exist
            }

            // Level gate: check minimum level requirement
            {
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

            // Use portal's spawn position if provided, otherwise fall back to scene default
            float spawnX = cmd.spawnX;
            float spawnY = cmd.spawnY;
            if (spawnX == 0.0f && spawnY == 0.0f) {
                auto* targetSceneDef = sceneCache_.get(cmd.targetScene);
                if (targetSceneDef) {
                    spawnX = targetSceneDef->defaultSpawnX;
                    spawnY = targetSceneDef->defaultSpawnY;
                }
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
                    PersistentId pid2(client->playerEntityId);
                    EntityHandle h2 = getReplicationForClient(clientId).getEntityHandle(pid2);
                    Entity* e2 = getWorldForClient(clientId).getEntity(h2);
                    if (e2) {
                        auto* sc2 = e2->getComponent<CharacterStatsComponent>();
                        if (sc2) {
                            sc2->stats.currentScene = cmd.targetScene;
                            sc2->stats.combatTimer = 0.0f; // H20-FIX
                        }
                    }
                    lastAutoAttackTime_.erase(clientId);
                    playerDirty_[clientId].position = true;

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
                    client->lastSentState.clear();
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
            EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
            Entity* e = getWorldForClient(clientId).getEntity(h);
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
                playerDirty_[clientId].inventory = true;
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
                playerDirty_[clientId].position = true;
                playerDirty_[clientId].vitals = true;

                // Clear AOI so client gets fresh SvEntityEnter in the new scene
                client->aoi.previous.clear();
                client->aoi.current.clear();
                client->aoi.entered.clear();
                client->aoi.left.clear();
                client->aoi.stayed.clear();
                client->lastSentState.clear();

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
            playerDirty_[clientId].position = true;
            playerDirty_[clientId].vitals = true;

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
        case PacketType::CmdEnchant: {
            auto msg = CmdEnchantMsg::read(payload);
            processEnchant(clientId, msg);
            break;
        }
        case PacketType::CmdRepair: {
            auto msg = CmdRepairMsg::read(payload);
            processRepair(clientId, msg);
            break;
        }
        case PacketType::CmdExtractCore: {
            auto msg = CmdExtractCoreMsg::read(payload);
            processExtractCore(clientId, msg);
            break;
        }
        case PacketType::CmdCraft: {
            auto msg = CmdCraftMsg::read(payload);
            processCraft(clientId, msg);
            break;
        }
        case PacketType::CmdBattlefield: {
            auto msg = CmdBattlefieldMsg::read(payload);
            processBattlefield(clientId, msg);
            break;
        }
        case PacketType::CmdArena: {
            auto msg = CmdArenaMsg::read(payload);
            processArena(clientId, msg);
            break;
        }
        case PacketType::CmdPet: {
            auto msg = CmdPetMsg::read(payload);
            processPetCommand(clientId, msg);
            break;
        }
        case PacketType::CmdBank: {
            auto msg = CmdBankMsg::read(payload);
            processBank(clientId, msg);
            break;
        }
        case PacketType::CmdSocketItem: {
            auto msg = CmdSocketItemMsg::read(payload);
            processSocketItem(clientId, msg);
            break;
        }
        case PacketType::CmdStatEnchant: {
            auto msg = CmdStatEnchantMsg::read(payload);
            processStatEnchant(clientId, msg);
            break;
        }
        case PacketType::CmdUseConsumable: {
            auto msg = CmdUseConsumableMsg::read(payload);
            processUseConsumable(clientId, msg);
            break;
        }
        case PacketType::CmdRankingQuery: {
            auto msg = CmdRankingQueryMsg::read(payload);
            processRankingQuery(clientId, msg);
            break;
        }
        case PacketType::CmdStartDungeon: {
            CmdStartDungeonMsg msg;
            msg.read(payload);
            processStartDungeon(clientId, msg);
            break;
        }
        case PacketType::CmdDungeonResponse: {
            CmdDungeonResponseMsg msg;
            msg.read(payload);
            processDungeonResponse(clientId, msg);
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

    World& world = getWorldForClient(clientId);
    ReplicationManager& repl = getReplicationForClient(clientId);

    if (msg.targetId != 0) {
        if (!TargetValidator::isInAOI(client->aoi, msg.targetId, repl)) {
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
    EntityHandle casterHandle = repl.getEntityHandle(casterPid);
    Entity* caster = world.getEntity(casterHandle);
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
        EntityHandle targetHandle = repl.getEntityHandle(targetPid);
        target = world.getEntity(targetHandle);
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
            for (int i = 0; i < 8; i++) {
                ctx.targetElementalResists[i] = targetCharStats->stats.getElementalResist(static_cast<DamageType>(i));
            }
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
        if (gameTime_ - cooldownIt->second < cooldown * 0.9f) {
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
            // Find target's clientId for dirty flags
            uint16_t skillTargetClientId = 0;
            server_.connections().forEach([&](const ClientConnection& conn) {
                if (conn.playerEntityId == msg.targetId) skillTargetClientId = conn.clientId;
            });
            if (skillTargetClientId != 0) {
                playerDirty_[skillTargetClientId].vitals = true;
            }
            // Attacker becomes Aggressor if target is innocent
            if (tgtCS->stats.pkStatus == PKStatus::White) {
                casterStatsComp->stats.flagAsAggressor();
                playerDirty_[clientId].stats = true;
                enqueuePersist(clientId, PersistPriority::HIGH, PersistType::Character);
            }
            // If attacker kills a non-flagged player → Murderer
            if (isKill && tgtCS->stats.pkStatus == PKStatus::White) {
                casterStatsComp->stats.flagAsMurderer();
                playerDirty_[clientId].stats = true;
                enqueuePersist(clientId, PersistPriority::HIGH, PersistType::Character);
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
                // Party XP bonus (L33)
                auto* partyXP = caster->getComponent<PartyComponent>();
                if (partyXP && partyXP->party.isInParty()) {
                    xp = static_cast<int>(xp * (1.0f + partyXP->party.getXPBonus()));
                }
                if (xp > 0) {
                    // WAL: record XP gain before mutating
                    auto* casterClient = server_.connections().findById(clientId);
                    if (casterClient) wal_.appendXPGain(casterClient->character_id, static_cast<int64_t>(xp));
                    casterStatsComp->stats.addXP(xp);
                    playerDirty_[clientId].stats = true;
                    enqueuePersist(clientId, PersistPriority::HIGH, PersistType::Character);
                    LOG_INFO("Server", "Client %d gained %d XP from '%s' (base=%d, mob Lv%d, player Lv%d)",
                             clientId, xp, es.enemyName.c_str(), es.xpReward,
                             es.level, casterStatsComp->stats.level);

                    // Pet XP sharing (50%)
                    auto* petCompXP = caster->getComponent<PetComponent>();
                    if (petCompXP && petCompXP->hasPet()) {
                        const auto* petDef = petDefCache_.getDefinition(petCompXP->equippedPet.petDefinitionId);
                        if (petDef) {
                            int64_t petXP = static_cast<int64_t>(xp * PetSystem::PET_XP_SHARE);
                            if (petXP > 0) {
                                int levelBefore = petCompXP->equippedPet.level;
                                PetSystem::addXP(*petDef, petCompXP->equippedPet, petXP,
                                                 casterStatsComp->stats.level);
                                playerDirty_[clientId].pet = true;
                                if (petCompXP->equippedPet.level != levelBefore) {
                                    recalcEquipmentBonuses(caster);
                                }
                                sendPetUpdate(clientId, caster);
                            }
                        }
                    }
                } else {
                    LOG_INFO("Server", "Client %d: trivial mob '%s' Lv%d — no XP (player Lv%d)",
                             clientId, es.enemyName.c_str(), es.level, casterStatsComp->stats.level);
                }
            }

            // Dungeon honor: +1 per mob, +50 for MiniBoss, to all party members in instance
            {
                uint32_t dungeonInstId = dungeonManager_.getInstanceForClient(clientId);
                if (dungeonInstId) {
                    auto* dInst = dungeonManager_.getInstance(dungeonInstId);
                    if (dInst) {
                        int honorAmount = (es.monsterType == "MiniBoss") ? 50 : 1;
                        for (uint16_t memberCid : dInst->playerClientIds) {
                            auto* memberConn = server_.connections().findById(memberCid);
                            if (!memberConn) continue;
                            PersistentId memberPid(memberConn->playerEntityId);
                            EntityHandle memberH = dInst->replication.getEntityHandle(memberPid);
                            Entity* memberPlayer = dInst->world.getEntity(memberH);
                            if (!memberPlayer) continue;
                            auto* memberCS = memberPlayer->getComponent<CharacterStatsComponent>();
                            if (memberCS) {
                                memberCS->stats.honor += honorAmount;
                                playerDirty_[memberCid].stats = true;
                                enqueuePersist(memberCid, PersistPriority::HIGH, PersistType::Character);
                                sendPlayerState(memberCid);
                            }
                        }
                    }
                }
            }

            // Determine top damager for loot ownership (party-aware)
            auto partyLookup = [&world](uint32_t entityId) -> int {
                EntityHandle h(entityId);
                auto* entity = world.getEntity(h);
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
                auto* topEntity = world.getEntity(topHandle);
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

                    Entity* dropEntity = EntityFactory::createDroppedItem(world, dropPos, false);
                    auto* dropComp = dropEntity->getComponent<DroppedItemComponent>();
                    if (dropComp) {
                        dropComp->itemId = drops[i].item.itemId;
                        dropComp->quantity = drops[i].item.quantity;
                        dropComp->enchantLevel = drops[i].item.enchantLevel;
                        dropComp->rolledStatsJson = ItemStatRoller::rolledStatsToJson(drops[i].item.rolledStats);
                        dropComp->ownerEntityId = pickOwner();  // random per item
                        dropComp->spawnTime = gameTime_;
                        dropComp->sceneId = es.sceneId;

                        const auto* def = itemDefCache_.getDefinition(drops[i].item.itemId);
                        if (def) dropComp->rarity = def->rarity;
                    }

                    PersistentId dropPid = PersistentId::generate(1);
                    repl.registerEntity(dropEntity->handle(), dropPid);
                }
            }

            // Roll gold drop
            if (es.goldDropChance > 0.0f) {
                thread_local std::mt19937 goldRng{std::random_device{}()};
                std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
                if (chanceDist(goldRng) <= es.goldDropChance && es.maxGoldDrop > 0) {
                    std::uniform_int_distribution<int> goldDist(es.minGoldDrop, es.maxGoldDrop);
                    int goldAmount = goldDist(goldRng);

                    Entity* goldEntity = EntityFactory::createDroppedItem(world, deathPos, true);
                    auto* goldComp = goldEntity->getComponent<DroppedItemComponent>();
                    if (goldComp) {
                        goldComp->isGold = true;
                        goldComp->goldAmount = goldAmount;
                        goldComp->ownerEntityId = pickOwner();  // random per item
                        goldComp->spawnTime = gameTime_;
                        goldComp->sceneId = es.sceneId;
                    }

                    PersistentId goldPid = PersistentId::generate(1);
                    repl.registerEntity(goldEntity->handle(), goldPid);
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
                playerDirty_[targetClientId].vitals = true;
                playerDirty_[targetClientId].stats = true;
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

    // Caster vitals dirty (MP/Fury may have changed from skill use)
    playerDirty_[clientId].vitals = true;

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

    World& world = getWorldForClient(clientId);
    ReplicationManager& repl = getReplicationForClient(clientId);

    if (action.targetId != 0) {
        if (!TargetValidator::isInAOI(client->aoi, action.targetId, repl)) {
            LOG_WARN("Net", "Client %u targeted entity %llu not in AOI", clientId, action.targetId);
            return;
        }
    }

    PersistentId attackerPid(client->playerEntityId);
    EntityHandle attackerHandle = repl.getEntityHandle(attackerPid);
    Entity* attacker = world.getEntity(attackerHandle);
    if (!attacker) return;

    // Find target entity
    PersistentId targetPid(action.targetId);
    EntityHandle targetHandle = repl.getEntityHandle(targetPid);
    Entity* target = world.getEntity(targetHandle);
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
        if (lastIt != lastAutoAttackTime_.end() && gameTime_ - lastIt->second < cooldown * 0.9f) return;
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
            playerDirty_[clientId].vitals = true;
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
                // Party XP bonus (L33)
                auto* partyXPAA = attacker->getComponent<PartyComponent>();
                if (partyXPAA && partyXPAA->party.isInParty()) {
                    xp = static_cast<int>(xp * (1.0f + partyXPAA->party.getXPBonus()));
                }
                if (xp > 0) {
                    // WAL: record XP gain before mutating
                    auto* attackerClient = server_.connections().findById(clientId);
                    if (attackerClient) wal_.appendXPGain(attackerClient->character_id, static_cast<int64_t>(xp));
                    charStats->stats.addXP(xp);
                    playerDirty_[clientId].stats = true;
                    enqueuePersist(clientId, PersistPriority::HIGH, PersistType::Character);
                    LOG_INFO("Server", "Client %d gained %d XP from '%s' (base=%d, mob Lv%d, player Lv%d)",
                             clientId, xp, es.enemyName.c_str(), es.xpReward,
                             es.level, charStats->stats.level);

                    // Pet XP sharing (50%)
                    auto* petCompXP = attacker->getComponent<PetComponent>();
                    if (petCompXP && petCompXP->hasPet()) {
                        const auto* petDef = petDefCache_.getDefinition(petCompXP->equippedPet.petDefinitionId);
                        if (petDef) {
                            int64_t petXP = static_cast<int64_t>(xp * PetSystem::PET_XP_SHARE);
                            if (petXP > 0) {
                                int levelBefore = petCompXP->equippedPet.level;
                                PetSystem::addXP(*petDef, petCompXP->equippedPet, petXP,
                                                 charStats->stats.level);
                                playerDirty_[clientId].pet = true;
                                if (petCompXP->equippedPet.level != levelBefore) {
                                    recalcEquipmentBonuses(attacker);
                                }
                                sendPetUpdate(clientId, attacker);
                            }
                        }
                    }
                } else {
                    LOG_INFO("Server", "Client %d: trivial mob '%s' Lv%d — no XP (player Lv%d)",
                             clientId, es.enemyName.c_str(), es.level, charStats->stats.level);
                }
            }

            // Dungeon honor: +1 per mob, +50 for MiniBoss, to all party members in instance
            {
                uint32_t dungeonInstId = dungeonManager_.getInstanceForClient(clientId);
                if (dungeonInstId) {
                    auto* dInst = dungeonManager_.getInstance(dungeonInstId);
                    if (dInst) {
                        int honorAmount = (es.monsterType == "MiniBoss") ? 50 : 1;
                        for (uint16_t memberCid : dInst->playerClientIds) {
                            auto* memberConn = server_.connections().findById(memberCid);
                            if (!memberConn) continue;
                            PersistentId memberPid(memberConn->playerEntityId);
                            EntityHandle memberH = dInst->replication.getEntityHandle(memberPid);
                            Entity* memberPlayer = dInst->world.getEntity(memberH);
                            if (!memberPlayer) continue;
                            auto* memberCS = memberPlayer->getComponent<CharacterStatsComponent>();
                            if (memberCS) {
                                memberCS->stats.honor += honorAmount;
                                playerDirty_[memberCid].stats = true;
                                enqueuePersist(memberCid, PersistPriority::HIGH, PersistType::Character);
                                sendPlayerState(memberCid);
                            }
                        }
                    }
                }
            }

            // Determine top damager for loot ownership (party-aware)
            auto partyLookup = [&world](uint32_t entityId) -> int {
                EntityHandle h(entityId);
                auto* entity = world.getEntity(h);
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
                auto* topEntity = world.getEntity(topHandle);
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

                    Entity* dropEntity = EntityFactory::createDroppedItem(world, dropPos, false);
                    auto* dropComp = dropEntity->getComponent<DroppedItemComponent>();
                    if (dropComp) {
                        dropComp->itemId = drops[i].item.itemId;
                        dropComp->quantity = drops[i].item.quantity;
                        dropComp->enchantLevel = drops[i].item.enchantLevel;
                        dropComp->rolledStatsJson = ItemStatRoller::rolledStatsToJson(drops[i].item.rolledStats);
                        dropComp->ownerEntityId = pickOwner();  // random per item
                        dropComp->spawnTime = gameTime_;
                        dropComp->sceneId = es.sceneId;

                        const auto* def = itemDefCache_.getDefinition(drops[i].item.itemId);
                        if (def) dropComp->rarity = def->rarity;
                    }

                    PersistentId dropPid = PersistentId::generate(1);
                    repl.registerEntity(dropEntity->handle(), dropPid);
                }
            }

            // Roll gold drop
            if (es.goldDropChance > 0.0f) {
                thread_local std::mt19937 goldRng{std::random_device{}()};
                std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
                if (chanceDist(goldRng) <= es.goldDropChance && es.maxGoldDrop > 0) {
                    std::uniform_int_distribution<int> goldDist(es.minGoldDrop, es.maxGoldDrop);
                    int goldAmount = goldDist(goldRng);

                    Entity* goldEntity = EntityFactory::createDroppedItem(world, deathPos, true);
                    auto* goldComp = goldEntity->getComponent<DroppedItemComponent>();
                    if (goldComp) {
                        goldComp->isGold = true;
                        goldComp->goldAmount = goldAmount;
                        goldComp->ownerEntityId = pickOwner();  // random per item
                        goldComp->spawnTime = gameTime_;
                        goldComp->sceneId = es.sceneId;
                    }

                    PersistentId goldPid = PersistentId::generate(1);
                    repl.registerEntity(goldEntity->handle(), goldPid);
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

            // Find target's clientId for dirty flags
            uint16_t pvpTargetClientId = 0;
            server_.connections().forEach([&](const ClientConnection& conn) {
                if (conn.playerEntityId == targetPid.value()) pvpTargetClientId = conn.clientId;
            });
            if (pvpTargetClientId != 0) {
                playerDirty_[pvpTargetClientId].vitals = true;
            }

            // PK status transitions for auto-attacks (same as skill path)
            if (charStats) {
                charStats->stats.enterCombat();
                targetCharStats->stats.enterCombat();
                if (targetCharStats->stats.pkStatus == PKStatus::White) {
                    charStats->stats.flagAsAggressor();
                    playerDirty_[clientId].stats = true;
                    enqueuePersist(clientId, PersistPriority::HIGH, PersistType::Character);
                }
                if (killed && targetCharStats->stats.pkStatus == PKStatus::White) {
                    charStats->stats.flagAsMurderer();
                    playerDirty_[clientId].stats = true;
                    enqueuePersist(clientId, PersistPriority::HIGH, PersistType::Character);
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
                playerDirty_[clientId].vitals = true;
                sendPlayerState(clientId);
            }

            if (killed) {
                // Award PvP kill to attacker
                if (charStats) {
                    charStats->stats.pvpKills++;
                    playerDirty_[clientId].stats = true;
                }
                // Record PvP death on target
                targetCharStats->stats.pvpDeaths++;

                // Honor system (DB-backed kill tracking)
                uint16_t targetClientId = 0;
                std::string targetCharId;
                server_.connections().forEach([&](const ClientConnection& conn) {
                    if (conn.playerEntityId == targetPid.value()) {
                        targetClientId = conn.clientId;
                        targetCharId = conn.character_id;
                    }
                });

                if (charStats && !targetCharId.empty()) {
                    int recentKills = pvpKillLogRepo_->countRecentKills(
                        client->character_id, targetCharId);
                    auto honorResult = HonorSystem::processKillWithCount(
                        charStats->stats.pkStatus, targetCharStats->stats.pkStatus,
                        recentKills, targetCharStats->stats.honor);

                    if (honorResult.attackerGain > 0) {
                        charStats->stats.honor = (std::min)(HonorSystem::MAX_HONOR,
                            charStats->stats.honor + honorResult.attackerGain);
                        playerDirty_[clientId].stats = true;
                        enqueuePersist(clientId, PersistPriority::HIGH, PersistType::Character);
                    }
                    if (honorResult.victimLoss > 0 && targetClientId != 0) {
                        targetCharStats->stats.honor = (std::max)(0,
                            targetCharStats->stats.honor - honorResult.victimLoss);
                        playerDirty_[targetClientId].stats = true;
                        enqueuePersist(targetClientId, PersistPriority::HIGH, PersistType::Character);
                    }

                    pvpKillLogRepo_->recordKill(client->character_id, targetCharId);

                    LOG_INFO("Server", "PvP honor: %s gained %d, %s lost %d (recent=%d)",
                             client->character_id.c_str(), honorResult.attackerGain,
                             targetCharId.c_str(), honorResult.victimLoss, recentKills);
                }

                // Send death notification to the killed player
                if (targetClientId != 0) {
                    playerDirty_[targetClientId].stats = true;
                    playerDirty_[targetClientId].vitals = true;
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
        EntityHandle itemHandle = repl.getEntityHandle(itemPid);
        Entity* itemEntity = world.getEntity(itemHandle);
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
                auto* ownerEntity = world.getEntity(ownerHandle);
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
            playerDirty_[clientId].inventory = true;
            enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);
            pickupMsg.isGold = 1;
            pickupMsg.goldAmount = dropComp->goldAmount;
            pickupMsg.displayName = "Gold";
        } else {
            const auto* def = itemDefCache_.getDefinition(dropComp->itemId);

            ItemInstance item;
            item.instanceId = generateItemInstanceId();
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
            playerDirty_[clientId].inventory = true;

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
        repl.unregisterEntity(itemHandle);
        world.destroyEntity(itemHandle);
    } else {
        LOG_INFO("Server", "Unhandled action type %d from client %d", action.actionType, clientId);
    }
}

void ServerApp::processPetCommand(uint16_t clientId, const CmdPetMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* charStats = player->getComponent<CharacterStatsComponent>();
    auto* petComp = player->getComponent<PetComponent>();
    if (!charStats || !petComp) return;

    const std::string& charId = client->character_id;

    if (msg.action == 0) { // Equip
        // Load all pets for this character to validate ownership
        auto pets = petRepo_->loadPets(charId);

        bool found = false;
        PetRecord targetPet;
        for (const auto& pet : pets) {
            if (pet.id == msg.petDbId) {
                found = true;
                targetPet = pet;
                break;
            }
        }
        if (!found) {
            LOG_WARN("Server", "Client %d tried to equip unknown pet %d", clientId, msg.petDbId);
            return;
        }

        // Unequip current pet, then equip new one
        petRepo_->unequipAllPets(charId);
        petRepo_->equipPet(charId, msg.petDbId);

        // Load into PetComponent
        petComp->equippedPet.petDefinitionId = targetPet.petDefId;
        petComp->equippedPet.petName         = targetPet.petName;
        petComp->equippedPet.level           = targetPet.level;
        petComp->equippedPet.currentXP       = targetPet.currentXP;
        petComp->equippedPet.isSoulbound     = targetPet.isSoulbound;
        petComp->equippedPet.autoLootEnabled  = targetPet.autoLootEnabled;
        petComp->dbPetId                     = targetPet.id;
        petComp->equippedPet.xpToNextLevel   = PetSystem::calculateXPToNextLevel(targetPet.level);

        recalcEquipmentBonuses(player);
        playerDirty_[clientId].pet = true;
        playerDirty_[clientId].vitals = true;
        playerDirty_[clientId].stats = true;
        enqueuePersist(clientId, PersistPriority::LOW, PersistType::Pet);
        sendPlayerState(clientId);
        sendPetUpdate(clientId, player);
        LOG_INFO("Server", "Client %d equipped pet '%s' (Lv%d)",
                 clientId, targetPet.petName.c_str(), targetPet.level);

    } else { // Unequip
        petRepo_->unequipAllPets(charId);

        petComp->equippedPet = PetInstance{};
        petComp->dbPetId     = 0;

        recalcEquipmentBonuses(player);
        playerDirty_[clientId].pet = true;
        playerDirty_[clientId].vitals = true;
        playerDirty_[clientId].stats = true;
        enqueuePersist(clientId, PersistPriority::LOW, PersistType::Pet);
        sendPlayerState(clientId);
        sendPetUpdate(clientId, player);
        LOG_INFO("Server", "Client %d unequipped pet", clientId);
    }
}

void ServerApp::sendPetUpdate(uint16_t clientId, Entity* player) {
    auto* petComp = player->getComponent<PetComponent>();
    SvPetUpdateMsg msg;
    if (petComp && petComp->hasPet()) {
        msg.equipped      = 1;
        msg.petDefId      = petComp->equippedPet.petDefinitionId;
        msg.petName       = petComp->equippedPet.petName;
        msg.level         = static_cast<uint8_t>(petComp->equippedPet.level);
        msg.currentXP     = static_cast<int32_t>(petComp->equippedPet.currentXP);
        msg.xpToNextLevel = static_cast<int32_t>(petComp->equippedPet.xpToNextLevel);
    }
    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvPetUpdate, buf, w.size());
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

    // Apply pet bonuses (after equipment, before recalculating stats)
    auto* petComp = player->getComponent<PetComponent>();
    if (petComp && petComp->hasPet()) {
        const auto* petDef = petDefCache_.getDefinition(petComp->equippedPet.petDefinitionId);
        if (petDef) {
            PetSystem::applyToEquipBonuses(*petDef, petComp->equippedPet,
                                           charStats->stats.equipBonusHP,
                                           charStats->stats.equipBonusCritRate);
        }
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
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
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

    // H4-FIX: Block equipment changes during an active trade session
    if (tradeRepo_->getActiveSession(client->character_id)) {
        LOG_WARN("Server", "Client %d tried to change equipment during active trade", clientId);
        return;
    }

    if (!isValidEquipmentSlot(msg.equipSlot)) {
        LOG_WARN("Server", "Client %d sent invalid equipment slot %d", clientId, msg.equipSlot);
        return;
    }
    auto targetSlot = static_cast<EquipmentSlot>(msg.equipSlot);

    // Validate class and level requirements before equipping
    if (msg.action == 0) {
        auto item = inv->inventory.getSlot(msg.inventorySlot);
        if (item.isValid()) {
            const auto* itemDef = itemDefCache_.getDefinition(item.itemId);
            if (itemDef) {
                if (!itemDef->classReq.empty() && itemDef->classReq != "All" &&
                    charStats->stats.classDef.displayName != itemDef->classReq) {
                    LOG_WARN("Server", "Client %d class %s cannot equip %s (requires %s)",
                             clientId, charStats->stats.classDef.displayName.c_str(),
                             item.itemId.c_str(), itemDef->classReq.c_str());
                    return;
                }
                if (itemDef->levelReq > charStats->stats.level) {
                    LOG_WARN("Server", "Client %d level %d too low for %s (requires %d)",
                             clientId, charStats->stats.level,
                             item.itemId.c_str(), itemDef->levelReq);
                    return;
                }
            }
        }
    }

    bool success = false;

    if (msg.action == 0) {
        success = inv->inventory.equipItem(msg.inventorySlot, targetSlot);
    } else {
        success = inv->inventory.unequipItem(targetSlot);
    }

    if (success) {
        recalcEquipmentBonuses(player);
        playerDirty_[clientId].vitals = true;
        playerDirty_[clientId].inventory = true;
        playerDirty_[clientId].stats = true;
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
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
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
    msg.honorRank   = static_cast<uint8_t>(HonorSystem::getHonorRank(s.honor));

    uint8_t buf[128];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvPlayerState, buf, w.size());
}

void ServerApp::sendSkillSync(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
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
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
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
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
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

    // Prune old PvP kill log entries (every 5 minutes)
    pvpKillLogPruneTimer_ += dt;
    if (pvpKillLogPruneTimer_ >= 300.0f) {
        pvpKillLogPruneTimer_ = 0.0f;
        int pruned = pvpKillLogRepo_->pruneOldEntries();
        if (pruned > 0) {
            LOG_INFO("Server", "Pruned %d old PvP kill log entries", pruned);
        }
    }

    // Expire stale economic nonces (>60s old)
    nonceManager_.expireAll(gameTime_);

    // Clean stale persistence dedup entries (>60s old)
    for (auto it = pendingPersist_.begin(); it != pendingPersist_.end(); ) {
        if (gameTime_ - it->second > 60.0f)
            it = pendingPersist_.erase(it);
        else
            ++it;
    }
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
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
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

    // Look up winner name — check main world first, then dungeon instances
    Entity* winnerEntity = nullptr;
    {
        EntityHandle winnerH(lootResult.topDamagerId);
        winnerEntity = world_.getEntity(winnerH);
        if (!winnerEntity) {
            for (auto& [id, inst] : dungeonManager_.allInstances()) {
                if (!inst->expired) {
                    winnerEntity = inst->world.getEntity(winnerH);
                    if (winnerEntity) break;
                }
            }
        }
    }
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
        PersistentId cpid(client.playerEntityId);
        EntityHandle ch = getReplicationForClient(client.clientId).getEntityHandle(cpid);
        auto* ce = getWorldForClient(client.clientId).getEntity(ch);
        if (!ce) return;
        auto* cs = ce->getComponent<CharacterStatsComponent>();
        if (cs && cs->stats.currentScene == scene) {
            server_.sendTo(client.clientId, Channel::ReliableOrdered,
                          PacketType::SvBossLootOwner, buf, w.size());
        }
    });
}

// ============================================================================
// processEnchant — attempt to enchant an inventory item
// ============================================================================
void ServerApp::processEnchant(uint16_t clientId, const CmdEnchantMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* charStats = player->getComponent<CharacterStatsComponent>();
    auto* inv       = player->getComponent<InventoryComponent>();
    if (!charStats || !inv) return;

    // Helper: send enchant result back to client
    auto sendResult = [&](bool success, uint8_t newLevel, bool broke, const std::string& msg_str) {
        SvEnchantResultMsg res;
        res.success  = success ? 1 : 0;
        res.newLevel = newLevel;
        res.broke    = broke ? 1 : 0;
        res.message  = msg_str;
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        res.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvEnchantResult, buf, w.size());
    };

    // 1. Validate: item exists in inventory slot
    ItemInstance item = inv->inventory.getSlot(msg.inventorySlot);
    if (!item.isValid()) {
        sendResult(false, 0, false, "Invalid item slot");
        return;
    }

    // 1b. Validate: item is not locked for trade (L17)
    if (inv->inventory.isSlotLocked(msg.inventorySlot)) {
        sendResult(false, 0, false, "Cannot enchant a trade-locked item");
        return;
    }

    // 2. Validate: item is not already broken
    if (item.isBroken) {
        sendResult(false, static_cast<uint8_t>(item.enchantLevel), false, "Item is broken — repair it first");
        return;
    }

    // 3. Look up item definition
    const CachedItemDefinition* itemDef = itemDefCache_.getDefinition(item.itemId);
    if (!itemDef) {
        sendResult(false, static_cast<uint8_t>(item.enchantLevel), false, "Unknown item");
        return;
    }

    // 4. Validate: must be weapon or armor but NOT accessory
    if (!(itemDef->isWeapon() || itemDef->isArmor()) || itemDef->isAccessory()) {
        sendResult(false, static_cast<uint8_t>(item.enchantLevel), false, "Item cannot be enchanted");
        return;
    }

    // 5. Check max enchant cap
    int targetLevel = item.enchantLevel + 1;
    int maxEnchant = static_cast<int>(EnchantConstants::MAX_ENCHANT_LEVEL);
    int cap = (itemDef->maxEnchant < maxEnchant) ? itemDef->maxEnchant : maxEnchant;
    if (targetLevel > cap) {
        sendResult(false, static_cast<uint8_t>(item.enchantLevel), false, "Item is already at max enchant level");
        return;
    }

    // 6. Find enhancement stone
    std::string requiredStone = EnchantSystem::getRequiredStone(itemDef->levelReq);
    int stoneSlot = inv->inventory.findItemById(requiredStone);
    if (stoneSlot < 0) {
        sendResult(false, static_cast<uint8_t>(item.enchantLevel), false, "Missing enhancement stone");
        return;
    }

    // 7. Check gold
    int goldCost = EnchantSystem::getGoldCost(targetLevel);
    int64_t currentGold = inv->inventory.getGold();
    if (currentGold < goldCost) {
        sendResult(false, static_cast<uint8_t>(item.enchantLevel), false, "Insufficient gold");
        return;
    }

    // 8. If protection stone requested, find it
    int protectSlot = -1;
    if (msg.useProtectionStone) {
        protectSlot = inv->inventory.findItemById("mat_protect_stone");
        if (protectSlot < 0) {
            sendResult(false, static_cast<uint8_t>(item.enchantLevel), false, "Missing protection stone");
            return;
        }
    }

    // 9. WAL: log gold deduction before consuming
    wal_.appendGoldChange(client->character_id, -static_cast<int64_t>(goldCost));

    // 10. Consume resources (H5-FIX: remove higher-indexed slot first)
    inv->inventory.setGold(currentGold - goldCost);
    if (protectSlot >= 0 && protectSlot > stoneSlot) {
        inv->inventory.removeItemQuantity(protectSlot, 1);
        inv->inventory.removeItemQuantity(stoneSlot, 1);
    } else {
        inv->inventory.removeItemQuantity(stoneSlot, 1);
        if (protectSlot >= 0) {
            inv->inventory.removeItemQuantity(protectSlot, 1);
        }
    }

    // 11. Roll enchant
    bool success = EnchantSystem::tryEnchant(targetLevel);
    bool broke = false;

    if (success) {
        item.enchantLevel = targetLevel;
    } else if (EnchantSystem::hasBreakRisk(targetLevel)) {
        if (msg.useProtectionStone) {
            // Stone was consumed; item stays at current level
        } else {
            // Break the item
            item.isBroken    = true;
            item.isSoulbound = true;
            broke = true;
        }
    }
    // Failure below safe threshold: item just stays at same level (no break)

    // 12. Write item back to inventory (remove + re-add at same slot)
    inv->inventory.removeItem(msg.inventorySlot);
    inv->inventory.addItemToSlot(msg.inventorySlot, item);

    // 13. Dirty flags: gold deducted + item modified
    playerDirty_[clientId].inventory = true;
    enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

    // 14. Send result and sync
    sendResult(success, static_cast<uint8_t>(item.enchantLevel), broke,
               success ? "Enchant succeeded!" : (broke ? "Item was destroyed!" : "Enchant failed"));
    sendPlayerState(clientId);
    sendInventorySync(clientId);

    LOG_INFO("Server", "Client %d enchant slot %d -> +%d success=%d broke=%d",
             clientId, msg.inventorySlot, item.enchantLevel, success ? 1 : 0, broke ? 1 : 0);
}

// ============================================================================
// processRepair — repair a broken inventory item using a repair scroll
// ============================================================================
void ServerApp::processRepair(uint16_t clientId, const CmdRepairMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* inv = player->getComponent<InventoryComponent>();
    if (!inv) return;

    // Helper: send repair result back to client
    auto sendResult = [&](bool success, uint8_t newLevel, const std::string& msg_str) {
        SvRepairResultMsg res;
        res.success  = success ? 1 : 0;
        res.newLevel = newLevel;
        res.message  = msg_str;
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        res.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvRepairResult, buf, w.size());
    };

    // 1. Validate: item exists and is broken
    ItemInstance item = inv->inventory.getSlot(msg.inventorySlot);
    if (!item.isValid()) {
        sendResult(false, 0, "Invalid item slot");
        return;
    }
    if (!item.isBroken) {
        sendResult(false, static_cast<uint8_t>(item.enchantLevel), "Item is not broken");
        return;
    }

    // 2. Find repair scroll
    int scrollSlot = inv->inventory.findItemById("item_repair_scroll");
    if (scrollSlot < 0) {
        sendResult(false, static_cast<uint8_t>(item.enchantLevel), "Missing repair scroll");
        return;
    }

    // 3. Check gold (50,000)
    constexpr int64_t REPAIR_GOLD_COST = 50000;
    int64_t currentGold = inv->inventory.getGold();
    if (currentGold < REPAIR_GOLD_COST) {
        sendResult(false, static_cast<uint8_t>(item.enchantLevel), "Insufficient gold (need 50,000)");
        return;
    }

    // 4. WAL: log gold deduction
    wal_.appendGoldChange(client->character_id, -REPAIR_GOLD_COST);

    // 5. Consume scroll and gold
    inv->inventory.removeItemQuantity(scrollSlot, 1);
    inv->inventory.setGold(currentGold - REPAIR_GOLD_COST);

    // 6. Repair item (stays soulbound, enchant level reset to safe random level)
    item.isBroken    = false;
    item.enchantLevel = EnchantSystem::rollRepairLevel();

    // 7. Write item back to inventory
    inv->inventory.removeItem(msg.inventorySlot);
    inv->inventory.addItemToSlot(msg.inventorySlot, item);

    // 8. Dirty flags: gold deducted + item repaired
    playerDirty_[clientId].inventory = true;
    enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

    // 9. Send result and sync
    sendResult(true, static_cast<uint8_t>(item.enchantLevel), "Item repaired successfully!");
    sendInventorySync(clientId);

    LOG_INFO("Server", "Client %d repaired slot %d -> enchant +%d",
             clientId, msg.inventorySlot, item.enchantLevel);
}

// ============================================================================
// processExtractCore — extract a core material from an inventory item
// ============================================================================
void ServerApp::processExtractCore(uint16_t clientId, const CmdExtractCoreMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* charStats = player->getComponent<CharacterStatsComponent>();
    auto* inv       = player->getComponent<InventoryComponent>();
    if (!charStats || !inv) return;

    // Helper: send extract result back to client
    auto sendResult = [&](bool success, const std::string& coreId, uint8_t qty, const std::string& message) {
        SvExtractResultMsg result;
        result.success      = success ? 1 : 0;
        result.coreItemId   = coreId;
        result.coreQuantity = qty;
        result.message      = message;
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        result.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvExtractResult, buf, w.size());
    };

    // 1. Validate item in inventory slot
    auto item = inv->inventory.getSlot(msg.itemSlot);
    if (!item.isValid()) { sendResult(false, "", 0, "Invalid item"); return; }
    if (item.isBroken)   { sendResult(false, "", 0, "Cannot extract broken items"); return; }

    // 2. Check rarity — Common cannot be extracted
    if (item.rarity == ItemRarity::Common) {
        sendResult(false, "", 0, "Common items cannot be extracted"); return;
    }

    // 3. Validate scroll
    auto scroll = inv->inventory.getSlot(msg.scrollSlot);
    if (!scroll.isValid() || scroll.itemId != "item_extraction_scroll") {
        sendResult(false, "", 0, "Missing extraction scroll"); return;
    }

    // 4. Determine core result
    auto* itemDef = itemDefCache_.getDefinition(item.itemId);
    int itemLevel = itemDef ? itemDef->levelReq : 1;
    auto coreResult = CoreExtraction::determineCoreResult(item.rarity, itemLevel, item.enchantLevel);
    if (!coreResult.success) {
        sendResult(false, "", 0, "Extraction failed"); return;
    }

    // 5. WAL + consume item and scroll
    wal_.appendItemRemove(client->character_id, msg.itemSlot);
    inv->inventory.removeItem(msg.itemSlot);
    inv->inventory.removeItemQuantity(msg.scrollSlot, 1);

    // 6. Add cores to inventory
    ItemInstance coreItem;
    coreItem.instanceId  = generateItemInstanceId();
    coreItem.itemId      = coreResult.coreItemId;
    coreItem.displayName = coreResult.coreItemId;
    coreItem.quantity    = coreResult.quantity;
    coreItem.rarity      = ItemRarity::Common;
    inv->inventory.addItem(coreItem);

    // 7. Dirty flag: item consumed + core added
    playerDirty_[clientId].inventory = true;
    enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

    // 8. Send result and sync
    sendResult(true, coreResult.coreItemId, static_cast<uint8_t>(coreResult.quantity),
               "Extracted " + std::to_string(coreResult.quantity) + "x core");
    sendInventorySync(clientId);

    LOG_INFO("Server", "Client %d extracted core from slot %d -> %dx %s",
             clientId, msg.itemSlot, coreResult.quantity, coreResult.coreItemId.c_str());
}

// ============================================================================
// getCombineBookTier — map combine book item ID to tier level
// ============================================================================
static int getCombineBookTier(const std::string& itemId) {
    if (itemId == "item_combine_novice")  return 0;
    if (itemId == "item_combine_book_1") return 1;
    if (itemId == "item_combine_book_2") return 2;
    if (itemId == "item_combine_book_3") return 3;
    return -1;
}

// ============================================================================
// processCraft — validate recipe and produce crafted item
// ============================================================================
void ServerApp::processCraft(uint16_t clientId, const CmdCraftMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* charStats = player->getComponent<CharacterStatsComponent>();
    auto* inv       = player->getComponent<InventoryComponent>();
    if (!charStats || !inv) return;

    // Helper: send craft result back to client
    auto sendResult = [&](bool success, const std::string& itemId, uint8_t qty, const std::string& message) {
        SvCraftResultMsg result;
        result.success        = success ? 1 : 0;
        result.resultItemId   = itemId;
        result.resultQuantity = qty;
        result.message        = message;
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        result.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvCraftResult, buf, w.size());
    };

    // 1. Look up recipe
    auto* recipe = recipeCache_.getRecipe(msg.recipeId);
    if (!recipe) { sendResult(false, "", 0, "Unknown recipe"); return; }

    // 2. Validate combine book tier — player needs a book with tier >= recipe tier
    int playerMaxBookTier = -1;
    int slotCount = inv->inventory.totalSlots();
    for (int i = 0; i < slotCount; ++i) {
        auto slot = inv->inventory.getSlot(i);
        if (!slot.isValid()) continue;
        int tier = getCombineBookTier(slot.itemId);
        if (tier > playerMaxBookTier) playerMaxBookTier = tier;
    }
    if (playerMaxBookTier < recipe->bookTier) {
        sendResult(false, "", 0, "Need a higher-tier Combine Book"); return;
    }

    // 3. Validate level
    if (charStats->stats.level < recipe->levelReq) {
        sendResult(false, "", 0, "Level too low"); return;
    }

    // 4. Validate class (if recipe has class requirement)
    if (!recipe->classReq.empty()) {
        std::string playerClass = charStats->stats.classDef.displayName;
        if (playerClass != recipe->classReq && recipe->classReq != "All") {
            sendResult(false, "", 0, "Wrong class for this recipe"); return;
        }
    }

    // 5. Validate ingredients
    for (const auto& ing : recipe->ingredients) {
        if (inv->inventory.countItem(ing.itemId) < ing.quantity) {
            sendResult(false, "", 0, "Missing ingredient: " + ing.itemId); return;
        }
    }

    // 6. Validate gold
    if (inv->inventory.getGold() < recipe->goldCost) {
        sendResult(false, "", 0, "Not enough gold"); return;
    }

    // 7. Pre-check: verify inventory has space for result BEFORE consuming anything
    {
        bool hasSpace = false;
        int slotCount2 = inv->inventory.totalSlots();
        for (int i = 0; i < slotCount2; ++i) {
            if (!inv->inventory.getSlot(i).isValid()) { hasSpace = true; break; }
        }
        if (!hasSpace) {
            sendResult(false, "", 0, "Inventory full"); return;
        }
    }

    // 8. WAL + consume gold
    wal_.appendGoldChange(client->character_id, -recipe->goldCost);
    inv->inventory.setGold(inv->inventory.getGold() - recipe->goldCost);

    // 9. Consume ingredients
    for (const auto& ing : recipe->ingredients) {
        int remaining = ing.quantity;
        while (remaining > 0) {
            int slotIdx = inv->inventory.findItemById(ing.itemId);
            if (slotIdx < 0) break;
            auto slotItem = inv->inventory.getSlot(slotIdx);
            int toRemove = (std::min)(remaining, slotItem.quantity);
            inv->inventory.removeItemQuantity(slotIdx, toRemove);
            remaining -= toRemove;
        }
    }

    // 10. Create result item
    ItemInstance resultItem;
    resultItem.instanceId = generateItemInstanceId();
    resultItem.itemId    = recipe->resultItemId;
    resultItem.quantity  = recipe->resultQuantity;

    auto* resultDef = itemDefCache_.getDefinition(recipe->resultItemId);
    if (resultDef) {
        resultItem.displayName = resultDef->displayName;
        if      (resultDef->rarity == "Common")    resultItem.rarity = ItemRarity::Common;
        else if (resultDef->rarity == "Uncommon")  resultItem.rarity = ItemRarity::Uncommon;
        else if (resultDef->rarity == "Rare")      resultItem.rarity = ItemRarity::Rare;
        else if (resultDef->rarity == "Epic")      resultItem.rarity = ItemRarity::Epic;
        else if (resultDef->rarity == "Legendary") resultItem.rarity = ItemRarity::Legendary;
    }

    // 11. Add to inventory (space already verified above)
    inv->inventory.addItem(resultItem);

    // 11. Dirty flag: gold + ingredients consumed + item created
    playerDirty_[clientId].inventory = true;
    enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

    // 12. Send result and sync inventory
    std::string displayLabel = resultDef ? resultDef->displayName : recipe->resultItemId;
    sendResult(true, recipe->resultItemId, static_cast<uint8_t>(recipe->resultQuantity),
               "Crafted " + displayLabel);
    sendInventorySync(clientId);

    LOG_INFO("Server", "Client %d crafted %dx %s from recipe %s",
             clientId, recipe->resultQuantity, recipe->resultItemId.c_str(), msg.recipeId.c_str());
}

// ============================================================================
// processArena — handle client arena queue registration/unregistration
// ============================================================================

void ServerApp::processArena(uint16_t clientId, const CmdArenaMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    uint32_t entityId = static_cast<uint32_t>(client->playerEntityId);

    if (msg.action == 0) { // Register
        // Validate not already in another event
        if (playerEventLocks_.count(entityId)) {
            LOG_INFO("Server", "Client %d already registered for event '%s', cannot join arena",
                     clientId, playerEventLocks_[entityId].c_str());
            return;
        }

        auto mode = static_cast<ArenaMode>(msg.mode);

        PersistentId pid(client->playerEntityId);
        EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
        Entity* player = getWorldForClient(clientId).getEntity(h);
        if (!player) return;

        auto* charStats = player->getComponent<CharacterStatsComponent>();
        auto* factionComp = player->getComponent<FactionComponent>();
        auto* partyComp = player->getComponent<PartyComponent>();
        if (!charStats || !factionComp) return;

        int requiredSize = static_cast<int>(mode);
        std::vector<uint32_t> groupIds;
        std::vector<int> levels;

        if (requiredSize == 1) {
            // Solo: just this player
            groupIds.push_back(entityId);
            levels.push_back(charStats->stats.level);
        } else {
            // Duo/Team: need party of exact size
            if (!partyComp || !partyComp->party.isInParty()) return;
            if (static_cast<int>(partyComp->party.members.size()) != requiredSize) return;

            for (const auto& member : partyComp->party.members) {
                groupIds.push_back(member.netId);
                levels.push_back(member.level);
            }
        }

        Faction faction = factionComp->faction;

        if (arenaManager_.registerGroup(groupIds, faction, mode, levels, gameTime_)) {
            for (uint32_t id : groupIds) {
                playerEventLocks_[id] = "arena";
            }
            LOG_INFO("Server", "Client %d registered for arena (mode %d, %zu players)",
                     clientId, msg.mode, groupIds.size());
        }
    } else { // Unregister
        arenaManager_.unregisterGroup(entityId);
        // playerEventLocks_ cleared via onGroupUnregistered callback
    }
}

// processBattlefield — handle client battlefield registration/unregistration
// ============================================================================

void ServerApp::processBattlefield(uint16_t clientId, const CmdBattlefieldMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    uint32_t entityId = static_cast<uint32_t>(client->playerEntityId);

    if (msg.action == 0) { // Register
        // Validate signup phase
        if (eventScheduler_.getState("battlefield") != EventState::Signup) {
            LOG_INFO("Server", "Client %d tried to register for battlefield outside signup window", clientId);
            return;
        }

        // Validate not already in another event
        if (playerEventLocks_.count(entityId)) {
            LOG_INFO("Server", "Client %d already registered for event '%s', cannot join battlefield",
                     clientId, playerEventLocks_[entityId].c_str());
            return;
        }

        // Get player entity, faction, position, scene
        PersistentId pid(client->playerEntityId);
        EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
        Entity* player = getWorldForClient(clientId).getEntity(h);
        if (!player) return;

        auto* charStats = player->getComponent<CharacterStatsComponent>();
        auto* factionComp = player->getComponent<FactionComponent>();
        auto* transform = player->getComponent<Transform>();
        if (!charStats || !factionComp) return;

        Faction faction = factionComp->faction;
        Vec2 pos = transform ? transform->position : Vec2{0.0f, 0.0f};
        std::string scene = charStats->stats.currentScene;

        if (battlefieldManager_.registerPlayer(entityId, client->character_id,
                                                faction, pos, scene)) {
            playerEventLocks_[entityId] = "battlefield";
            LOG_INFO("Server", "Client %d registered for battlefield (faction %d)",
                     clientId, static_cast<int>(faction));
        }
    } else { // Unregister
        // Only allow unregistration during signup phase
        if (eventScheduler_.getState("battlefield") != EventState::Signup) {
            LOG_INFO("Server", "Client %d tried to unregister from battlefield outside signup window", clientId);
            return;
        }

        battlefieldManager_.unregisterPlayer(entityId);
        playerEventLocks_.erase(entityId);
        LOG_INFO("Server", "Client %d unregistered from battlefield", clientId);
    }
}

// ============================================================================
// processBank — deposit/withdraw gold and items to/from player bank
// ============================================================================
void ServerApp::processBank(uint16_t clientId, const CmdBankMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* charStats = player->getComponent<CharacterStatsComponent>();
    auto* inv       = player->getComponent<InventoryComponent>();
    auto* bankComp  = player->getComponent<BankStorageComponent>();
    if (!charStats || !inv || !bankComp) return;

    // Dead players cannot use bank
    if (charStats->stats.isDead) return;

    // Helper: send bank result back to client
    auto sendResult = [&](uint8_t action, bool success, const std::string& message) {
        SvBankResultMsg res;
        res.action   = action;
        res.success  = success ? 1 : 0;
        res.bankGold = bankComp->storage.getStoredGold();
        res.message  = message;
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        res.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvBankResult, buf, w.size());
    };

    switch (msg.action) {
        case 0: { // Deposit item
            if (msg.itemId.empty() || msg.itemCount == 0) {
                sendResult(0, false, "Invalid item");
                return;
            }

            // Find item in inventory and validate count
            int slot = inv->inventory.findItemById(msg.itemId);
            if (slot < 0) {
                sendResult(0, false, "Item not found in inventory");
                return;
            }

            int available = inv->inventory.countItem(msg.itemId);
            if (available < msg.itemCount) {
                sendResult(0, false, "Not enough items in inventory");
                return;
            }

            // Get full item data before depositing (preserves metadata)
            ItemInstance depositedItem = inv->inventory.getSlot(slot);
            depositedItem.quantity = msg.itemCount;
            if (!bankComp->storage.depositItem(depositedItem)) {
                sendResult(0, false, "Bank is full");
                return;
            }
            inv->inventory.removeItemQuantity(slot, msg.itemCount);
            bankRepo_->depositItem(client->character_id, -1, msg.itemId,
                                   msg.itemCount, "", "", depositedItem.enchantLevel, 0, false);

            playerDirty_[clientId].bank = true;
            playerDirty_[clientId].inventory = true;
            enqueuePersist(clientId, PersistPriority::LOW, PersistType::Bank);
            sendResult(0, true, "Item deposited");
            sendInventorySync(clientId);

            LOG_INFO("Server", "Client %d deposited %d x %s to bank",
                     clientId, msg.itemCount, msg.itemId.c_str());
            break;
        }
        case 1: { // Withdraw item
            if (msg.itemId.empty() || msg.itemCount == 0) {
                sendResult(1, false, "Invalid item");
                return;
            }

            // Check inventory has space
            if (inv->inventory.freeSlots() <= 0) {
                sendResult(1, false, "Inventory is full");
                return;
            }

            // Try withdraw from bank (retrieves full item metadata)
            ItemInstance withdrawnItem;
            if (!bankComp->storage.withdrawItem(msg.itemId, msg.itemCount, &withdrawnItem)) {
                sendResult(1, false, "Item not in bank or insufficient quantity");
                return;
            }

            // Add to inventory with preserved metadata
            if (withdrawnItem.instanceId.empty()) withdrawnItem.instanceId = generateItemInstanceId();
            withdrawnItem.itemId = msg.itemId;
            withdrawnItem.quantity = msg.itemCount;
            inv->inventory.addItem(withdrawnItem);

            // Persist to DB: find slot index of item in bank (or -1 for removal)
            // Bank items are saved on-demand; withdraw removes the slot
            bankRepo_->withdrawItem(client->character_id, -1);

            playerDirty_[clientId].bank = true;
            playerDirty_[clientId].inventory = true;
            enqueuePersist(clientId, PersistPriority::LOW, PersistType::Bank);
            sendResult(1, true, "Item withdrawn");
            sendInventorySync(clientId);

            LOG_INFO("Server", "Client %d withdrew %d x %s from bank",
                     clientId, msg.itemCount, msg.itemId.c_str());
            break;
        }
        case 2: { // Deposit gold
            int64_t amount = msg.goldAmount;
            if (amount <= 0) {
                sendResult(2, false, "Invalid amount");
                return;
            }

            // Validate player has enough gold
            int64_t playerGold = inv->inventory.getGold();
            if (playerGold < amount) {
                sendResult(2, false, "Not enough gold");
                return;
            }

            // Try deposit with 2% fee
            if (!bankComp->storage.depositGold(amount, 0.02f)) {
                sendResult(2, false, "Deposit failed (fee exceeds amount)");
                return;
            }

            // Deduct from inventory using setGold (server-authoritative)
            inv->inventory.setGold(playerGold - amount);

            // WAL: log gold deduction
            wal_.appendGoldChange(client->character_id, -amount);

            // Persist bank gold to DB
            int64_t fee = static_cast<int64_t>(std::floor(amount * 0.02f));
            int64_t deposited = amount - fee;
            bankRepo_->depositGold(client->character_id, deposited);

            playerDirty_[clientId].bank = true;
            playerDirty_[clientId].inventory = true;
            enqueuePersist(clientId, PersistPriority::LOW, PersistType::Bank);
            sendResult(2, true, "Gold deposited");
            sendPlayerState(clientId);
            sendInventorySync(clientId);

            LOG_INFO("Server", "Client %d deposited %lld gold to bank (fee %lld, net %lld)",
                     clientId, static_cast<long long>(amount),
                     static_cast<long long>(fee),
                     static_cast<long long>(deposited));
            break;
        }
        case 3: { // Withdraw gold
            int64_t amount = msg.goldAmount;
            if (amount <= 0) {
                sendResult(3, false, "Invalid amount");
                return;
            }

            // Try withdraw from bank
            if (!bankComp->storage.withdrawGold(amount)) {
                sendResult(3, false, "Not enough gold in bank");
                return;
            }

            // Add to inventory using setGold (server-authoritative)
            int64_t playerGold = inv->inventory.getGold();
            inv->inventory.setGold(playerGold + amount);

            // Persist bank gold withdrawal to DB
            bankRepo_->withdrawGold(client->character_id, amount);

            playerDirty_[clientId].bank = true;
            playerDirty_[clientId].inventory = true;
            enqueuePersist(clientId, PersistPriority::LOW, PersistType::Bank);
            sendResult(3, true, "Gold withdrawn");
            sendPlayerState(clientId);
            sendInventorySync(clientId);

            LOG_INFO("Server", "Client %d withdrew %lld gold from bank",
                     clientId, static_cast<long long>(amount));
            break;
        }
        default:
            LOG_WARN("Server", "Client %d sent unknown bank action %d", clientId, msg.action);
            break;
    }
}

// ============================================================================
// processSocketItem — socket an accessory with a stat scroll
// ============================================================================
void ServerApp::processSocketItem(uint16_t clientId, const CmdSocketItemMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* charStats = player->getComponent<CharacterStatsComponent>();
    auto* inv       = player->getComponent<InventoryComponent>();
    if (!charStats || !inv) return;

    // Dead players cannot socket items
    if (charStats->stats.isDead) return;

    // Helper: send socket result back to client
    auto sendResult = [&](bool success, uint8_t rolledValue, uint8_t previousValue,
                          bool wasResocket, const std::string& msg_str) {
        SvSocketResultMsg res;
        res.success       = success ? 1 : 0;
        res.rolledValue   = rolledValue;
        res.previousValue = previousValue;
        res.wasResocket   = wasResocket ? 1 : 0;
        res.message       = msg_str;
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        res.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvSocketResult, buf, w.size());
    };

    // 1. Validate equipment slot enum range
    if (!isValidEquipmentSlot(msg.equipSlot)) {
        sendResult(false, 0, 0, false, "Invalid equipment slot");
        return;
    }
    auto equipSlot = static_cast<EquipmentSlot>(msg.equipSlot);

    // 2. Validate slot is socketable (Ring, Necklace, Cloak only)
    if (!SocketSystem::isSocketable(equipSlot)) {
        sendResult(false, 0, 0, false, "This item slot cannot be socketed");
        return;
    }

    // 3. Get equipped item from the slot
    ItemInstance item = inv->inventory.getEquipment(equipSlot);
    if (!item.isValid()) {
        sendResult(false, 0, 0, false, "No item equipped in that slot");
        return;
    }

    // 4. Map scroll ID to StatType
    StatType statType{};
    if (!SocketSystem::getStatFromScrollId(msg.scrollItemId, statType)) {
        sendResult(false, 0, 0, false, "Invalid stat scroll");
        return;
    }

    // 5. Find scroll in inventory
    int scrollSlot = inv->inventory.findItemById(msg.scrollItemId);
    if (scrollSlot < 0) {
        sendResult(false, 0, 0, false, "Scroll not found in inventory");
        return;
    }

    // 6. Call SocketSystem::trySocket — modifies item in-place
    SocketResultData result = SocketSystem::trySocket(item, statType, equipSlot);

    if (!result.wasSuccessful()) {
        sendResult(false, 0, 0, false, "Socket operation failed");
        return;
    }

    // 7. Consume scroll
    inv->inventory.removeItemQuantity(scrollSlot, 1);

    // 8. Write modified item back to equipment
    inv->inventory.setEquipment(equipSlot, item);

    // 9. Recalculate equipment bonuses
    recalcEquipmentBonuses(player);

    // 10. Save inventory
    saveInventoryForClient(clientId);

    // 11. Dirty flags: scroll consumed + equipment modified
    playerDirty_[clientId].inventory = true;
    playerDirty_[clientId].vitals = true;
    playerDirty_[clientId].stats = true;
    enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

    // 12. Send results to client
    sendResult(true,
               static_cast<uint8_t>(result.rolledValue),
               static_cast<uint8_t>(result.previousValue),
               result.wasResocket,
               result.wasResocket ? "Socket replaced!" : "Socket applied!");
    sendPlayerState(clientId);
    sendInventorySync(clientId);

    LOG_INFO("Server", "Client %d socketed %s slot %d with %s -> +%d (prev +%d, resocket=%d)",
             clientId, item.itemId.c_str(), msg.equipSlot, msg.scrollItemId.c_str(),
             result.rolledValue, result.previousValue, result.wasResocket ? 1 : 0);
}

// ============================================================================
// processStatEnchant — enchant an accessory with a stat bonus (tier 0-5)
// ============================================================================
void ServerApp::processStatEnchant(uint16_t clientId, const CmdStatEnchantMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* charStats = player->getComponent<CharacterStatsComponent>();
    auto* inv       = player->getComponent<InventoryComponent>();
    if (!charStats || !inv) return;

    // Dead players cannot stat-enchant
    if (charStats->stats.isDead) return;

    // Helper: send stat enchant result back to client
    auto sendResult = [&](bool success, uint8_t tier, int32_t value, const std::string& msg_str) {
        SvStatEnchantResultMsg res;
        res.success = success ? 1 : 0;
        res.tier    = tier;
        res.value   = value;
        res.message = msg_str;
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        res.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvStatEnchantResult, buf, w.size());
    };

    // 1. Validate equipment slot enum range
    if (!isValidEquipmentSlot(msg.equipSlot)) {
        sendResult(false, 0, 0, "Invalid equipment slot");
        return;
    }
    auto equipSlot = static_cast<EquipmentSlot>(msg.equipSlot);

    // 2. Get equipped item from the slot
    ItemInstance item = inv->inventory.getEquipment(equipSlot);
    if (!item.isValid()) {
        sendResult(false, 0, 0, "No item equipped in that slot");
        return;
    }

    // 3. Validate slot is stat-enchantable (Belt, Ring, Necklace, Cloak)
    if (!StatEnchantSystem::canStatEnchant(equipSlot)) {
        sendResult(false, 0, 0, "This item slot cannot be stat-enchanted");
        return;
    }

    // 4. Parse stat type from scroll
    auto statType = static_cast<StatType>(msg.scrollStatType);

    // 5. Roll tier (0-5; 0 = fail)
    int tier = StatEnchantSystem::rollStatEnchant();

    // 6. Get the stat value for this tier
    int value = StatEnchantSystem::getStatValue(statType, tier);

    // 7. Apply stat enchant — modifies item in-place (tier 0 clears enchant)
    StatEnchantSystem::applyStatEnchant(item, statType, tier);

    // 8. Write modified item back to equipment
    inv->inventory.setEquipment(equipSlot, item);

    // 9. Recalculate equipment bonuses
    recalcEquipmentBonuses(player);

    // 10. Save inventory
    saveInventoryForClient(clientId);

    // 11. Dirty flags: equipment modified
    playerDirty_[clientId].inventory = true;
    playerDirty_[clientId].vitals = true;
    playerDirty_[clientId].stats = true;
    enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

    // 12. Send results to client
    if (tier > 0) {
        sendResult(true, static_cast<uint8_t>(tier), static_cast<int32_t>(value),
                   "Stat enchant success! Tier " + std::to_string(tier));
    } else {
        sendResult(false, 0, 0, "Enchant failed");
    }
    sendPlayerState(clientId);
    sendInventorySync(clientId);

    LOG_INFO("Server", "Client %d stat-enchanted slot %d with stat %d -> tier %d value %d",
             clientId, msg.equipSlot, msg.scrollStatType, tier, value);
}

// ============================================================================
// GM Commands
// ============================================================================

uint16_t ServerApp::findClientByCharacterName(const std::string& name) {
    uint16_t result = 0;
    server_.connections().forEach([&](ClientConnection& client) {
        if (result != 0) return;
        if (client.playerEntityId == 0) return;
        PersistentId pid(client.playerEntityId);
        EntityHandle h = getReplicationForClient(client.clientId).getEntityHandle(pid);
        Entity* entity = getWorldForClient(client.clientId).getEntity(h);
        if (!entity) return;
        auto* nameplate = entity->getComponent<NameplateComponent>();
        if (nameplate && nameplate->displayName == name) {
            result = client.clientId;
        }
    });
    return result;
}

void ServerApp::initGMCommands() {
    // Helper: send a system chat message to a single client
    auto sendSystemMsg = [this](uint16_t targetClientId, const std::string& text) {
        SvChatMessageMsg sysMsg;
        sysMsg.channel    = static_cast<uint8_t>(ChatChannel::System);
        sysMsg.senderName = "System";
        sysMsg.message    = text;
        sysMsg.faction    = 0;
        uint8_t buf[512]; ByteWriter w(buf, sizeof(buf));
        sysMsg.write(w);
        server_.sendTo(targetClientId, Channel::ReliableOrdered,
                       PacketType::SvChatMessage, buf, w.size());
    };

    // /kick <player> — GM (role 1)
    gmCommands_.registerCommand({"kick", 1, [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
        if (args.empty()) { sendSystemMsg(callerId, "Usage: /kick <player>"); return; }
        auto targetId = findClientByCharacterName(args[0]);
        if (targetId == 0) { sendSystemMsg(callerId, "Player not found: " + args[0]); return; }
        server_.connections().removeClient(targetId);
        if (server_.onClientDisconnected) server_.onClientDisconnected(targetId);
        sendSystemMsg(callerId, "Kicked: " + args[0]);
        LOG_INFO("GM", "Client %d kicked '%s'", callerId, args[0].c_str());
    }});

    // /ban <player> <minutes> [reason] — GM (role 1)
    gmCommands_.registerCommand({"ban", 1, [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
        if (args.size() < 2) { sendSystemMsg(callerId, "Usage: /ban <player> <minutes> [reason]"); return; }
        int minutes = std::atoi(args[1].c_str());
        std::string reason = args.size() > 2 ? args[2] : "No reason";
        auto targetId = findClientByCharacterName(args[0]);
        if (targetId != 0) {
            server_.connections().removeClient(targetId);
            if (server_.onClientDisconnected) server_.onClientDisconnected(targetId);
        }
        sendSystemMsg(callerId, "Banned: " + args[0] + " for " + std::to_string(minutes) + " min");
        LOG_INFO("GM", "Client %d banned '%s' for %d min: %s", callerId, args[0].c_str(), minutes, reason.c_str());
    }});

    // /unban <player> — GM (role 1)
    gmCommands_.registerCommand({"unban", 1, [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
        if (args.empty()) { sendSystemMsg(callerId, "Usage: /unban <player>"); return; }
        sendSystemMsg(callerId, "Unbanned: " + args[0] + " (DB update required for full effect)");
        LOG_INFO("GM", "Client %d unbanned '%s'", callerId, args[0].c_str());
    }});

    // /tp <player> — teleport self to target — GM (role 1)
    gmCommands_.registerCommand({"tp", 1, [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
        if (args.empty()) { sendSystemMsg(callerId, "Usage: /tp <player>"); return; }
        auto targetId = findClientByCharacterName(args[0]);
        if (targetId == 0) { sendSystemMsg(callerId, "Player not found: " + args[0]); return; }

        auto* targetClient = server_.connections().findById(targetId);
        auto* callerClient = server_.connections().findById(callerId);
        if (!targetClient || !callerClient) return;

        Entity* targetEntity = getWorldForClient(targetId).getEntity(getReplicationForClient(targetId).getEntityHandle(PersistentId(targetClient->playerEntityId)));
        Entity* callerEntity = getWorldForClient(callerId).getEntity(getReplicationForClient(callerId).getEntityHandle(PersistentId(callerClient->playerEntityId)));
        if (!targetEntity || !callerEntity) return;

        auto* targetTransform = targetEntity->getComponent<Transform>();
        auto* targetStats    = targetEntity->getComponent<CharacterStatsComponent>();
        auto* callerTransform = callerEntity->getComponent<Transform>();
        auto* callerStats    = callerEntity->getComponent<CharacterStatsComponent>();
        if (!targetTransform || !targetStats || !callerTransform || !callerStats) return;

        if (callerStats->stats.currentScene != targetStats->stats.currentScene) {
            callerStats->stats.currentScene = targetStats->stats.currentScene;
            callerClient->aoi.previous.clear();
            callerClient->aoi.current.clear();
            callerClient->aoi.entered.clear();
            callerClient->aoi.left.clear();
            callerClient->aoi.stayed.clear();
            callerClient->lastSentState.clear();
            SvZoneTransitionMsg zt;
            zt.targetScene = targetStats->stats.currentScene;
            zt.spawnX = targetTransform->position.x;
            zt.spawnY = targetTransform->position.y;
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf)); zt.write(w);
            server_.sendTo(callerId, Channel::ReliableOrdered, PacketType::SvZoneTransition, buf, w.size());
        }
        callerTransform->position = targetTransform->position;
        playerDirty_[callerId].position = true;
        sendSystemMsg(callerId, "Teleported to: " + args[0]);
        LOG_INFO("GM", "Client %d teleported to '%s'", callerId, args[0].c_str());
    }});

    // /tphere <player> — summon target to self — GM (role 1)
    gmCommands_.registerCommand({"tphere", 1, [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
        if (args.empty()) { sendSystemMsg(callerId, "Usage: /tphere <player>"); return; }
        auto targetId = findClientByCharacterName(args[0]);
        if (targetId == 0) { sendSystemMsg(callerId, "Player not found: " + args[0]); return; }

        auto* callerClient = server_.connections().findById(callerId);
        auto* targetClient = server_.connections().findById(targetId);
        if (!callerClient || !targetClient) return;

        Entity* callerEntity = getWorldForClient(callerId).getEntity(getReplicationForClient(callerId).getEntityHandle(PersistentId(callerClient->playerEntityId)));
        Entity* targetEntity = getWorldForClient(targetId).getEntity(getReplicationForClient(targetId).getEntityHandle(PersistentId(targetClient->playerEntityId)));
        if (!callerEntity || !targetEntity) return;

        auto* callerTransform = callerEntity->getComponent<Transform>();
        auto* callerStats    = callerEntity->getComponent<CharacterStatsComponent>();
        auto* targetTransform = targetEntity->getComponent<Transform>();
        auto* targetStats    = targetEntity->getComponent<CharacterStatsComponent>();
        if (!callerTransform || !callerStats || !targetTransform || !targetStats) return;

        if (targetStats->stats.currentScene != callerStats->stats.currentScene) {
            targetStats->stats.currentScene = callerStats->stats.currentScene;
            targetClient->aoi.previous.clear();
            targetClient->aoi.current.clear();
            targetClient->aoi.entered.clear();
            targetClient->aoi.left.clear();
            targetClient->aoi.stayed.clear();
            targetClient->lastSentState.clear();
            SvZoneTransitionMsg zt;
            zt.targetScene = callerStats->stats.currentScene;
            zt.spawnX = callerTransform->position.x;
            zt.spawnY = callerTransform->position.y;
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf)); zt.write(w);
            server_.sendTo(targetId, Channel::ReliableOrdered, PacketType::SvZoneTransition, buf, w.size());
        }
        targetTransform->position = callerTransform->position;
        playerDirty_[targetId].position = true;
        sendSystemMsg(callerId, "Summoned: " + args[0]);
        LOG_INFO("GM", "Client %d summoned '%s'", callerId, args[0].c_str());
    }});

    // /announce <message> — GM (role 1)
    gmCommands_.registerCommand({"announce", 1, [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
        if (args.empty()) { sendSystemMsg(callerId, "Usage: /announce <message>"); return; }
        std::string msg;
        for (const auto& a : args) { if (!msg.empty()) msg += " "; msg += a; }
        SvChatMessageMsg chatMsg;
        chatMsg.channel    = static_cast<uint8_t>(ChatChannel::System);
        chatMsg.senderName = "[GM]";
        chatMsg.message    = msg;
        chatMsg.faction    = 0;
        uint8_t buf[512]; ByteWriter w(buf, sizeof(buf));
        chatMsg.write(w);
        server_.broadcast(Channel::ReliableOrdered, PacketType::SvChatMessage, buf, w.size());
        LOG_INFO("GM", "Client %d announced: %s", callerId, msg.c_str());
    }});

    // /setlevel <player> <level> — Admin (role 2)
    gmCommands_.registerCommand({"setlevel", 2, [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
        if (args.size() < 2) { sendSystemMsg(callerId, "Usage: /setlevel <player> <level>"); return; }
        auto targetId = findClientByCharacterName(args[0]);
        if (targetId == 0) { sendSystemMsg(callerId, "Player not found: " + args[0]); return; }
        int level = std::atoi(args[1].c_str());
        if (level < 1 || level > 50) { sendSystemMsg(callerId, "Level must be 1-50"); return; }

        auto* targetClient = server_.connections().findById(targetId);
        if (!targetClient) return;
        Entity* targetEntity = getWorldForClient(targetId).getEntity(getReplicationForClient(targetId).getEntityHandle(PersistentId(targetClient->playerEntityId)));
        if (!targetEntity) return;
        auto* charStats = targetEntity->getComponent<CharacterStatsComponent>();
        if (!charStats) return;

        charStats->stats.level = level;
        charStats->stats.recalculateStats();
        recalcEquipmentBonuses(targetEntity);
        sendPlayerState(targetId);
        sendSystemMsg(callerId, "Set " + args[0] + " to level " + std::to_string(level));
        LOG_INFO("GM", "Client %d set '%s' to level %d", callerId, args[0].c_str(), level);
    }});

    // /additem <player> <itemId> [quantity] — Admin (role 2)
    gmCommands_.registerCommand({"additem", 2, [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
        if (args.size() < 2) { sendSystemMsg(callerId, "Usage: /additem <player> <itemId> [qty]"); return; }
        auto targetId = findClientByCharacterName(args[0]);
        if (targetId == 0) { sendSystemMsg(callerId, "Player not found: " + args[0]); return; }
        const std::string& itemId = args[1];
        int qty = args.size() > 2 ? std::atoi(args[2].c_str()) : 1;
        if (qty < 1) qty = 1;

        const auto* itemDef = itemDefCache_.getDefinition(itemId);
        if (!itemDef) { sendSystemMsg(callerId, "Unknown item: " + itemId); return; }

        auto* targetClient = server_.connections().findById(targetId);
        if (!targetClient) return;
        Entity* targetEntity = getWorldForClient(targetId).getEntity(getReplicationForClient(targetId).getEntityHandle(PersistentId(targetClient->playerEntityId)));
        if (!targetEntity) return;
        auto* inv = targetEntity->getComponent<InventoryComponent>();
        if (!inv) return;

        ItemInstance item;
        item.itemId      = itemId;
        item.displayName = itemDef->displayName;
        item.quantity    = qty;
        item.rarity      = parseItemRarity(itemDef->rarity);
        inv->inventory.addItem(item);
        sendInventorySync(targetId);
        sendSystemMsg(callerId, "Gave " + args[0] + " " + std::to_string(qty) + "x " + itemId);
        LOG_INFO("GM", "Client %d gave '%s' %dx '%s'", callerId, args[0].c_str(), qty, itemId.c_str());
    }});

    // /addgold <player> <amount> — Admin (role 2)
    gmCommands_.registerCommand({"addgold", 2, [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
        if (args.size() < 2) { sendSystemMsg(callerId, "Usage: /addgold <player> <amount>"); return; }
        auto targetId = findClientByCharacterName(args[0]);
        if (targetId == 0) { sendSystemMsg(callerId, "Player not found: " + args[0]); return; }
        int64_t amount = std::atoll(args[1].c_str());
        if (amount <= 0) { sendSystemMsg(callerId, "Amount must be positive"); return; }

        auto* targetClient = server_.connections().findById(targetId);
        if (!targetClient) return;
        Entity* targetEntity = getWorldForClient(targetId).getEntity(getReplicationForClient(targetId).getEntityHandle(PersistentId(targetClient->playerEntityId)));
        if (!targetEntity) return;
        auto* inv = targetEntity->getComponent<InventoryComponent>();
        if (!inv) return;

        wal_.appendGoldChange(targetClient->character_id, amount);
        inv->inventory.setGold(inv->inventory.getGold() + amount);
        playerDirty_[targetId].inventory = true;
        enqueuePersist(targetId, PersistPriority::IMMEDIATE, PersistType::Inventory);
        sendPlayerState(targetId);
        sendSystemMsg(callerId, "Gave " + args[0] + " " + std::to_string(amount) + " gold");
        LOG_INFO("GM", "Client %d gave '%s' %lld gold", callerId, args[0].c_str(),
                 static_cast<long long>(amount));
    }});

    // /dungeon start <sceneId> | leave | list — Admin (role 2)
    gmCommands_.registerCommand({"dungeon", 2, [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
        if (args.empty()) {
            sendSystemMsg(callerId, "Usage: /dungeon start <sceneId> | leave | list");
            return;
        }

        if (args[0] == "start") {
            if (args.size() < 2) {
                sendSystemMsg(callerId, "Usage: /dungeon start <sceneId>");
                return;
            }
            const std::string& sceneId = args[1];
            auto* sceneInfo = sceneCache_.get(sceneId);
            if (!sceneInfo || !sceneInfo->isDungeon) {
                sendSystemMsg(callerId, "Unknown dungeon: " + sceneId);
                return;
            }
            if (dungeonManager_.getInstanceForClient(callerId) != 0) {
                sendSystemMsg(callerId, "Already in a dungeon instance");
                return;
            }

            auto* conn = server_.connections().findById(callerId);
            if (!conn) return;
            PersistentId pid(conn->playerEntityId);
            EntityHandle ph = replication_.getEntityHandle(pid);
            Entity* player = world_.getEntity(ph);
            if (!player) return;

            // Create instance (partyId = -1 for GM solo)
            uint32_t instId = dungeonManager_.createInstance(sceneId, -1, sceneInfo->difficultyTier);
            auto* inst = dungeonManager_.getInstance(instId);
            if (!inst) return;
            inst->leaderClientId = callerId;

            // Save return point
            auto* cs = player->getComponent<CharacterStatsComponent>();
            auto* transform = player->getComponent<Transform>();
            if (cs && transform) {
                inst->returnPoints[callerId] = {cs->stats.currentScene, transform->position.x, transform->position.y};
            }

            // Event lock
            playerEventLocks_[static_cast<uint32_t>(conn->playerEntityId)] = "Dungeon";

            // Transfer to instance world
            Vec2 spawnPos = {sceneInfo->defaultSpawnX, sceneInfo->defaultSpawnY};
            transferPlayerToWorld(callerId, world_, replication_, inst->world, inst->replication, spawnPos, sceneId);

            // Track
            dungeonManager_.addPlayer(instId, callerId);

            // Spawn mobs
            spawnDungeonMobs(inst);

            // Send start message
            SvDungeonStartMsg start;
            start.sceneId = sceneId;
            start.timeLimitSeconds = static_cast<uint16_t>(inst->timeLimitSeconds);
            uint8_t buf[128];
            ByteWriter w(buf, sizeof(buf));
            start.write(w);
            server_.sendTo(callerId, Channel::ReliableOrdered, PacketType::SvDungeonStart, buf, w.size());

            sendSystemMsg(callerId, "Dungeon instance " + std::to_string(instId) + " created: " + sceneInfo->sceneName);
            LOG_INFO("GM", "Client %d started dungeon '%s' (instance %u)", callerId, sceneId.c_str(), instId);
        }
        else if (args[0] == "leave") {
            uint32_t instId = dungeonManager_.getInstanceForClient(callerId);
            if (instId == 0) {
                sendSystemMsg(callerId, "Not in a dungeon");
                return;
            }
            endDungeonInstance(instId, 2); // reason=abandoned
            sendSystemMsg(callerId, "Left dungeon instance " + std::to_string(instId));
            LOG_INFO("GM", "Client %d left dungeon instance %u", callerId, instId);
        }
        else if (args[0] == "list") {
            size_t count = dungeonManager_.instanceCount();
            std::string msg = "Active dungeons: " + std::to_string(count);
            for (auto& [id, inst] : dungeonManager_.allInstances()) {
                msg += "\n  #" + std::to_string(id)
                     + " " + inst->sceneId
                     + " | " + std::to_string(inst->playerClientIds.size()) + " players"
                     + " | " + std::to_string(static_cast<int>(inst->elapsedTime)) + "s elapsed"
                     + (inst->completed ? " [COMPLETED]" : "")
                     + (inst->expired ? " [EXPIRED]" : "");
            }
            sendSystemMsg(callerId, msg);
        }
        else {
            sendSystemMsg(callerId, "Usage: /dungeon start <sceneId> | leave | list");
        }
    }});

    LOG_INFO("Server", "Registered %zu GM commands", gmCommands_.size());
}

// ============================================================================
// processUseConsumable — consume a potion/scroll from inventory
// ============================================================================
void ServerApp::processUseConsumable(uint16_t clientId, const CmdUseConsumableMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* charStats = player->getComponent<CharacterStatsComponent>();
    auto* inv       = player->getComponent<InventoryComponent>();
    if (!charStats || !inv) return;

    // Helper: send consume result back to client
    auto sendResult = [&](bool success, const std::string& msg_str) {
        SvConsumeResultMsg res;
        res.success = success ? 1 : 0;
        res.message = msg_str;
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        res.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvConsumeResult, buf, w.size());
    };

    // 1. Dead players cannot use consumables
    if (charStats->stats.isDead) {
        sendResult(false, "Cannot use items while dead");
        return;
    }

    // 2. Get item from inventory slot
    int slotIndex = static_cast<int>(msg.inventorySlot);
    ItemInstance item = inv->inventory.getSlot(slotIndex);
    if (!item.isValid()) {
        sendResult(false, "No item in that slot");
        return;
    }

    // 3. Determine if item is consumable using definition cache
    bool isConsumable = false;
    std::string subtype;
    int healAmount = 0;
    int manaAmount = 0;

    const CachedItemDefinition* def = itemDefCache_.getDefinition(item.itemId);
    if (def) {
        // Check itemType from the definition cache
        if (def->itemType != "Consumable") {
            sendResult(false, "This item is not consumable");
            return;
        }
        isConsumable = true;
        subtype = def->subtype;

        // Try to get amounts from attributes JSON, fall back to defaults
        if (subtype == "hp_potion" || subtype == "HpPotion") {
            healAmount = def->getIntAttribute("heal_amount", 50);
        } else if (subtype == "mp_potion" || subtype == "MpPotion") {
            manaAmount = def->getIntAttribute("mana_amount", 30);
        } else if (subtype == "hp_mp_potion" || subtype == "HpMpPotion") {
            healAmount = def->getIntAttribute("heal_amount", 50);
            manaAmount = def->getIntAttribute("mana_amount", 30);
        } else {
            // Generic consumable — try attributes for any heal/mana values
            healAmount = def->getIntAttribute("heal_amount", 0);
            manaAmount = def->getIntAttribute("mana_amount", 0);
        }
    } else {
        // No cache entry — fallback: check item ID for common patterns
        const std::string& id = item.itemId;
        if (id.find("hp") != std::string::npos || id.find("health") != std::string::npos ||
            id.find("HP") != std::string::npos || id.find("Health") != std::string::npos) {
            isConsumable = true;
            healAmount = 50;
        } else if (id.find("mp") != std::string::npos || id.find("mana") != std::string::npos ||
                   id.find("MP") != std::string::npos || id.find("Mana") != std::string::npos) {
            isConsumable = true;
            manaAmount = 30;
        } else if (id.find("potion_") == 0 || id.find("Potion_") == 0) {
            isConsumable = true;
            healAmount = 50;  // default to HP potion
        }
    }

    if (!isConsumable) {
        sendResult(false, "Cannot use this item");
        return;
    }

    // 4. Check cooldown (5 seconds, reusing skillCooldowns_ map)
    static constexpr float CONSUMABLE_COOLDOWN = 5.0f;
    auto& cooldowns = skillCooldowns_[clientId];
    auto cdIt = cooldowns.find(item.itemId);
    if (cdIt != cooldowns.end()) {
        float elapsed = gameTime_ - cdIt->second;
        if (elapsed < CONSUMABLE_COOLDOWN) {
            sendResult(false, "Still on cooldown");
            return;
        }
    }

    // 4b. Check cooldown group -- items sharing a group share cooldowns (L16)
    int cooldownGroup = def ? def->getIntAttribute("cooldown_group", 0) : 0;
    if (cooldownGroup > 0) {
        for (const auto& [cdItemId, cdTime] : cooldowns) {
            if (cdItemId == item.itemId) continue;
            float elapsed = gameTime_ - cdTime;
            if (elapsed < CONSUMABLE_COOLDOWN) {
                const CachedItemDefinition* cdDef = itemDefCache_.getDefinition(cdItemId);
                if (cdDef && cdDef->getIntAttribute("cooldown_group", 0) == cooldownGroup) {
                    sendResult(false, "Another item in this group is on cooldown");
                    return;
                }
            }
        }
    }

    // 5. If no effect can be determined, reject
    if (healAmount <= 0 && manaAmount <= 0) {
        sendResult(false, "Cannot use this item");
        return;
    }

    // 6. Apply effects
    std::string effectMsg;
    if (healAmount > 0) {
        charStats->stats.currentHP = std::min(
            charStats->stats.currentHP + healAmount,
            charStats->stats.maxHP);
        effectMsg = "Restored " + std::to_string(healAmount) + " HP";
    }
    if (manaAmount > 0) {
        charStats->stats.currentMP = std::min(
            charStats->stats.currentMP + manaAmount,
            charStats->stats.maxMP);
        if (!effectMsg.empty()) effectMsg += ", ";
        effectMsg += "Restored " + std::to_string(manaAmount) + " MP";
    }

    // 7. Consume one unit of the item
    inv->inventory.removeItemQuantity(slotIndex, 1);

    // 8. Update cooldown
    cooldowns[item.itemId] = gameTime_;

    // 9. WAL log the item removal
    wal_.appendItemRemove(client->character_id, slotIndex);

    // 10. Dirty flags: vitals changed (HP/MP), inventory changed (item consumed)
    playerDirty_[clientId].vitals = true;
    playerDirty_[clientId].inventory = true;
    enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

    // 11. Send results
    sendResult(true, effectMsg);
    sendPlayerState(clientId);
    sendInventorySync(clientId);

    LOG_INFO("Server", "Client %d used consumable '%s' from slot %d: %s",
             clientId, item.itemId.c_str(), slotIndex, effectMsg.c_str());
}

// ============================================================================
// processRankingQuery — paginated leaderboard with 60s DB cache
// ============================================================================
void ServerApp::processRankingQuery(uint16_t clientId, const CmdRankingQueryMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    auto category = static_cast<RankingCategory>(msg.category);

    // Validate category
    if (msg.category > static_cast<uint8_t>(RankingCategory::Honor)) {
        LOG_WARN("Server", "Client %d sent invalid ranking category %d", clientId, msg.category);
        return;
    }

    // Refresh cache from DB if stale
    if (!rankingMgr_.isCacheValid(gameTime_)) {
        try {
            if (gameDbConn_.isConnected()) {
                pqxx::work txn(gameDbConn_.connection());

                // --- Player rankings (global, by level/xp) ---
                auto playerResult = txn.exec(
                    "SELECT character_name, class_name, level, current_xp, honor, pvp_kills, pvp_deaths "
                    "FROM characters ORDER BY level DESC, current_xp DESC LIMIT 500"
                );

                std::vector<PlayerRankingEntry> globalEntries;
                std::vector<PlayerRankingEntry> warriorEntries;
                std::vector<PlayerRankingEntry> mageEntries;
                std::vector<PlayerRankingEntry> archerEntries;

                int globalRank = 1;
                int warriorRank = 1;
                int mageRank = 1;
                int archerRank = 1;

                for (const auto& row : playerResult) {
                    PlayerRankingEntry entry;
                    entry.characterName = row["character_name"].as<std::string>("");
                    entry.classType     = row["class_name"].as<std::string>("");
                    entry.level         = row["level"].as<int>(1);
                    entry.characterId   = globalRank; // use rank as ID placeholder
                    entry.rankPosition  = globalRank++;
                    globalEntries.push_back(entry);

                    // Class-filtered lists
                    if (entry.classType == "Warrior") {
                        PlayerRankingEntry ce = entry;
                        ce.rankPosition = warriorRank++;
                        warriorEntries.push_back(ce);
                    } else if (entry.classType == "Mage") {
                        PlayerRankingEntry ce = entry;
                        ce.rankPosition = mageRank++;
                        mageEntries.push_back(ce);
                    } else if (entry.classType == "Archer") {
                        PlayerRankingEntry ce = entry;
                        ce.rankPosition = archerRank++;
                        archerEntries.push_back(ce);
                    }
                }

                rankingMgr_.setPlayerRankings(RankingCategory::PlayersGlobal, std::move(globalEntries));
                rankingMgr_.setPlayerRankings(RankingCategory::PlayersWarrior, std::move(warriorEntries));
                rankingMgr_.setPlayerRankings(RankingCategory::PlayersMage, std::move(mageEntries));
                rankingMgr_.setPlayerRankings(RankingCategory::PlayersArcher, std::move(archerEntries));

                // --- Honor rankings ---
                auto honorResult = txn.exec(
                    "SELECT character_name, class_name, level, honor, pvp_kills, pvp_deaths "
                    "FROM characters ORDER BY honor DESC LIMIT 500"
                );

                std::vector<HonorRankingEntry> honorEntries;
                int honorRank = 1;
                for (const auto& row : honorResult) {
                    HonorRankingEntry entry;
                    entry.characterName = row["character_name"].as<std::string>("");
                    entry.classType     = row["class_name"].as<std::string>("");
                    entry.level         = row["level"].as<int>(1);
                    entry.honor         = row["honor"].as<int>(0);
                    entry.pvpKills      = row["pvp_kills"].as<int>(0);
                    entry.pvpDeaths     = row["pvp_deaths"].as<int>(0);
                    entry.characterId   = std::to_string(honorRank);
                    entry.rankPosition  = honorRank++;
                    honorEntries.push_back(entry);
                }
                rankingMgr_.setHonorRankings(std::move(honorEntries));

                // --- Guild rankings ---
                auto guildResult = txn.exec(
                    "SELECT g.id, g.name, g.level, "
                    "(SELECT COUNT(*) FROM guild_members gm WHERE gm.guild_id = g.id) as member_count, "
                    "(SELECT c.character_name FROM characters c "
                    " JOIN guild_members gm2 ON gm2.character_id = c.id "
                    " WHERE gm2.guild_id = g.id AND gm2.rank = 0 LIMIT 1) as owner_name "
                    "FROM guilds g ORDER BY g.level DESC LIMIT 500"
                );

                std::vector<GuildRankingEntry> guildEntries;
                int guildRank = 1;
                for (const auto& row : guildResult) {
                    GuildRankingEntry entry;
                    entry.guildId     = row["id"].as<int>(0);
                    entry.guildName   = row["name"].as<std::string>("");
                    entry.guildLevel  = row["level"].as<int>(1);
                    entry.memberCount = row["member_count"].as<int>(0);
                    entry.ownerName   = row["owner_name"].as<std::string>("");
                    entry.rankPosition = guildRank++;
                    guildEntries.push_back(entry);
                }
                rankingMgr_.setGuildRankings(std::move(guildEntries));

                txn.commit();
                rankingMgr_.setCacheTime(gameTime_);

                LOG_INFO("Server", "Ranking cache refreshed from DB");
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Server", "Failed to refresh ranking cache: %s", e.what());
            // Serve stale data — better than nothing
        }
    }

    // Build JSON response from cached data
    nlohmann::json jsonEntries = nlohmann::json::array();
    uint16_t totalEntries = 0;

    if (category == RankingCategory::Guilds) {
        auto entries = rankingMgr_.getGuildRankings(msg.page);
        totalEntries = static_cast<uint16_t>(entries.size());
        for (const auto& e : entries) {
            jsonEntries.push_back({
                {"rank", e.rankPosition},
                {"guildId", e.guildId},
                {"guildName", e.guildName},
                {"guildLevel", e.guildLevel},
                {"memberCount", e.memberCount},
                {"ownerName", e.ownerName}
            });
        }
    } else if (category == RankingCategory::Honor) {
        auto entries = rankingMgr_.getHonorRankings(msg.page);
        totalEntries = static_cast<uint16_t>(entries.size());
        for (const auto& e : entries) {
            jsonEntries.push_back({
                {"rank", e.rankPosition},
                {"characterName", e.characterName},
                {"classType", e.classType},
                {"level", e.level},
                {"honor", e.honor},
                {"pvpKills", e.pvpKills},
                {"pvpDeaths", e.pvpDeaths},
                {"kdRatio", e.getKDRatio()}
            });
        }
    } else {
        // PlayersGlobal, PlayersWarrior, PlayersMage, PlayersArcher
        auto entries = rankingMgr_.getPlayerRankings(category, msg.page);
        totalEntries = static_cast<uint16_t>(entries.size());
        for (const auto& e : entries) {
            jsonEntries.push_back({
                {"rank", e.rankPosition},
                {"characterName", e.characterName},
                {"classType", e.classType},
                {"level", e.level}
            });
        }
    }

    // Send response
    SvRankingResultMsg result;
    result.category     = msg.category;
    result.page         = msg.page;
    result.totalEntries = totalEntries;
    result.entriesJson  = jsonEntries.dump();

    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    result.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvRankingResult, buf, w.size());

    LOG_INFO("Server", "Client %d queried rankings cat=%d page=%d (%d entries)",
             clientId, msg.category, msg.page, totalEntries);
}

void ServerApp::tickPetAutoLoot(float dt) {
    static constexpr float AUTO_LOOT_INTERVAL = 0.5f;
    petAutoLootTimer_ += dt;
    if (petAutoLootTimer_ < AUTO_LOOT_INTERVAL) return;
    petAutoLootTimer_ = 0.0f;

    // Per-world auto-loot processing
    struct DroppedItemInfo {
        Entity* entity;
        Vec2 pos;
        DroppedItemComponent* drop;
    };

    auto processAutoLootForWorld = [this](World& w, ReplicationManager& r) {
        std::vector<DroppedItemInfo> droppedItems;
        w.forEach<DroppedItemComponent>([&](Entity* e, DroppedItemComponent* d) {
            if (d->claimedBy != 0) return;
            auto* t = e->getComponent<Transform>();
            if (!t) return;
            droppedItems.push_back({e, t->position, d});
        });

        if (droppedItems.empty()) return;

        std::vector<EntityHandle> toDestroy;

        w.forEach<CharacterStatsComponent>([&](Entity* player, CharacterStatsComponent* cs) {
            auto* petComp = player->getComponent<PetComponent>();
            if (!petComp || !petComp->hasPet() || !petComp->equippedPet.autoLootEnabled) return;

            auto* playerTransform = player->getComponent<Transform>();
            if (!playerTransform) return;

            uint16_t clientId = 0;
            auto playerHandle = player->handle();
            server_.connections().forEach([&](const ClientConnection& conn) {
                PersistentId pid(conn.playerEntityId);
                EntityHandle h = r.getEntityHandle(pid);
                if (h == playerHandle) {
                    clientId = conn.clientId;
                }
            });
            if (clientId == 0) return;

            auto* inv = player->getComponent<InventoryComponent>();
            if (!inv) return;

            float radiusSq = petComp->autoLootRadius * petComp->autoLootRadius;
            Vec2 pPos = playerTransform->position;

            for (auto& info : droppedItems) {
                if (info.drop->claimedBy != 0) continue;
                if (info.drop->sceneId != cs->stats.currentScene) continue;

                bool canLoot = (info.drop->ownerEntityId == 0 ||
                               info.drop->ownerEntityId == playerHandle.value);
                if (!canLoot) {
                    auto* partyComp = player->getComponent<PartyComponent>();
                    if (partyComp && partyComp->party.isInParty() &&
                        partyComp->party.lootMode == PartyLootMode::FreeForAll) {
                        EntityHandle ownerH(info.drop->ownerEntityId);
                        auto* ownerEntity = w.getEntity(ownerH);
                        if (ownerEntity) {
                            auto* ownerParty = ownerEntity->getComponent<PartyComponent>();
                            if (ownerParty && ownerParty->party.isInParty() &&
                                ownerParty->party.partyId == partyComp->party.partyId) {
                                canLoot = true;
                            }
                        }
                    }
                }
                if (!canLoot) continue;

                float dx = pPos.x - info.pos.x;
                float dy = pPos.y - info.pos.y;
                if (dx * dx + dy * dy > radiusSq) continue;

                if (!info.drop->tryClaim(playerHandle.value)) continue;

                if (info.drop->isGold) {
                    auto* client = server_.connections().findById(clientId);
                    if (client) wal_.appendGoldChange(client->character_id, static_cast<int64_t>(info.drop->goldAmount));
                    inv->inventory.setGold(inv->inventory.getGold() + info.drop->goldAmount);
                    playerDirty_[clientId].inventory = true;
                    enqueuePersist(clientId, PersistPriority::LOW, PersistType::Inventory);

                    SvLootPickupMsg pickup;
                    pickup.isGold = 1;
                    pickup.goldAmount = info.drop->goldAmount;
                    pickup.displayName = "Gold";
                    uint8_t buf[256];
                    ByteWriter bw(buf, sizeof(buf));
                    pickup.write(bw);
                    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvLootPickup, buf, bw.size());
                } else {
                    const auto* itemDef = itemDefCache_.getDefinition(info.drop->itemId);

                    ItemInstance item;
                    item.instanceId   = generateItemInstanceId();
                    item.itemId       = info.drop->itemId;
                    item.quantity     = info.drop->quantity;
                    item.enchantLevel = info.drop->enchantLevel;
                    item.rolledStats  = ItemStatRoller::parseRolledStats(info.drop->rolledStatsJson);
                    item.rarity       = parseItemRarity(info.drop->rarity);
                    item.displayName  = itemDef ? itemDef->displayName : info.drop->itemId;

                    auto* client = server_.connections().findById(clientId);
                    if (client) wal_.appendItemAdd(client->character_id, -1, item.instanceId);

                    if (!inv->inventory.addItem(item)) {
                        info.drop->releaseClaim();
                        continue;
                    }
                    playerDirty_[clientId].inventory = true;

                    SvLootPickupMsg pickup;
                    pickup.itemId = info.drop->itemId;
                    pickup.quantity = info.drop->quantity;
                    pickup.rarity = info.drop->rarity;
                    pickup.displayName = itemDef ? itemDef->displayName : info.drop->itemId;
                    if (info.drop->enchantLevel > 0) {
                        pickup.displayName += " +" + std::to_string(info.drop->enchantLevel);
                    }
                    uint8_t buf[256];
                    ByteWriter bw(buf, sizeof(buf));
                    pickup.write(bw);
                    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvLootPickup, buf, bw.size());

                    sendInventorySync(clientId);
                }

                sendPlayerState(clientId);
                toDestroy.push_back(info.entity->handle());
            }
        });

        for (auto handle : toDestroy) {
            r.unregisterEntity(handle);
            w.destroyEntity(handle);
        }
    };

    // Process main world
    processAutoLootForWorld(world_, replication_);

    // Process dungeon instances
    for (auto& [id, inst] : dungeonManager_.allInstances()) {
        if (!inst->expired) {
            processAutoLootForWorld(inst->world, inst->replication);
        }
    }
}

void ServerApp::tickDungeonInstances(float dt) {
    dungeonManager_.tick(dt);

    // Tick replication for each active instance
    for (auto& [id, inst] : dungeonManager_.allInstances()) {
        if (!inst->expired) {
            inst->replication.update(inst->world, server_);
        }
    }

    // Check invite timeouts (30s)
    {
        uint32_t cancelId = 0;
        for (auto& [id, inst] : dungeonManager_.allInstances()) {
            if (!inst->pendingAccepts.empty()) {
                inst->inviteTimer += dt;
                if (inst->inviteTimer >= DungeonInstance::INVITE_TIMEOUT) {
                    cancelId = id;
                    break;
                }
            }
        }
        if (cancelId) {
            LOG_INFO("Server", "Dungeon invite timed out for instance %u", cancelId);
            dungeonManager_.destroyInstance(cancelId);
        }
    }

    // Boss kill detection -- check for dead MiniBoss in each active instance
    for (auto& [id, inst] : dungeonManager_.allInstances()) {
        if (inst->completed || inst->expired) continue;
        bool bossKilled = false;
        inst->world.forEach<EnemyStatsComponent>([&](Entity* e, EnemyStatsComponent* es) {
            if (!bossKilled && !es->stats.isAlive && es->stats.monsterType == "MiniBoss") {
                bossKilled = true;
            }
        });
        if (bossKilled) {
            inst->completed = true;
            inst->celebrationTimer = 15.0f;
            distributeDungeonRewards(inst.get());
            LOG_INFO("Server", "Dungeon instance %u boss killed! 15s celebration", id);
        }
    }

    // Handle timed-out instances (10 min expired, boss still alive)
    for (uint32_t id : dungeonManager_.getTimedOutInstances()) {
        endDungeonInstance(id, 1); // reason=timeout
        break; // iterator may be invalidated
    }

    // Handle celebration finished (15s after boss kill)
    for (uint32_t id : dungeonManager_.getCelebrationFinishedInstances()) {
        endDungeonInstance(id, 0); // reason=boss_killed
        break; // iterator may be invalidated
    }

    // Handle all-disconnect (no players left in active instance)
    for (uint32_t id : dungeonManager_.getEmptyActiveInstances()) {
        auto* inst = dungeonManager_.getInstance(id);
        if (inst) {
            LOG_INFO("Server", "Dungeon instance %u empty -- destroying (no loot)", id);
            inst->expired = true;
            dungeonManager_.destroyInstance(id);
            break; // iterator invalidated
        }
    }
}

// ---------------------------------------------------------------------------
// distributeDungeonRewards -- gold, honor, treasure box to all party members
// ---------------------------------------------------------------------------
void ServerApp::distributeDungeonRewards(DungeonInstance* inst) {
    int64_t goldReward = static_cast<int64_t>(10000) * inst->difficultyTier;
    std::string treasureBoxId = "boss_treasure_box_t" + std::to_string(inst->difficultyTier);

    for (uint16_t cid : inst->playerClientIds) {
        auto* conn = server_.connections().findById(cid);
        if (!conn) continue;
        PersistentId pid(conn->playerEntityId);
        EntityHandle ph = inst->replication.getEntityHandle(pid);
        Entity* player = inst->world.getEntity(ph);
        if (!player) continue;

        auto* cs = player->getComponent<CharacterStatsComponent>();
        auto* inv = player->getComponent<InventoryComponent>();
        if (!cs || !inv) continue;

        // Gold reward (WAL-logged, server-authoritative)
        wal_.appendGoldChange(conn->character_id, goldReward);
        inv->inventory.setGold(inv->inventory.getGold() + goldReward);

        // Boss honor (+50)
        cs->stats.honor += 50;
        playerDirty_[cid].stats = true;
        enqueuePersist(cid, PersistPriority::HIGH, PersistType::Character);

        // Treasure box to inventory (silently skip if full)
        auto* boxDef = itemDefCache_.getDefinition(treasureBoxId);
        if (boxDef) {
            ItemInstance box;
            box.itemId = treasureBoxId;
            box.quantity = 1;
            box.instanceId = generateItemInstanceId();
            box.rarity = parseItemRarity(boxDef->rarity);
            int slot = inv->inventory.addItem(box);
            if (slot >= 0) {
                wal_.appendItemAdd(conn->character_id, -1, box.instanceId);
            }
        }

        sendPlayerState(cid);
        sendInventorySync(cid);

        LOG_INFO("Server", "Dungeon rewards for client %d: %lld gold, 50 honor, treasure box '%s'",
                 cid, static_cast<long long>(goldReward), treasureBoxId.c_str());
    }
}

// ---------------------------------------------------------------------------
// endDungeonInstance -- send end msg, teleport players back, cleanup
// ---------------------------------------------------------------------------
void ServerApp::endDungeonInstance(uint32_t instanceId, uint8_t reason) {
    auto* inst = dungeonManager_.getInstance(instanceId);
    if (!inst) return;

    LOG_INFO("Server", "Ending dungeon instance %u (reason=%u)", instanceId, reason);

    // Send end message to all players
    SvDungeonEndMsg endMsg;
    endMsg.reason = reason;
    uint8_t buf[8];
    ByteWriter w(buf, sizeof(buf));
    endMsg.write(w);

    // Copy client list (will be modified during iteration)
    std::vector<uint16_t> clients = inst->playerClientIds;

    for (uint16_t cid : clients) {
        server_.sendTo(cid, Channel::ReliableOrdered, PacketType::SvDungeonEnd, buf, w.size());

        // Save old entityId for event lock cleanup BEFORE transfer changes it
        auto* conn = server_.connections().findById(cid);
        uint64_t oldEntityId = conn ? conn->playerEntityId : 0;

        // Get return point
        Vec2 returnPos = {0.0f, 0.0f};
        std::string returnScene = "WhisperingWoods";
        auto it = inst->returnPoints.find(cid);
        if (it != inst->returnPoints.end()) {
            returnPos = {it->second.x, it->second.y};
            returnScene = it->second.scene;
        }

        // Transfer back to overworld
        transferPlayerToWorld(cid, inst->world, inst->replication, world_, replication_, returnPos, returnScene);

        // Clear event lock using OLD entityId (before transfer changed it)
        if (oldEntityId) {
            playerEventLocks_.erase(static_cast<uint32_t>(oldEntityId));
        }

        dungeonManager_.removePlayer(instanceId, cid);
    }

    dungeonManager_.destroyInstance(instanceId);
}

// ============================================================================
// Dungeon instance routing helpers
// ============================================================================

World& ServerApp::getWorldForClient(uint16_t clientId) {
    uint32_t instId = dungeonManager_.getInstanceForClient(clientId);
    if (instId) {
        auto* inst = dungeonManager_.getInstance(instId);
        if (inst) return inst->world;
    }
    return world_;
}

ReplicationManager& ServerApp::getReplicationForClient(uint16_t clientId) {
    uint32_t instId = dungeonManager_.getInstanceForClient(clientId);
    if (instId) {
        auto* inst = dungeonManager_.getInstance(instId);
        if (inst) return inst->replication;
    }
    return replication_;
}

// ---------------------------------------------------------------------------
// transferPlayerToWorld — move a player entity between Worlds
// ---------------------------------------------------------------------------
EntityHandle ServerApp::transferPlayerToWorld(uint16_t clientId,
                                              World& srcWorld, ReplicationManager& srcRepl,
                                              World& dstWorld, ReplicationManager& dstRepl,
                                              Vec2 spawnPos, const std::string& newScene) {
    auto* conn = server_.connections().findById(clientId);
    if (!conn) return EntityHandle();

    // Resolve source entity via PersistentId -> EntityHandle
    PersistentId oldPid(conn->playerEntityId);
    EntityHandle srcHandle = srcRepl.getEntityHandle(oldPid);
    Entity* srcEntity = srcWorld.getEntity(srcHandle);
    if (!srcEntity) {
        LOG_ERROR("Server", "transferPlayerToWorld: source entity not found for client %u", clientId);
        return EntityHandle();
    }

    // ---- 1. Snapshot component data from source entity ----
    auto* srcStats    = srcEntity->getComponent<CharacterStatsComponent>();
    auto* srcInv      = srcEntity->getComponent<InventoryComponent>();
    auto* srcSkills   = srcEntity->getComponent<SkillManagerComponent>();
    auto* srcPet      = srcEntity->getComponent<PetComponent>();
    auto* srcFaction  = srcEntity->getComponent<FactionComponent>();
    auto* srcParty    = srcEntity->getComponent<PartyComponent>();
    auto* srcSprite   = srcEntity->getComponent<SpriteComponent>();
    auto* srcCtrl     = srcEntity->getComponent<PlayerController>();
    auto* srcNameplate = srcEntity->getComponent<NameplateComponent>();
    auto* srcCombat   = srcEntity->getComponent<CombatControllerComponent>();
    auto* srcQuest    = srcEntity->getComponent<QuestComponent>();
    auto* srcBank     = srcEntity->getComponent<BankStorageComponent>();
    auto* srcChat     = srcEntity->getComponent<ChatComponent>();
    auto* srcGuild    = srcEntity->getComponent<GuildComponent>();
    auto* srcFriends  = srcEntity->getComponent<FriendsComponent>();
    auto* srcMarket   = srcEntity->getComponent<MarketComponent>();
    auto* srcTrade    = srcEntity->getComponent<TradeComponent>();

    // Deep-copy mutable data before we destroy the source entity
    CharacterStats  savedStats;
    if (srcStats) savedStats = srcStats->stats;

    Inventory savedInv;
    if (srcInv) savedInv = srcInv->inventory;

    SkillManager savedSkills;
    if (srcSkills) savedSkills = srcSkills->skills;

    PetComponent savedPet;
    if (srcPet) savedPet = *srcPet;

    Faction savedFaction = Faction::None;
    if (srcFaction) savedFaction = srcFaction->faction;

    PartyManager savedParty;
    if (srcParty) savedParty = srcParty->party;

    // Sprite visual data
    std::string savedTexPath;
    Vec2 savedSpriteSize{20.0f, 33.0f};
    Color savedTint = Color::white();
    bool savedFlipX = false;
    if (srcSprite) {
        savedTexPath = srcSprite->texturePath;
        savedSpriteSize = srcSprite->size;
        savedTint = srcSprite->tint;
        savedFlipX = srcSprite->flipX;
    }

    // PlayerController
    float savedMoveSpeed = 96.0f;
    Direction savedFacing = Direction::Down;
    bool savedIsLocal = false;
    if (srcCtrl) {
        savedMoveSpeed = srcCtrl->moveSpeed;
        savedFacing = srcCtrl->facing;
        savedIsLocal = srcCtrl->isLocalPlayer;
    }

    // Nameplate
    NameplateComponent savedNameplate;
    if (srcNameplate) savedNameplate = *srcNameplate;

    // Combat controller
    CombatControllerComponent savedCombat;
    if (srcCombat) savedCombat = *srcCombat;

    // Quest, Bank, Chat, Guild, Friends, Market, Trade
    QuestManager savedQuest;
    if (srcQuest) savedQuest = srcQuest->quests;

    BankStorage savedBank;
    if (srcBank) savedBank = srcBank->storage;

    ChatManager savedChat;
    if (srcChat) savedChat = srcChat->chat;

    GuildManager savedGuild;
    if (srcGuild) savedGuild = srcGuild->guild;

    FriendsManager savedFriends;
    if (srcFriends) savedFriends = srcFriends->friends;

    MarketManager savedMarket;
    if (srcMarket) savedMarket = srcMarket->market;

    TradeManager savedTrade;
    if (srcTrade) savedTrade = srcTrade->trade;

    // ---- 2. Unregister from source replication ----
    srcRepl.unregisterEntity(srcHandle);

    // ---- 3. Destroy source entity ----
    srcWorld.destroyEntity(srcHandle);
    srcWorld.processDestroyQueue();

    // ---- 4. Create new entity in destination World ----
    auto newHandle = dstWorld.createEntityH("player");
    auto* newEntity = dstWorld.getEntity(newHandle);
    if (!newEntity) {
        LOG_ERROR("Server", "transferPlayerToWorld: failed to create entity in dst world");
        return EntityHandle();
    }
    newEntity->setTag("player");

    // ---- 5. Add components and copy saved data ----

    // Transform — use spawnPos, not source position
    auto* newTransform = dstWorld.addComponentToEntity<Transform>(newEntity);
    newTransform->position = spawnPos;
    newTransform->depth = 10.0f;

    // SpriteComponent — copy visual data
    auto* newSprite = dstWorld.addComponentToEntity<SpriteComponent>(newEntity);
    newSprite->texturePath = savedTexPath;
    newSprite->texture = TextureCache::instance().load(savedTexPath);
    newSprite->size = savedSpriteSize;
    newSprite->tint = savedTint;
    newSprite->flipX = savedFlipX;

    // BoxCollider — recreate with standard player dimensions
    auto* newCollider = dstWorld.addComponentToEntity<BoxCollider>(newEntity);
    newCollider->size = {newSprite->size.x - 4.0f, newSprite->size.y * 0.5f};
    newCollider->offset = {0.0f, -newSprite->size.y * 0.25f};
    newCollider->isStatic = false;

    // PlayerController — copy movement config
    auto* newCtrl = dstWorld.addComponentToEntity<PlayerController>(newEntity);
    newCtrl->moveSpeed = savedMoveSpeed;
    newCtrl->facing = savedFacing;
    newCtrl->isLocalPlayer = savedIsLocal;
    newCtrl->isMoving = false;

    // CharacterStats — deep copy, update scene
    auto* newStats = dstWorld.addComponentToEntity<CharacterStatsComponent>(newEntity);
    newStats->stats = savedStats;
    newStats->stats.currentScene = newScene;
    playerDirty_[clientId].position = true;

    // CombatController — copy config, reset cooldown
    auto* newCombat = dstWorld.addComponentToEntity<CombatControllerComponent>(newEntity);
    newCombat->baseAttackCooldown = savedCombat.baseAttackCooldown;
    newCombat->attackCooldownRemaining = 0.0f;

    // Damageable marker
    dstWorld.addComponentToEntity<DamageableComponent>(newEntity);

    // Inventory — deep copy
    auto* newInv = dstWorld.addComponentToEntity<InventoryComponent>(newEntity);
    newInv->inventory = savedInv;

    // SkillManager — deep copy, relink stats pointer
    auto* newSkillComp = dstWorld.addComponentToEntity<SkillManagerComponent>(newEntity);
    newSkillComp->skills = savedSkills;
    newSkillComp->skills.initialize(&newStats->stats);

    // StatusEffects — fresh (clear buffs/debuffs on transfer)
    dstWorld.addComponentToEntity<StatusEffectComponent>(newEntity);

    // CrowdControl — fresh (clear CC on transfer)
    dstWorld.addComponentToEntity<CrowdControlComponent>(newEntity);

    // Targeting — fresh (clear target on transfer)
    dstWorld.addComponentToEntity<TargetingComponent>(newEntity);

    // Chat — copy
    auto* newChat = dstWorld.addComponentToEntity<ChatComponent>(newEntity);
    newChat->chat = savedChat;

    // Guild — copy
    auto* newGuild = dstWorld.addComponentToEntity<GuildComponent>(newEntity);
    newGuild->guild = savedGuild;

    // Party — copy
    auto* newPartyComp = dstWorld.addComponentToEntity<PartyComponent>(newEntity);
    newPartyComp->party = savedParty;

    // Friends — copy
    auto* newFriends = dstWorld.addComponentToEntity<FriendsComponent>(newEntity);
    newFriends->friends = savedFriends;

    // Market — copy
    auto* newMarket = dstWorld.addComponentToEntity<MarketComponent>(newEntity);
    newMarket->market = savedMarket;

    // Trade — copy
    auto* newTrade = dstWorld.addComponentToEntity<TradeComponent>(newEntity);
    newTrade->trade = savedTrade;

    // Quest — copy
    auto* newQuestComp = dstWorld.addComponentToEntity<QuestComponent>(newEntity);
    newQuestComp->quests = savedQuest;

    // Bank — copy
    auto* newBankComp = dstWorld.addComponentToEntity<BankStorageComponent>(newEntity);
    newBankComp->storage = savedBank;

    // Faction — copy
    auto* newFaction = dstWorld.addComponentToEntity<FactionComponent>(newEntity);
    newFaction->faction = savedFaction;

    // Pet — copy
    auto* newPetComp = dstWorld.addComponentToEntity<PetComponent>(newEntity);
    *newPetComp = savedPet;

    // Nameplate — copy
    auto* newNameplate = dstWorld.addComponentToEntity<NameplateComponent>(newEntity);
    *newNameplate = savedNameplate;

    // ---- 6. Register in destination replication ----
    auto newPid = PersistentId::generate(1);
    dstRepl.registerEntity(newHandle, newPid);

    // ---- 7. Update connection to point to new entity ----
    conn->playerEntityId = newPid.value();

    // Clear stale AOI state so replication rebuilds from scratch
    conn->aoi.current.clear();
    conn->aoi.previous.clear();
    conn->aoi.entered.clear();
    conn->aoi.left.clear();
    conn->aoi.stayed.clear();
    conn->lastSentState.clear();

    // Mark first-move sync so server accepts position unconditionally
    needsFirstMoveSync_.insert(clientId);

    LOG_INFO("Server", "transferPlayerToWorld: client %u transferred to scene '%s' at (%.0f, %.0f)",
             clientId, newScene.c_str(), spawnPos.x, spawnPos.y);

    return newHandle;
}

// ---------------------------------------------------------------------------
// processStartDungeon — leader requests dungeon entry, validate + invite
// ---------------------------------------------------------------------------
void ServerApp::processStartDungeon(uint16_t clientId, const CmdStartDungeonMsg& msg) {
    auto* conn = server_.connections().findById(clientId);
    if (!conn) return;
    PersistentId pid(conn->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* player = world_.getEntity(h);
    if (!player) return;

    // 1. Validate scene is a dungeon
    auto* sceneInfo = sceneCache_.get(msg.sceneId);
    if (!sceneInfo || !sceneInfo->isDungeon) {
        LOG_WARN("Server", "Client %d: invalid dungeon scene '%s'", clientId, msg.sceneId.c_str());
        return;
    }

    // 2. Validate party (2+ members, caller is leader)
    auto* partyComp = player->getComponent<PartyComponent>();
    if (!partyComp || !partyComp->party.isInParty() || !partyComp->party.isLeader) {
        LOG_WARN("Server", "Client %d: must be party leader to start dungeon", clientId);
        return;
    }
    if (static_cast<int>(partyComp->party.members.size()) < 2) {
        LOG_WARN("Server", "Client %d: party needs 2+ members for dungeon", clientId);
        return;
    }

    // 3. Validate no event locks for any member
    for (auto& member : partyComp->party.members) {
        // Look up entityId via characterId
        uint16_t memberCid = 0;
        server_.connections().forEach([&](ClientConnection& c) {
            if (c.character_id == member.characterId) memberCid = c.clientId;
        });
        if (memberCid == 0) continue;
        auto* memberConn = server_.connections().findById(memberCid);
        if (memberConn && playerEventLocks_.count(static_cast<uint32_t>(memberConn->playerEntityId))) {
            LOG_WARN("Server", "Client %d: party member '%s' in another event",
                     clientId, member.characterName.c_str());
            return;
        }
    }

    // 4. Validate level requirement for all members
    for (auto& member : partyComp->party.members) {
        if (member.level < sceneInfo->minLevel) {
            LOG_WARN("Server", "Client %d: party member '%s' below min level %d",
                     clientId, member.characterName.c_str(), sceneInfo->minLevel);
            return;
        }
    }

    // 5. Validate dungeon tickets for all members
    for (auto& member : partyComp->party.members) {
        if (!checkDungeonTicket(member.characterId)) {
            LOG_WARN("Server", "Client %d: party member '%s' has no dungeon ticket",
                     clientId, member.characterName.c_str());
            return;
        }
    }

    // 6. Create pending instance
    uint32_t instId = dungeonManager_.createInstance(msg.sceneId, partyComp->party.partyId, sceneInfo->difficultyTier);
    auto* inst = dungeonManager_.getInstance(instId);
    inst->leaderClientId = clientId;

    // 7. Send invite to non-leader members
    for (auto& member : partyComp->party.members) {
        if (member.isLeader) continue;
        uint16_t memberClientId = 0;
        server_.connections().forEach([&](ClientConnection& c) {
            if (c.character_id == member.characterId) memberClientId = c.clientId;
        });
        if (memberClientId == 0) continue;
        inst->pendingAccepts.insert(memberClientId);

        SvDungeonInviteMsg invite;
        invite.sceneId = msg.sceneId;
        invite.dungeonName = sceneInfo->sceneName;
        invite.timeLimitSeconds = static_cast<uint16_t>(inst->timeLimitSeconds);
        invite.levelReq = static_cast<uint8_t>(sceneInfo->minLevel);
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        invite.write(w);
        server_.sendTo(memberClientId, Channel::ReliableOrdered, PacketType::SvDungeonInvite, buf, w.size());
    }

    // If no pending (shouldn't happen with 2+ members, but safety)
    if (inst->allAccepted()) {
        startDungeonInstance(inst);
    }

    LOG_INFO("Server", "Client %d started dungeon '%s', waiting for %zu accepts",
             clientId, msg.sceneId.c_str(), inst->pendingAccepts.size());
}

// ---------------------------------------------------------------------------
// processDungeonResponse — member accepts or declines dungeon invite
// ---------------------------------------------------------------------------
void ServerApp::processDungeonResponse(uint16_t clientId, const CmdDungeonResponseMsg& msg) {
    // Find the instance this client was invited to
    DungeonInstance* inst = nullptr;
    uint32_t instId = 0;
    for (auto& [id, i] : dungeonManager_.allInstances()) {
        if (i->pendingAccepts.count(clientId)) {
            inst = i.get();
            instId = id;
            break;
        }
    }
    if (!inst) return;

    if (msg.accept) {
        inst->pendingAccepts.erase(clientId);
        LOG_INFO("Server", "Client %d accepted dungeon invite (instance %u)", clientId, instId);
        if (inst->allAccepted()) {
            startDungeonInstance(inst);
        }
    } else {
        LOG_INFO("Server", "Client %d declined dungeon invite, cancelling instance %u", clientId, instId);
        dungeonManager_.destroyInstance(instId);
    }
}

// ---------------------------------------------------------------------------
// startDungeonInstance — transfer all party members into the dungeon world
// ---------------------------------------------------------------------------
void ServerApp::startDungeonInstance(DungeonInstance* inst) {
    auto* sceneInfo = sceneCache_.get(inst->sceneId);
    if (!sceneInfo) return;

    Vec2 spawnPos = {sceneInfo->defaultSpawnX, sceneInfo->defaultSpawnY};

    // Collect all party member clientIds
    std::vector<uint16_t> allClients;
    allClients.push_back(inst->leaderClientId);

    auto* leaderConn = server_.connections().findById(inst->leaderClientId);
    if (!leaderConn) return;
    PersistentId leaderPid(leaderConn->playerEntityId);
    EntityHandle leaderH = replication_.getEntityHandle(leaderPid);
    Entity* leader = world_.getEntity(leaderH);
    if (!leader) return;
    auto* partyComp = leader->getComponent<PartyComponent>();

    if (partyComp) {
        for (auto& member : partyComp->party.members) {
            if (member.isLeader) continue;
            server_.connections().forEach([&](ClientConnection& c) {
                if (c.character_id == member.characterId) {
                    allClients.push_back(c.clientId);
                }
            });
        }
    }

    // Transfer each player
    for (uint16_t cid : allClients) {
        auto* conn = server_.connections().findById(cid);
        if (!conn) continue;
        PersistentId ppid(conn->playerEntityId);
        EntityHandle ph = replication_.getEntityHandle(ppid);
        Entity* player = world_.getEntity(ph);
        if (!player) continue;

        auto* cs = player->getComponent<CharacterStatsComponent>();
        auto* transform = player->getComponent<Transform>();
        if (!cs || !transform) continue;

        // Save return point
        inst->returnPoints[cid] = {cs->stats.currentScene, transform->position.x, transform->position.y};

        // Event lock
        playerEventLocks_[static_cast<uint32_t>(conn->playerEntityId)] = "Dungeon";

        // Consume ticket
        consumeDungeonTicket(conn->character_id);

        // Transfer to instance world
        transferPlayerToWorld(cid, world_, replication_, inst->world, inst->replication, spawnPos, inst->sceneId);

        // Track
        dungeonManager_.addPlayer(inst->instanceId, cid);

        // Send start message
        SvDungeonStartMsg start;
        start.sceneId = inst->sceneId;
        start.timeLimitSeconds = static_cast<uint16_t>(inst->timeLimitSeconds);
        uint8_t buf[128];
        ByteWriter w(buf, sizeof(buf));
        start.write(w);
        server_.sendTo(cid, Channel::ReliableOrdered, PacketType::SvDungeonStart, buf, w.size());
    }

    // Spawn dungeon mobs (no respawn)
    spawnDungeonMobs(inst);

    LOG_INFO("Server", "Dungeon instance %u started: scene=%s players=%zu",
             inst->instanceId, inst->sceneId.c_str(), allClients.size());
}

// ---------------------------------------------------------------------------
// spawnDungeonMobs — create mobs in dungeon instance (NO respawn)
// ---------------------------------------------------------------------------
void ServerApp::spawnDungeonMobs(DungeonInstance* inst) {
    static thread_local std::mt19937 s_rng{std::random_device{}()};

    const auto& zones = spawnZoneCache_.getZonesForScene(inst->sceneId);
    for (const auto& zone : zones) {
        const CachedMobDef* def = mobDefCache_.get(zone.mobDefId);
        if (!def) {
            LOG_WARN("Server", "Dungeon spawn: unknown mob_def_id '%s' in zone '%s'",
                     zone.mobDefId.c_str(), zone.zoneName.c_str());
            continue;
        }

        for (int i = 0; i < zone.targetCount; ++i) {
            // Random level within mob def range
            int level = def->minSpawnLevel;
            if (def->maxSpawnLevel > def->minSpawnLevel) {
                std::uniform_int_distribution<int> levelDist(def->minSpawnLevel, def->maxSpawnLevel);
                level = levelDist(s_rng);
            }

            // Random position within zone bounds (square, same as ServerSpawnManager)
            std::uniform_real_distribution<float> xDist(zone.centerX - zone.radius, zone.centerX + zone.radius);
            std::uniform_real_distribution<float> yDist(zone.centerY - zone.radius, zone.centerY + zone.radius);
            Vec2 pos = {xDist(s_rng), yDist(s_rng)};

            // Create entity in instance world
            Entity* mob = inst->world.createEntity(def->displayName);
            if (!mob) continue;
            mob->setTag("mob");

            // Transform
            auto* t = mob->addComponent<Transform>(pos);
            t->depth = 1.0f;

            // EnemyStatsComponent — fill all stats from CachedMobDef
            auto* esComp = mob->addComponent<EnemyStatsComponent>();
            EnemyStats& es = esComp->stats;
            es.enemyId          = def->mobDefId;
            es.enemyName        = def->displayName;
            es.sceneId          = inst->sceneId;
            es.level            = level;
            es.baseDamage       = def->getDamageForLevel(level);
            es.maxHP            = def->getHPForLevel(level);
            es.currentHP        = es.maxHP;
            es.armor            = def->getArmorForLevel(level);
            es.magicResist      = def->magicResist;
            es.critRate         = def->critRate;
            es.attackSpeed      = def->attackSpeed;
            es.moveSpeed        = def->moveSpeed;
            es.mobHitRate       = def->mobHitRate;
            es.xpReward         = def->getXPRewardForLevel(level);
            es.dealsMagicDamage = def->dealsMagicDamage;
            es.isAggressive     = def->isAggressive;
            es.isBoss           = def->isBoss;
            es.monsterType      = def->monsterType;
            es.lootTableId      = def->lootTableId;
            es.minGoldDrop      = def->minGoldDrop;
            es.maxGoldDrop      = def->maxGoldDrop;
            es.goldDropChance   = def->goldDropChance;
            es.honorReward      = def->honorReward;
            es.isAlive          = true;

            // MobAIComponent — initialize AI with home position and ranges from def
            auto* aiComp = mob->addComponent<MobAIComponent>();
            aiComp->ai.acquireRadius  = def->aggroRange * 32.0f;
            aiComp->ai.attackRange    = def->attackRange * 32.0f;
            aiComp->ai.contactRadius  = def->leashRadius * 32.0f;
            aiComp->ai.attackCooldown  = def->attackSpeed;
            aiComp->ai.isPassive       = !def->isAggressive;
            aiComp->ai.baseChaseSpeed  = def->moveSpeed * 32.0f;
            aiComp->ai.baseReturnSpeed = def->moveSpeed * 32.0f;
            aiComp->ai.baseRoamSpeed   = def->moveSpeed * 32.0f * 0.6f;
            aiComp->ai.roamRadius      = zone.radius * 0.4f;
            aiComp->ai.initialize(pos);

            // MobNameplateComponent — for replication buildEnterMessage
            auto* np = mob->addComponent<MobNameplateComponent>();
            np->displayName = def->displayName;
            np->level       = level;
            np->isBoss      = def->isBoss;
            np->visible     = true;

            // Register with instance replication
            PersistentId mobPid = PersistentId::generate(1);
            inst->replication.registerEntity(mob->handle(), mobPid);
        }
    }

    LOG_INFO("Server", "Spawned dungeon mobs for instance %u, scene '%s'",
             inst->instanceId, inst->sceneId.c_str());
}

// ---------------------------------------------------------------------------
// checkDungeonTicket — returns true if character can enter a dungeon today
// ---------------------------------------------------------------------------
bool ServerApp::checkDungeonTicket(const std::string& characterId) {
    try {
        pqxx::work txn(gameDbConn_.connection());
        auto result = txn.exec_params(
            "SELECT CASE WHEN last_dungeon_entry IS NULL THEN true "
            "ELSE last_dungeon_entry < date_trunc('day', NOW() AT TIME ZONE 'America/Chicago') "
            "END AS has_ticket FROM characters WHERE character_id = $1",
            characterId);
        txn.commit();
        if (result.empty()) return false;
        return result[0]["has_ticket"].as<bool>(false);
    } catch (const std::exception& e) {
        LOG_ERROR("Server", "Dungeon ticket check failed: %s", e.what());
        return false;
    }
}

// ---------------------------------------------------------------------------
// consumeDungeonTicket — mark character as having entered a dungeon today
// ---------------------------------------------------------------------------
void ServerApp::consumeDungeonTicket(const std::string& characterId) {
    try {
        pqxx::work txn(gameDbConn_.connection());
        txn.exec_params(
            "UPDATE characters SET last_dungeon_entry = NOW() WHERE character_id = $1",
            characterId);
        txn.commit();
    } catch (const std::exception& e) {
        LOG_ERROR("Server", "Failed to consume dungeon ticket: %s", e.what());
    }
}

} // namespace fate
