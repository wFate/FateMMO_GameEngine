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
#include "game/components/box_collider.h"
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
#include "game/shared/aurora_rotation.h"
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
#include <fstream>
#ifndef _WIN32
#include <dirent.h>
#endif

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
                        case WalEntryType::ItemRemove:
                            txn.exec_params(
                                "DELETE FROM character_inventory WHERE character_id = $1 AND slot_index = $2",
                                e.characterId, static_cast<int>(e.intValue));
                            break;
                        case WalEntryType::ItemAdd:
                            // ItemAdd WAL entries only store instanceId — not enough to reconstruct full item.
                            // The periodic inventory save will have the authoritative state; log for manual reconciliation.
                            LOG_WARN("WAL", "ItemAdd replay: char=%s instanceId=%s — verify inventory is consistent",
                                     e.characterId.c_str(), e.strValue.c_str());
                            break;
                        default: break;
                    }
                }
                txn.commit();
                // M2-FIX: Only truncate WAL after successful commit (was outside try block)
                wal_.truncate();
                LOG_INFO("WAL", "Replayed %zu entries", walEntries.size());
            } catch (const std::exception& ex) {
                LOG_ERROR("WAL", "Replay failed: %s — WAL preserved for retry on next startup", ex.what());
            }
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
    collectionRepo_ = std::make_unique<CollectionRepository>(dbPool_);
    costumeRepo_ = std::make_unique<CostumeRepository>(dbPool_);

    // Initialize definition caches
    itemDefCache_.initialize(gameDbConn_.connection());
    replication_.setItemDefCache(&itemDefCache_);
    replication_.setCostumeCache(&costumeCache_);

    // Invisibility filter: hide invisible entities from non-staff clients
    replication_.visibilityFilter = [this](uint64_t entityPid, const ClientConnection& observer) -> bool {
        if (invisibleEntities_.count(entityPid) == 0) return false; // not invisible
        // Staff can still see invisible entities
        AdminRole observerRole = clientAdminRoles_.count(observer.clientId)
            ? clientAdminRoles_[observer.clientId] : AdminRole::Player;
        return observerRole == AdminRole::Player; // hide from players only
    };

    dungeonManager_.setItemDefCache(&itemDefCache_);
    lootTableCache_.initialize(gameDbConn_.connection(), itemDefCache_);
    mobDefCache_.initialize(gameDbConn_.connection());
    skillDefCache_.initialize(gameDbConn_.connection());
    sceneCache_.initialize(gameDbConn_.connection());
    loadPortalsFromScenes();
    loadCollisionGridsFromScenes();

    // Wire collision grids into MobAISystem (system already added at startup)
    auto* mobAISys = world_.getSystem<MobAISystem>();
    if (mobAISys) {
        mobAISys->setCollisionGrids(&collisionGrids_);
    }

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

    // Load collection definitions from database
    if (collectionCache_.loadFromDatabase(gameDbConn_.connection())) {
        LOG_INFO("Server", "Loaded %zu collection definitions", collectionCache_.size());
    } else {
        LOG_WARN("Server", "Failed to load collection definitions (table may not exist yet)");
    }

    // Load costume definitions from database
    if (!costumeCache_.loadFromDatabase(gameDbConn_.connection())) {
        LOG_ERROR("Server", "Failed to load costume definitions");
    } else {
        LOG_INFO("Server", "Loaded %zu costume definitions", costumeCache_.size());
    }
    if (!costumeCache_.loadMobDrops(gameDbConn_.connection())) {
        LOG_WARN("Server", "Failed to load mob costume drops (table may not exist yet)");
    }

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
        battlefieldManager_.setActive(true);
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

            // Collection hook: battlefield win
            if (isWinner) {
                checkPlayerCollections(conn.clientId, "WinBattlefield");
            }

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
            auto* client = server_.connections().findByEntityLow32(eid);
            if (client && client->playerEntityId != 0) {
                uint16_t targetClientId = client->clientId;
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
                client->aoi.previous.clear();
                client->aoi.current.clear();
                client->aoi.entered.clear();
                client->aoi.left.clear();
                client->aoi.stayed.clear();
                client->lastSentState.clear();

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

                // Collection hook: arena win
                if (won) {
                    checkPlayerCollections(targetClientId, "WinArena");
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

    // Initialize Aurora rotation state
    lastFavoredFaction_ = AuroraRotation::getFavoredFaction(std::time(nullptr));
    LOG_INFO("Server", "Aurora rotation initialized: favored faction = %d", static_cast<int>(lastFavoredFaction_));

    // Initialize GM commands
    initGMCommands();

    // Auth server startup (warning only — game server can run without auth in dev)
    if (!authServer_.start(authPort_, tlsCertPath_, tlsKeyPath_, dbConnectionString_)) {
        LOG_WARN("Server", "Auth server failed to start on port %d; continuing without auth", authPort_);
    }

    LOG_INFO("Server", "Started on port %d at %.0f ticks/sec", port, TICK_RATE);
    return true;
}

void ServerApp::loadPortalsFromScenes() {
    portals_.clear();
    const std::string scenesDir = "assets/scenes/";
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((scenesDir + "*.json").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        LOG_WARN("Server", "No scene files found in %s — portal validation disabled", scenesDir.c_str());
        return;
    }
    std::vector<std::string> sceneFiles;
    do {
        sceneFiles.push_back(scenesDir + fd.cFileName);
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    // POSIX directory scan
    DIR* dir = opendir(scenesDir.c_str());
    if (!dir) {
        LOG_WARN("Server", "No scene files found in %s — portal validation disabled", scenesDir.c_str());
        return;
    }
    std::vector<std::string> sceneFiles;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() > 5 && name.substr(name.size() - 5) == ".json")
            sceneFiles.push_back(scenesDir + name);
    }
    closedir(dir);
#endif

    for (const auto& filePath : sceneFiles) {
        // Derive scene ID from filename (e.g., "assets/scenes/Town.json" -> "Town")
        std::string filename = filePath.substr(filePath.find_last_of("/\\") + 1);
        std::string sceneId = filename.substr(0, filename.size() - 5); // strip ".json"

        std::ifstream f(filePath);
        if (!f.is_open()) continue;

        nlohmann::json doc;
        try { doc = nlohmann::json::parse(f); }
        catch (...) { continue; }

        // Scene files have "entities" array; each entity has "components" object
        auto entitiesIt = doc.find("entities");
        if (entitiesIt == doc.end() || !entitiesIt->is_array()) continue;

        for (const auto& entity : *entitiesIt) {
            auto compsIt = entity.find("components");
            if (compsIt == entity.end()) continue;

            auto portalIt = compsIt->find("PortalComponent");
            if (portalIt == compsIt->end()) continue;

            auto transformIt = compsIt->find("Transform");
            if (transformIt == compsIt->end()) continue;

            ServerPortal p;
            p.sourceScene = sceneId;
            p.targetScene = portalIt->value("targetScene", "");
            if (p.targetScene.empty()) continue;

            auto posIt = transformIt->find("position");
            if (posIt != transformIt->end() && posIt->is_array() && posIt->size() >= 2) {
                p.x = (*posIt)[0].get<float>();
                p.y = (*posIt)[1].get<float>();
            }

            auto trigIt = portalIt->find("triggerSize");
            if (trigIt != portalIt->end() && trigIt->is_array() && trigIt->size() >= 2) {
                p.halfW = (*trigIt)[0].get<float>() / 2.0f;
                p.halfH = (*trigIt)[1].get<float>() / 2.0f;
            } else {
                p.halfW = 16.0f;
                p.halfH = 16.0f;
            }

            auto spawnIt = portalIt->find("targetSpawnPos");
            if (spawnIt != portalIt->end() && spawnIt->is_array() && spawnIt->size() >= 2) {
                p.targetSpawnX = (*spawnIt)[0].get<float>();
                p.targetSpawnY = (*spawnIt)[1].get<float>();
            }

            portals_.push_back(std::move(p));
        }
    }

    LOG_INFO("Server", "Loaded %d portal definitions from scene files", static_cast<int>(portals_.size()));
}

void ServerApp::loadCollisionGridsFromScenes() {
    collisionGrids_.clear();
    const std::string scenesDir = "assets/scenes/";
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((scenesDir + "*.json").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        LOG_WARN("Server", "No scene files found in %s — collision grids disabled", scenesDir.c_str());
        return;
    }
    std::vector<std::string> sceneFiles;
    do {
        sceneFiles.push_back(scenesDir + fd.cFileName);
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    // POSIX directory scan
    DIR* dir = opendir(scenesDir.c_str());
    if (!dir) {
        LOG_WARN("Server", "No scene files found in %s — collision grids disabled", scenesDir.c_str());
        return;
    }
    std::vector<std::string> sceneFiles;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() > 5 && name.substr(name.size() - 5) == ".json")
            sceneFiles.push_back(scenesDir + name);
    }
    closedir(dir);
#endif

    for (const auto& filePath : sceneFiles) {
        // Derive scene name from filename (e.g., "assets/scenes/Town.json" -> "Town")
        std::string filename = filePath.substr(filePath.find_last_of("/\\") + 1);
        std::string sceneName = filename.substr(0, filename.size() - 5); // strip ".json"

        std::ifstream f(filePath);
        if (!f.is_open()) continue;

        nlohmann::json doc;
        try { doc = nlohmann::json::parse(f); }
        catch (...) { continue; }

        auto entitiesIt = doc.find("entities");
        if (entitiesIt == doc.end() || !entitiesIt->is_array()) continue;

        CollisionGrid grid;
        grid.beginBuild();

        for (const auto& entity : *entitiesIt) {
            // Only process entities tagged as "ground"
            if (entity.value("tag", "") != "ground") continue;

            auto compsIt = entity.find("components");
            if (compsIt == entity.end()) continue;

            // Must have TileLayerComponent with layer == "collision"
            auto tileLayerIt = compsIt->find("TileLayerComponent");
            if (tileLayerIt == compsIt->end()) continue;
            if (tileLayerIt->value("layer", "") != "collision") continue;

            // Must have Transform with position
            auto transformIt = compsIt->find("Transform");
            if (transformIt == compsIt->end()) continue;

            auto posIt = transformIt->find("position");
            if (posIt == transformIt->end() || !posIt->is_array() || posIt->size() < 2) continue;

            float px = (*posIt)[0].get<float>();
            float py = (*posIt)[1].get<float>();
            grid.markBlocked(static_cast<int>(std::floor(px / 32.0f)),
                             static_cast<int>(std::floor(py / 32.0f)));
        }

        grid.endBuild();
        if (!grid.empty()) {
            collisionGrids_[sceneName] = std::move(grid);
        }
    }

    LOG_INFO("Server", "Loaded collision grids for %zu scenes", collisionGrids_.size());
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
                    if (auto* tc = server_.connections().findByEntity(playerPid)) {
                        sendPlayerState(tc->clientId);
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
                auto* tc = server_.connections().findByEntity(playerPid);
                if (tc) {
                    uint16_t targetClientId = tc->clientId;
                    SvDeathNotifyMsg deathMsg;
                    deathMsg.deathSource = static_cast<uint8_t>(DeathSource::PvE);
                    // Override for Aurora zones — client shows only "Return to Town"
                    if (sc && isAuroraScene(sc->stats.currentScene)) {
                        deathMsg.deathSource = static_cast<uint8_t>(DeathSource::Aurora);
                    }
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

        mobAISys->shouldBlockDamage = [this](Entity* mob, Entity* player) -> bool {
            if (!player) return false;
            uint64_t playerPid = replication_.getPersistentId(player->handle()).value();
            return godModeEntities_.count(playerPid) > 0;
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
            int ticksNeeded = static_cast<int>(elapsed / TICK_INTERVAL);
            if (ticksNeeded > MAX_CATCHUP_TICKS) {
                LOG_WARN("Server", "Tick overrun: %d ticks behind (%.0fms), shedding %d",
                         ticksNeeded, elapsed * 1000.0f, ticksNeeded - MAX_CATCHUP_TICKS);
                ticksNeeded = MAX_CATCHUP_TICKS;
            }

            for (int i = 0; i < ticksNeeded && running_; ++i) {
                gameTime_ += TICK_INTERVAL;

                tick(TICK_INTERVAL);
            }

            // Reset to current time to prevent debt accumulation
            lastTick = std::chrono::high_resolution_clock::now();
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
    using Clock = std::chrono::high_resolution_clock;
    auto tp0 = Clock::now(); // tick start

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
    auto tp1 = Clock::now(); // after network poll

    // 3. World update (systems)
    world_.update(dt);
    spawnManager_.tick(dt, gameTime_, world_, replication_);

    auto tp2 = Clock::now(); // after world update + spawn

    // 3b. Boss/mini-boss combat leash — reset HP if no damage for 15s
    world_.forEach<EnemyStatsComponent>([&](Entity*, EnemyStatsComponent* esComp) {
        auto& es = esComp->stats;
        if (es.monsterType == "Normal") return;  // only bosses/mini-bosses
        if (es.tickLeash(gameTime_)) {
            LOG_DEBUG("Server", "Leash reset: %s '%s' healed to full (no damage for %.0fs)",
                      es.monsterType.c_str(), es.enemyName.c_str(), EnemyStats::LEASH_TIMEOUT);
        }
    });
    for (auto& [id, inst] : dungeonManager_.allInstances()) {
        if (!inst->expired) {
            inst->world.forEach<EnemyStatsComponent>([&](Entity*, EnemyStatsComponent* esComp) {
                auto& es = esComp->stats;
                if (es.monsterType == "Normal") return;
                es.tickLeash(gameTime_);
            });
        }
    }

    // 3c. Tick status effects (DoTs, buffs/debuffs) and crowd control for all entities
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

    // 3d. Tick player timers (PK decay, combat timer, respawn invuln)
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

    // 3e. HP/MP regen tick (server-authoritative)
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

    // Tick active casts for all players
    world_.forEach<CharacterStatsComponent>([&](Entity* e, CharacterStatsComponent* cs) {
        if (!cs->stats.isCasting()) return;
        bool completed = cs->stats.tickCast(dt);
        if (completed) {
            // Fields (skillId, skillRank, targetEntityId) are still valid after tickCast clears active
            std::string castSkillId = cs->stats.castingState.skillId;
            uint8_t castRank = static_cast<uint8_t>(cs->stats.castingState.skillRank);
            uint32_t castTargetId = cs->stats.castingState.targetEntityId;

            // Find owning client
            uint16_t ownerClientId = 0;
            server_.connections().forEach([&](const ClientConnection& c) {
                if (c.playerEntityId != 0) {
                    PersistentId p(c.playerEntityId);
                    EntityHandle eh = replication_.getEntityHandle(p);
                    if (world_.getEntity(eh) == e) ownerClientId = c.clientId;
                }
            });
            if (ownerClientId == 0) return;

            // Re-validate target is still alive before executing
            if (cs->stats.castingState.targetEntityId != 0) {
                PersistentId targetPid(cs->stats.castingState.targetEntityId);
                EntityHandle targetH = replication_.getEntityHandle(targetPid);
                Entity* targetE = world_.getEntity(targetH);
                if (!targetE) {
                    LOG_INFO("Server", "Cast fizzled: target entity gone during cast");
                    return; // inside forEach lambda
                }
                auto* targetCs = targetE->getComponent<CharacterStatsComponent>();
                auto* targetEs = targetE->getComponent<EnemyStatsComponent>();
                bool targetAlive = (targetCs && targetCs->stats.isAlive()) ||
                                   (targetEs && targetEs->stats.isAlive);
                if (!targetAlive) {
                    LOG_INFO("Server", "Cast fizzled: target died during cast");
                    return; // inside forEach lambda
                }
            }

            // Erase cooldown so processUseSkill doesn't reject (cooldown was set at cast start)
            skillCooldowns_[ownerClientId].erase(castSkillId);

            // Re-execute the skill now that cast completed
            CmdUseSkillMsg fakeMsg;
            fakeMsg.skillId = castSkillId;
            fakeMsg.rank = castRank;
            fakeMsg.targetId = static_cast<uint64_t>(castTargetId);
            castCompleting_ = true;
            processUseSkill(ownerClientId, fakeMsg);
            castCompleting_ = false;
        }
    });

    auto tp3 = Clock::now(); // after ECS systems (status/CC/regen/casts)

    // Pet auto-loot tick
    tickPetAutoLoot(dt);

    // 3f. Process Dying → Dead transitions (two-tick death lifecycle)
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

    // Flush destroyed entities to free memory (items, mobs, etc. queued by destroyEntity above)
    world_.processDestroyQueue();

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

    auto tp4 = Clock::now(); // after despawn + boss

    // 4. Replicate entity state to connected clients
    replication_.update(world_, server_);

    auto tp5 = Clock::now(); // after replication

    // 5. Retransmit unacked reliable packets
    server_.processRetransmits(gameTime_);

    // 5b. Drain async DB completions
    dbDispatcher_.drainCompletions();

    // 5c. Gauntlet event cycle
    gauntletManager_.tick(gameTime_);

    // 5d. Battlefield event scheduler
    eventScheduler_.tick(gameTime_);

    // 5d2. Aurora rotation (faction buff cycling)
    tickAuroraRotation(dt);

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
                auto* client = server_.connections().findByEntityLow32(eid);
                if (!client || client->playerEntityId == 0) continue;
                uint16_t targetClientId = client->clientId;

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

    auto tp6 = Clock::now(); // after retransmit + DB drain + events

    // 5g. Auto-save and periodic maintenance
    tickAutoSave(dt);
    tickPersistQueue();
    tickMaintenance(dt);
    tickDungeonInstances(dt);
    battlefieldManager_.tickGracePeriod(gameTime_);
    wal_.flush();

    // If WAL writes failed, crash recovery is compromised — force sync DB save
    if (wal_.hasWriteError()) {
        LOG_ERROR("Server", "WAL write error detected — forcing synchronous DB flush for all players");
        server_.connections().forEach([this](ClientConnection& c) {
            if (c.playerEntityId != 0) savePlayerToDB(c.clientId, true);
        });
        wal_.clearWriteError();
        wal_.truncate();
    }

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

    auto tp7 = Clock::now(); // tick end
    auto ms = [](auto a, auto b) { return std::chrono::duration<float, std::milli>(b - a).count(); };
    float totalMs = ms(tp0, tp7);
    if (totalMs > TICK_INTERVAL * 1000.0f) {
        LOG_WARN("Server", "Slow tick: %.1fms | net=%.1f world=%.1f ecs=%.1f "
                 "despawn=%.1f repl=%.1f events=%.1f save=%.1f",
                 totalMs, ms(tp0, tp1), ms(tp1, tp2), ms(tp2, tp3),
                 ms(tp3, tp4), ms(tp4, tp5), ms(tp5, tp6), ms(tp6, tp7));
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

        // Reject if too many pending sessions (prevent unbounded growth / DoS)
        static constexpr size_t MAX_PENDING_SESSIONS = 1000;
        if (pendingSessions_.size() >= MAX_PENDING_SESSIONS) {
            LOG_WARN("Server", "Pending sessions cap reached (%zu); rejecting auth for account %d",
                     MAX_PENDING_SESSIONS, result.session.account_id);
            continue;
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
    if (src.resourceType == "Fury")      def.resourceType = ResourceType::Fury;
    else if (src.resourceType == "Mana") def.resourceType = ResourceType::Mana;
    else                                 def.resourceType = ResourceType::None;

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
    Entity* player = EntityFactory::createPlayer(world_, rec.character_name, classType, false, Faction::None,
        static_cast<uint8_t>(rec.gender), static_cast<uint8_t>(rec.hairstyle));

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

        // DISABLED: stat allocation removed — stats are fixed per class
        // s.freeStatPoints = static_cast<int16_t>(rec.free_stat_points);
        // s.allocatedSTR   = static_cast<int16_t>(rec.allocated_str);
        // s.allocatedINT   = static_cast<int16_t>(rec.allocated_int);
        // s.allocatedDEX   = static_cast<int16_t>(rec.allocated_dex);
        // s.allocatedCON   = static_cast<int16_t>(rec.allocated_con);
        // s.allocatedWIS   = static_cast<int16_t>(rec.allocated_wis);

        s.recallScene = rec.recall_scene.empty() ? "Town" : rec.recall_scene;

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
    server_.connections().mapEntity(pid.value(), clientId);

    // Track active account session
    activeAccountSessions_[session.account_id] = clientId;

    // Load admin_role from DB (carried through PendingSession from auth flow)
    clientAdminRoles_[clientId] = session.admin_role;

    // Load skills from DB
    // Re-link SkillManager's stats pointer — EntityFactory stores it during
    // addComponent, but archetype migrations invalidate component pointers.
    auto* skillComp = player->getComponent<SkillManagerComponent>();
    if (skillComp && charStatsComp) {
        skillComp->skills.initialize(&charStatsComp->stats);
    }
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

        // Recompute passive bonuses now that definitions are available.
        // activateSkillRank() above couldn't look up definitions (registered after),
        // so passive accumulators were left at zero.
        skillComp->skills.recomputePassiveBonuses();
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
        bankComp->storage.setSerializedState(bankGold, bankComp->storage.getMaxSlots(), {}); // no fee on load
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

    // Load completed collections
    {
        auto completedColIds = collectionRepo_->loadCompletedCollections(rec.character_id);
        auto* collComp = player->addComponent<CollectionComponent>();
        for (uint32_t cid : completedColIds) {
            collComp->collections.markCompleted(cid);
        }
        collComp->collections.recalculateBonuses(collectionCache_.all());
        // Apply bonuses to stats
        if (charStatsComp) {
            charStatsComp->stats.collectionBonusSTR = collComp->collections.bonuses.bonusSTR;
            charStatsComp->stats.collectionBonusINT = collComp->collections.bonuses.bonusINT;
            charStatsComp->stats.collectionBonusDEX = collComp->collections.bonuses.bonusDEX;
            charStatsComp->stats.collectionBonusCON = collComp->collections.bonuses.bonusCON;
            charStatsComp->stats.collectionBonusWIS = collComp->collections.bonuses.bonusWIS;
            charStatsComp->stats.collectionBonusHP  = collComp->collections.bonuses.bonusMaxHP;
            charStatsComp->stats.collectionBonusMP  = collComp->collections.bonuses.bonusMaxMP;
            charStatsComp->stats.collectionBonusDamage = collComp->collections.bonuses.bonusDamage;
            charStatsComp->stats.collectionBonusArmor  = collComp->collections.bonuses.bonusArmor;
            charStatsComp->stats.collectionBonusCritRate = collComp->collections.bonuses.bonusCritRate;
            charStatsComp->stats.collectionBonusMoveSpeed = collComp->collections.bonuses.bonusMoveSpeed;
            charStatsComp->stats.recalculateStats();
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
                deathMsg.deathSource = static_cast<uint8_t>(DeathSource::PvE);
                // Override for Aurora zones
                if (cs && isAuroraScene(cs->stats.currentScene)) {
                    deathMsg.deathSource = static_cast<uint8_t>(DeathSource::Aurora);
                }
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

            // Grant a skill point on level-up
            auto* sk = ent->getComponent<SkillManagerComponent>();
            if (sk) {
                sk->skills.grantSkillPoint();
                playerDirty_[clientId].skills = true;
                enqueuePersist(clientId, PersistPriority::HIGH, PersistType::Skills);
                sendSkillSync(clientId);
            }

            // DISABLED: stat allocation removed — no free stat points to persist
            // playerDirty_[clientId].stats = true;
            // enqueuePersist(clientId, PersistPriority::HIGH, PersistType::Character);

            // Also send full player state
            sendPlayerState(clientId);

            // Collection hook: level reached
            checkPlayerCollections(clientId, "ReachLevel");
        };
    }

    // Send initial player state and full sync
    sendPlayerState(clientId);
    sendSkillSync(clientId);
    sendSkillDefs(clientId, rec.class_name);
    sendQuestSync(clientId);
    sendInventorySync(clientId);
    sendCollectionSync(clientId);
    sendCollectionDefs(clientId);
    loadPlayerCostumes(clientId, rec.character_id);

    // If player reconnects while dead, notify client so death overlay shows
    if (rec.is_dead) {
        SvDeathNotifyMsg deathMsg;
        deathMsg.deathSource = static_cast<uint8_t>(DeathSource::PvE);
        // Override for Aurora zones
        if (charStatsComp && isAuroraScene(charStatsComp->stats.currentScene)) {
            deathMsg.deathSource = static_cast<uint8_t>(DeathSource::Aurora);
        }
        deathMsg.respawnTimer = 0.0f;  // timer already expired, can respawn immediately
        deathMsg.xpLost = 0;           // penalty was already applied on original death
        deathMsg.honorLost = 0;
        uint8_t buf[32]; ByteWriter w(buf, sizeof(buf));
        deathMsg.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvDeathNotify, buf, w.size());
        LOG_INFO("Server", "Client %d reconnected dead — sent death notification", clientId);
    }

    // Reconnect to active battlefield if within grace period
    if (battlefieldManager_.hasGraceEntry(session.character_id)) {
        auto bfData = battlefieldManager_.consumeGraceEntry(session.character_id);
        uint32_t newEid = static_cast<uint32_t>(pid.value());
        battlefieldManager_.restorePlayer(newEid, std::move(bfData));
        playerEventLocks_[newEid] = "battlefield";
        if (charStatsComp) {
            charStatsComp->stats.currentScene = "Battlefield";
        }
        LOG_INFO("Server", "Client %d ('%s') rejoined battlefield (grace period)",
                 clientId, session.character_id.c_str());
    }

    // Reconnect to active dungeon if within grace period
    if (dungeonManager_.hasGraceEntry(session.character_id)) {
        auto dgData = dungeonManager_.consumeGraceEntry(session.character_id);
        auto* inst = dungeonManager_.getInstance(dgData.instanceId);
        if (inst && !inst->expired && !inst->completed) {
            dungeonManager_.addPlayer(dgData.instanceId, clientId);
            inst->returnPoints[clientId] = dgData.returnPoint;
            if (charStatsComp) {
                charStatsComp->stats.currentScene = inst->sceneId;
            }
            LOG_INFO("Server", "Client %d ('%s') rejoined dungeon instance %u (grace period)",
                     clientId, session.character_id.c_str(), dgData.instanceId);
        }
    }

    // If player logged in while inside an Aurora zone, apply buffs + send status
    if (charStatsComp && isAuroraScene(charStatsComp->stats.currentScene)) {
        applyAuroraBuffs(clientId, player);
        sendAuroraStatus(clientId);
    }

    // Establish AEAD encryption via key exchange
    if (PacketCrypto::isAvailable()) {
        if (client->hasClientPublicKey) {
            // Secure DH exchange: derive shared keys, send only our public key
            auto serverKp = PacketCrypto::generateKeypair();
            auto keys = PacketCrypto::deriveServerSessionKeys(
                serverKp.pk, serverKp.sk, client->clientPublicKey);
            client->crypto.setKeys(keys.txKey, keys.rxKey);

            uint8_t keyBuf[32];
            ByteWriter kw(keyBuf, sizeof(keyBuf));
            kw.writeBytes(serverKp.pk.data(), 32);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::KeyExchange, keyBuf, kw.size());

            // Wipe ephemeral secret key and client public key from connection state
            PacketCrypto::secureWipe(serverKp.sk.data(), serverKp.sk.size());
            PacketCrypto::secureWipe(client->clientPublicKey.data(), client->clientPublicKey.size());
            client->hasClientPublicKey = false;
            LOG_INFO("Server", "DH key exchange with client %d — encryption active", clientId);
        } else {
            // No DH public key — reject (plaintext key fallback removed)
            LOG_ERROR("Server", "Client %d rejected: no DH public key (encryption required)", clientId);
            server_.sendConnectReject(client->address, "Encryption required (DH key exchange)");
            server_.connections().removeClient(clientId);
            return;
        }
    }

    LOG_INFO("Server", "Client %d connected: account=%d char='%s' level=%d guild=%d",
             clientId, session.account_id, rec.character_name.c_str(), rec.level,
             guildComp ? guildComp->guild.guildId : 0);
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

        // Battlefield: move to grace period instead of removing (3 min to rejoin)
        if (battlefieldManager_.isPlayerRegistered(eid)) {
            battlefieldManager_.markDisconnected(client->character_id, eid, gameTime_);
            LOG_INFO("Server", "Client %d ('%s') moved to battlefield grace period",
                     clientId, client->character_id.c_str());
        }

        // Dungeon: move to grace period instead of removing
        if (dungeonManager_.getInstanceForClient(clientId) != 0) {
            dungeonManager_.markDisconnected(client->character_id, clientId, gameTime_);
            LOG_INFO("Server", "Client %d ('%s') moved to dungeon grace period",
                     clientId, client->character_id.c_str());
        }

        arenaManager_.onPlayerDisconnect(eid);
        playerEventLocks_.erase(eid);

        // Purge this player's damage from all mob threat tables in their scene
        {
            World& w = getWorldForClient(clientId);
            w.forEach<EnemyStatsComponent>([eid](Entity*, EnemyStatsComponent* esc) {
                esc->stats.damageByAttacker.erase(eid);
            });
        }

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

        // Dungeon instance tracking already handled by markDisconnected above
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
    // Clear invisibility/god mode on disconnect
    if (client) {
        invisibleEntities_.erase(client->playerEntityId);
        godModeEntities_.erase(client->playerEntityId);
    }
    clientMutes_.erase(clientId);
    clientAdminRoles_.erase(clientId);
}

bool ServerApp::validatePayload(ByteReader& payload, uint16_t clientId, uint8_t type) {
    if (payload.ok()) return true;
    LOG_WARN("Server", "Malformed packet 0x%02X from client %d (payload overflow)", type, clientId);
    rateLimiters_[clientId].addViolation(static_cast<double>(gameTime_), 5);
    return false;
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
            if (!validatePayload(payload, clientId, type)) return;
            processMove(clientId, move);
            break;
        }
        case PacketType::CmdAction: {
            auto action = CmdAction::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processAction(clientId, action);
            break;
        }
        case PacketType::CmdChat: {
            auto chat = CmdChat::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processChat(clientId, chat);
            break;
        }
        case PacketType::CmdMarket: {
            processMarket(clientId, payload);
            break;
        }
        case PacketType::CmdBounty: {
            processBounty(clientId, payload);
            break;
        }
        case PacketType::CmdGuild: {
            processGuild(clientId, payload);
            break;
        }
        case PacketType::CmdSocial: {
            processSocial(clientId, payload);
            break;
        }
        case PacketType::CmdTrade: {
            processTrade(clientId, payload);
            break;
        }
        case PacketType::CmdGauntlet: {
            processGauntletCommand(clientId, payload);
            break;
        }
        case PacketType::CmdQuestAction: {
            processQuestAction(clientId, payload);
            break;
        }
        case PacketType::CmdZoneTransition: {
            auto cmd = CmdZoneTransition::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processZoneTransition(clientId, cmd);
            break;
        }
        case PacketType::CmdRespawn: {
            auto msg = CmdRespawnMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processRespawn(clientId, msg);
            break;
        }
        case PacketType::CmdUseSkill: {
            auto msg = CmdUseSkillMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processUseSkill(clientId, msg);
            break;
        }
        case PacketType::CmdEquip: {
            auto msg = CmdEquipMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processEquip(clientId, msg);
            break;
        }
        case PacketType::CmdMoveItem: {
            auto msg = CmdMoveItemMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processMoveItem(clientId, msg);
            break;
        }
        case PacketType::CmdDestroyItem: {
            auto msg = CmdDestroyItemMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processDestroyItem(clientId, msg);
            break;
        }
        case PacketType::CmdEnchant: {
            auto msg = CmdEnchantMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processEnchant(clientId, msg);
            break;
        }
        case PacketType::CmdRepair: {
            auto msg = CmdRepairMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processRepair(clientId, msg);
            break;
        }
        case PacketType::CmdExtractCore: {
            auto msg = CmdExtractCoreMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processExtractCore(clientId, msg);
            break;
        }
        case PacketType::CmdCraft: {
            auto msg = CmdCraftMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processCraft(clientId, msg);
            break;
        }
        case PacketType::CmdBattlefield: {
            auto msg = CmdBattlefieldMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processBattlefield(clientId, msg);
            break;
        }
        case PacketType::CmdArena: {
            auto msg = CmdArenaMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processArena(clientId, msg);
            break;
        }
        case PacketType::CmdPet: {
            auto msg = CmdPetMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processPetCommand(clientId, msg);
            break;
        }
        case PacketType::CmdShopBuy: {
            auto msg = CmdShopBuyMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processShopBuy(clientId, msg);
            break;
        }
        case PacketType::CmdShopSell: {
            auto msg = CmdShopSellMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processShopSell(clientId, msg);
            break;
        }
        case PacketType::CmdTeleport: {
            auto msg = CmdTeleportMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processTeleport(clientId, msg);
            break;
        }
        case PacketType::CmdBankDepositItem: {
            auto msg = CmdBankDepositItemMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processBankDepositItem(clientId, msg);
            break;
        }
        case PacketType::CmdBankWithdrawItem: {
            auto msg = CmdBankWithdrawItemMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processBankWithdrawItem(clientId, msg);
            break;
        }
        case PacketType::CmdBankDepositGold: {
            auto msg = CmdBankDepositGoldMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processBankDepositGold(clientId, msg);
            break;
        }
        case PacketType::CmdBankWithdrawGold: {
            auto msg = CmdBankWithdrawGoldMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processBankWithdrawGold(clientId, msg);
            break;
        }
        case PacketType::CmdSocketItem: {
            auto msg = CmdSocketItemMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processSocketItem(clientId, msg);
            break;
        }
        case PacketType::CmdStatEnchant: {
            auto msg = CmdStatEnchantMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processStatEnchant(clientId, msg);
            break;
        }
        case PacketType::CmdUseConsumable: {
            auto msg = CmdUseConsumableMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processUseConsumable(clientId, msg);
            break;
        }
        case PacketType::CmdRankingQuery: {
            auto msg = CmdRankingQueryMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processRankingQuery(clientId, msg);
            break;
        }
        case PacketType::CmdStartDungeon: {
            auto msg = CmdStartDungeonMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processStartDungeon(clientId, msg);
            break;
        }
        case PacketType::CmdDungeonResponse: {
            auto msg = CmdDungeonResponseMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processDungeonResponse(clientId, msg);
            break;
        }
        case PacketType::CmdActivateSkillRank: {
            auto msg = CmdActivateSkillRankMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processActivateSkillRank(clientId, msg);
            break;
        }
        case PacketType::CmdAssignSkillSlot: {
            auto msg = CmdAssignSkillSlotMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processAssignSkillSlot(clientId, msg);
            break;
        }
        case PacketType::CmdAllocateStat: {
            auto msg = CmdAllocateStatMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processAllocateStat(clientId, msg);
            break;
        }
        case PacketType::CmdEquipCostume: {
            auto msg = CmdEquipCostumeMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processEquipCostume(clientId, msg);
            break;
        }
        case PacketType::CmdUnequipCostume: {
            auto msg = CmdUnequipCostumeMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processUnequipCostume(clientId, msg);
            break;
        }
        case PacketType::CmdToggleCostumes: {
            auto msg = CmdToggleCostumesMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            processToggleCostumes(clientId, msg);
            break;
        }
        case PacketType::CmdEditorPause: {
            auto msg = CmdEditorPauseMsg::read(payload);
            if (!validatePayload(payload, clientId, type)) return;
            auto* client = server_.connections().findById(clientId);
            if (!client || client->playerEntityId == 0) break;
            PersistentId pid(client->playerEntityId);
            EntityHandle h = replication_.getEntityHandle(pid);
            Entity* player = world_.getEntity(h);
            if (player) {
                auto* charStats = player->getComponent<CharacterStatsComponent>();
                if (charStats) {
                    bool paused = msg.paused != 0;
                    charStats->stats.editorPaused = paused;
                    if (paused) {
                        godModeEntities_.insert(client->playerEntityId);
                    } else {
                        godModeEntities_.erase(client->playerEntityId);
                    }
                    LOG_INFO("Server", "Client %d editor %s", clientId, paused ? "paused" : "resumed");
                }
            }
            break;
        }
        default:
            LOG_WARN("Server", "Unknown packet type 0x%02X from client %d", type, clientId);
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

} // namespace fate
