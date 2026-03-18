#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/net/protocol.h"
#include "engine/ecs/persistent_id.h"
#include "game/entity_factory.h"
#include "game/shared/game_types.h"
#include "game/components/game_components.h"
#include "game/components/faction_component.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

#include <chrono>
#include <thread>
#include <cstdlib>

namespace fate {

bool ServerApp::init(uint16_t port) {
    if (!NetSocket::initPlatform()) {
        LOG_ERROR("Server", "Failed to init network platform");
        return false;
    }

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

    // Register gameplay systems (no render systems)
    // For now, empty world — systems will be added when we have entity replication

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

    characterRepo_ = std::make_unique<CharacterRepository>(gameDbConn_.connection());
    inventoryRepo_ = std::make_unique<InventoryRepository>(gameDbConn_.connection());

    // Auth server startup (warning only — game server can run without auth in dev)
    if (!authServer_.start(authPort_, tlsCertPath_, tlsKeyPath_, dbConnectionString_)) {
        LOG_WARN("Server", "Auth server failed to start on port %d; continuing without auth", authPort_);
    }

    LOG_INFO("Server", "Started on port %d at %.0f ticks/sec", port, TICK_RATE);
    return true;
}

void ServerApp::run() {
    running_ = true;
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

    authServer_.stop();
    gameDbConn_.disconnect();

    server_.stop();
    NetSocket::shutdownPlatform();
    LOG_INFO("Server", "Shutdown complete");
}

void ServerApp::tick(float dt) {
    // Reset per-tick move counters
    for (auto& [id, count] : moveCountThisTick_) count = 0;

    // 1. Drain incoming packets
    server_.poll(gameTime_);

    // 2. Drain auth results
    consumePendingSessions();

    // 3. World update (systems)
    world_.update(dt);

    // 4. Replicate entity state to connected clients
    replication_.update(world_, server_);

    // 5. Retransmit unacked reliable packets
    server_.processRetransmits(gameTime_);

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
            savePlayerToDB(oldClientId);

            auto* oldClient = server_.connections().findById(oldClientId);
            if (oldClient) {
                // Disconnect them
                if (server_.onClientDisconnected) server_.onClientDisconnected(oldClientId);
                server_.connections().removeClient(oldClientId);
            }
            activeAccountSessions_.erase(existing);
        }

        // Store pending session with 30s expiry
        result.session.expires_at = static_cast<double>(gameTime_) + 30.0;
        pendingSessions_[result.token] = result.session;
    }
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

        // Recalculate derived stats (resets currentHP/MP to max)
        s.recalculateStats();

        // Re-apply saved HP/MP after recalc
        s.currentHP    = rec.current_hp;
        s.currentMP    = rec.current_mp;
        s.currentFury  = rec.current_fury;
        s.maxHP        = rec.max_hp;
        s.maxMP        = rec.max_mp;
    }

    // Set position
    auto* t = player->getComponent<Transform>();
    if (t) {
        t->position = {rec.position_x, rec.position_y};
    }

    // Set gold on inventory
    auto* inv = player->getComponent<InventoryComponent>();
    if (inv) {
        inv->inventory.addGold(rec.gold);
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

    // Initialize movement tracking
    lastValidPositions_[clientId] = t ? t->position : Vec2{0.0f, 0.0f};
    lastMoveTime_[clientId] = gameTime_;
    moveCountThisTick_[clientId] = 0;

    // Send initial player state
    sendPlayerState(clientId);

    LOG_INFO("Server", "Client %d connected: account=%d char='%s' level=%d",
             clientId, session.account_id, rec.character_name.c_str(), rec.level);
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
        rec.position_x = t->position.x;
        rec.position_y = t->position.y;
    }

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
        if (h) {
            world_.destroyEntity(h);
            world_.processDestroyQueue();
        }
        replication_.unregisterEntity(h);
    }

    // Clean up movement tracking
    lastValidPositions_.erase(clientId);
    lastMoveTime_.erase(clientId);
    moveCountThisTick_.erase(clientId);
}

void ServerApp::onPacketReceived(uint16_t clientId, uint8_t type, ByteReader& payload) {
    // Handle game packets — stub for now, will be implemented in Phase 6C/6D
    switch (type) {
        case PacketType::CmdMove: {
            auto move = CmdMove::read(payload);

            // Rate limit check
            int maxPerTick = static_cast<int>(MAX_MOVES_PER_SEC / TICK_RATE);
            if (maxPerTick < 1) maxPerTick = 1;
            moveCountThisTick_[clientId]++;
            if (moveCountThisTick_[clientId] > maxPerTick) {
                LOG_WARN("Server", "Client %d exceeding move rate limit", clientId);
                break;
            }

            auto* client = server_.connections().findById(clientId);
            if (client && client->playerEntityId != 0) {
                PersistentId pid(client->playerEntityId);
                EntityHandle h = replication_.getEntityHandle(pid);
                Entity* e = world_.getEntity(h);
                if (!e) break;

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
        default:
            LOG_WARN("Server", "Unknown packet type 0x%02X from client %d", type, clientId);
            break;
    }
}

void ServerApp::processAction(uint16_t clientId, const CmdAction& action) {
    // Find attacker's player entity
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

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
        float maxRange = attackRange * 32.0f + 16.0f;
        float dist = attackerTransform->position.distance(targetTransform->position);
        if (dist > maxRange) {
            LOG_WARN("Server", "Client %d attack out of range (%.1f > %.1f)", clientId, dist, maxRange);
            return;
        }

        // Check target is a living enemy
        auto* enemyStats = target->getComponent<EnemyStatsComponent>();
        if (!enemyStats || !enemyStats->stats.isAlive) return;

        // Calculate damage
        int damage = 10; // default
        bool isCrit = false;
        if (charStats) {
            damage = charStats->stats.calculateDamage(false, isCrit);
        }

        // Apply damage
        enemyStats->stats.takeDamage(damage);
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

        if (killed) {
            LOG_INFO("Server", "Client %d killed target %llu", clientId,
                     static_cast<unsigned long long>(action.targetId));
            // Quest/XP integration deferred
        }
    } else {
        LOG_INFO("Server", "Unhandled action type %d from client %d", action.actionType, clientId);
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

    uint8_t buf[64];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvPlayerState, buf, w.size());
}

} // namespace fate
