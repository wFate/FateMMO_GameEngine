#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include "engine/net/game_messages.h"

namespace fate {

void ServerApp::processRespawn(uint16_t clientId, const CmdRespawnMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
    if (!e) return;

    auto* sc = e->getComponent<CharacterStatsComponent>();
    if (!sc) return;

    // Aurora recall: living players in Aurora can use town respawn to leave
    if (!sc->stats.isDead && msg.respawnType == 0 && isAuroraScene(sc->stats.currentScene)) {
        auto* t = e->getComponent<Transform>();
        auto* townScene = sceneCache_.get("Town");
        float spX = townScene ? townScene->defaultSpawnX : 0.0f;
        float spY = townScene ? townScene->defaultSpawnY : 0.0f;

        removeAuroraBuffs(e);

        sc->stats.currentScene = "Town";
        if (t) t->position = {spX, spY};
        lastValidPositions_[clientId] = {spX, spY};
        lastMoveTime_[clientId] = gameTime_;
        needsFirstMoveSync_.insert(clientId);
        playerDirty_[clientId].position = true;

        client->aoi.previous.clear();
        client->aoi.current.clear();
        client->aoi.entered.clear();
        client->aoi.left.clear();
        client->aoi.stayed.clear();
        client->lastSentState.clear();

        SvZoneTransitionMsg ztResp;
        ztResp.targetScene = "Town";
        ztResp.spawnX = spX;
        ztResp.spawnY = spY;
        uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
        ztResp.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvZoneTransition, buf, w.size());

        LOG_INFO("Server", "Client %d recalled from Aurora to Town", clientId);
        savePlayerToDBAsync(clientId);
        return;
    }

    // Reject respawn if player is not actually dead (prevents double-respawn exploits)
    if (!sc->stats.isDead) {
        LOG_WARN("Server", "Client %d respawn rejected: not dead", clientId);
        return;
    }

    // Check timer for type 0 (town) and type 1 (map spawn)
    if (msg.respawnType <= 1 && sc->stats.respawnTimeRemaining > 0.0f) {
        LOG_WARN("Server", "Client %d respawn rejected: timer still %.1fs",
                 clientId, sc->stats.respawnTimeRemaining);
        return;
    }

    // Type 2: Phoenix Down — validate and consume
    if (msg.respawnType == 2) {
        auto* invComp = e->getComponent<InventoryComponent>();
        if (!invComp) return;
        int slot = invComp->inventory.findItemById("phoenix_down");
        if (slot < 0) {
            LOG_WARN("Server", "Client %d tried Phoenix Down respawn but has none", clientId);
            return;
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

    // Block town-respawn while inside a dungeon instance
    uint32_t dungeonInstId = dungeonManager_.getInstanceForClient(clientId);
    if (msg.respawnType == 0 && dungeonInstId != 0) {
        LOG_WARN("Server", "Client %d tried town respawn while in dungeon instance %u", clientId, dungeonInstId);
        return;
    }

    if (msg.respawnType == 0 && sceneName != "Town") {
        // Town respawn from another scene — zone transition + respawn
        auto* townScene = sceneCache_.get("Town");
        float spX = townScene ? townScene->defaultSpawnX : 0.0f;
        float spY = townScene ? townScene->defaultSpawnY : 0.0f;

        if (sc->stats.isDead) sc->stats.respawn();
        // Purge this player from all mob threat tables so mobs re-acquire normally
        {
            uint32_t eid = client->playerEntityId;
            World& w = getWorldForClient(clientId);
            w.forEach<EnemyStatsComponent>([eid](Entity*, EnemyStatsComponent* esc) {
                esc->stats.damageByAttacker.erase(eid);
            });
        }
        // Remove Aurora buffs when leaving Aurora via death
        if (isAuroraScene(sceneName)) {
            removeAuroraBuffs(e);
        }
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
        return;
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
    // Purge this player from all mob threat tables so mobs re-acquire normally
    {
        uint32_t eid = client->playerEntityId;
        World& w = getWorldForClient(clientId);
        w.forEach<EnemyStatsComponent>([eid](Entity*, EnemyStatsComponent* esc) {
            esc->stats.damageByAttacker.erase(eid);
        });
    }
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
}

} // namespace fate
