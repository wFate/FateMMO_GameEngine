#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include "engine/net/game_messages.h"
#include <cmath>

namespace fate {

void ServerApp::processZoneTransition(uint16_t clientId, const CmdZoneTransition& cmd) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
    if (!e) return;

    // Validate target scene exists (lookup by scene_id, not display name)
    const SceneInfoRecord* targetScene = sceneCache_.get(cmd.targetScene);
    if (!targetScene) {
        LOG_WARN("Server", "Client %d zone transition to unknown scene '%s'",
                 clientId, cmd.targetScene.c_str());
        return; // reject — scene does not exist
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
            return; // block transition
        }
    }

    // Portal proximity check: verify player is near a registered portal
    const ServerPortal* matchedPortal = nullptr;
    if (!portals_.empty()) {
        auto* charStats = e->getComponent<CharacterStatsComponent>();
        std::string currentScene = charStats ? charStats->stats.currentScene : "";
        auto* t = e->getComponent<Transform>();
        float px = t ? t->position.x : 0.0f;
        float py = t ? t->position.y : 0.0f;

        for (const auto& portal : portals_) {
            if (portal.sourceScene == currentScene && portal.targetScene == cmd.targetScene) {
                if (std::abs(px - portal.x) <= portal.halfW + 16.0f &&
                    std::abs(py - portal.y) <= portal.halfH + 16.0f) {
                    matchedPortal = &portal;
                    break;
                }
            }
        }

        if (!matchedPortal) {
            LOG_WARN("Server", "Client %d zone transition to '%s' rejected: not near portal (pos=%.0f,%.0f scene=%s)",
                     clientId, cmd.targetScene.c_str(), px, py, currentScene.c_str());
            return;
        }
    }

    // Cancel any active trade before transitioning
    if (client->activeTradeSessionId != 0) {
        int cancelSid = client->activeTradeSessionId;
        tradeRepo_->cancelSession(cancelSid);
        std::string otherCharId = client->tradePartnerCharId;
        server_.connections().forEach([&](ClientConnection& c) {
            if (c.character_id == otherCharId) {
                c.activeTradeSessionId = 0;
                c.tradePartnerCharId.clear();
                SvTradeUpdateMsg cancelMsg;
                cancelMsg.updateType = 6; // cancelled
                cancelMsg.resultCode = 10; // partner zoned
                cancelMsg.otherPlayerName = "Trade cancelled — other player left the area";
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                cancelMsg.write(w);
                server_.sendTo(c.clientId, Channel::ReliableOrdered,
                               PacketType::SvTradeUpdate, buf, w.size());
            }
        });
        LOG_INFO("Server", "Cancelled trade session %d — client %d zone transition",
                 cancelSid, clientId);
        client->activeTradeSessionId = 0;
        client->tradePartnerCharId.clear();
    }

    // Transition allowed — send SvZoneTransition back to client
    LOG_INFO("Server", "Client %d zone transition -> '%s'", clientId, cmd.targetScene.c_str());

    // Server-authoritative spawn resolution: never use client coordinates
    float spawnX = 0.0f;
    float spawnY = 0.0f;
    if (matchedPortal) {
        spawnX = matchedPortal->targetSpawnX;
        spawnY = matchedPortal->targetSpawnY;
    }
    // Fallback to scene default (for GM teleport or scripted transitions without portal)
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
        auto* client2 = server_.connections().findById(clientId);
        if (client2 && client2->playerEntityId != 0) {
            PersistentId pid2(client2->playerEntityId);
            EntityHandle h2 = getReplicationForClient(clientId).getEntityHandle(pid2);
            Entity* e2 = getWorldForClient(clientId).getEntity(h2);
            std::string previousScene;
            if (e2) {
                auto* sc2 = e2->getComponent<CharacterStatsComponent>();
                if (sc2) {
                    previousScene = sc2->stats.currentScene;
                    sc2->stats.currentScene = cmd.targetScene;
                    sc2->stats.combatTimer = 0.0f; // H20-FIX
                }
            }
            // Purge this player's damage from all mob threat tables in the old scene
            {
                uint32_t eid = static_cast<uint32_t>(client2->playerEntityId);
                World& w2 = getWorldForClient(clientId);
                w2.forEach<EnemyStatsComponent>([eid](Entity*, EnemyStatsComponent* esc) {
                    esc->stats.damageByAttacker.erase(eid);
                });
            }

            lastAutoAttackTime_.erase(clientId);
            playerDirty_[clientId].position = true;

            // Clear AOI state so the replication system sends fresh
            // SvEntityEnter messages for all entities near the new position.
            client2->aoi.previous.clear();
            client2->aoi.current.clear();
            client2->aoi.entered.clear();
            client2->aoi.left.clear();
            client2->aoi.stayed.clear();
            client2->lastSentState.clear();

            // Apply Aurora buffs if entering an Aurora zone
            if (e2 && isAuroraScene(cmd.targetScene)) {
                applyAuroraBuffs(clientId, e2);
                sendAuroraStatus(clientId);
            }
            // Remove Aurora buffs if leaving Aurora
            if (e2 && isAuroraScene(previousScene) && !isAuroraScene(cmd.targetScene)) {
                removeAuroraBuffs(e2);
            }
        }
    }

    // Update movement tracking for the new scene position
    lastValidPositions_[clientId] = {spawnX, spawnY};
    lastMoveTime_[clientId] = gameTime_;
    needsFirstMoveSync_.insert(clientId);

    // Save updated scene to DB asynchronously
    savePlayerToDBAsync(clientId);
}

} // namespace fate
