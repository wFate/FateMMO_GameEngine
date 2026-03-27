#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "game/shared/game_types.h"
#include "game/shared/honor_system.h"
#include "engine/net/game_messages.h"

namespace fate {

void ServerApp::processPKAttack(uint16_t attackerClientId, uint16_t targetClientId, int damage) {
    if (damage <= 0) return;

    auto* attackerConn = server_.connections().findById(attackerClientId);
    auto* targetConn = server_.connections().findById(targetClientId);
    if (!attackerConn || !targetConn) return;

    World& world = getWorldForClient(attackerClientId);
    ReplicationManager& repl = getReplicationForClient(attackerClientId);

    PersistentId attackerPid(attackerConn->playerEntityId);
    EntityHandle attackerH = repl.getEntityHandle(attackerPid);
    Entity* attackerEntity = world.getEntity(attackerH);

    PersistentId targetPid(targetConn->playerEntityId);
    EntityHandle targetH = repl.getEntityHandle(targetPid);
    Entity* targetEntity = world.getEntity(targetH);

    if (!attackerEntity || !targetEntity) return;

    auto* attackerStats = attackerEntity->getComponent<CharacterStatsComponent>();
    auto* targetStats = targetEntity->getComponent<CharacterStatsComponent>();
    if (!attackerStats || !targetStats) return;

    attackerStats->stats.enterCombat();
    targetStats->stats.enterCombat();
    playerDirty_[targetClientId].vitals = true;

    // Attacker becomes Aggressor if target is innocent
    if (targetStats->stats.pkStatus == PKStatus::White) {
        attackerStats->stats.flagAsAggressor();
        playerDirty_[attackerClientId].stats = true;
        enqueuePersist(attackerClientId, PersistPriority::HIGH, PersistType::Character);
    }
}

void ServerApp::processPKKill(uint16_t killerClientId, uint16_t victimClientId) {
    auto* killerConn = server_.connections().findById(killerClientId);
    auto* victimConn = server_.connections().findById(victimClientId);
    if (!killerConn || !victimConn) return;

    World& world = getWorldForClient(killerClientId);
    ReplicationManager& repl = getReplicationForClient(killerClientId);

    PersistentId killerPid(killerConn->playerEntityId);
    EntityHandle killerH = repl.getEntityHandle(killerPid);
    Entity* killerEntity = world.getEntity(killerH);

    PersistentId victimPid(victimConn->playerEntityId);
    EntityHandle victimH = repl.getEntityHandle(victimPid);
    Entity* victimEntity = world.getEntity(victimH);

    if (!killerEntity || !victimEntity) return;

    auto* killerStats = killerEntity->getComponent<CharacterStatsComponent>();
    auto* victimStats = victimEntity->getComponent<CharacterStatsComponent>();
    if (!killerStats || !victimStats) return;

    // Flag killer as Murderer if victim was innocent
    if (victimStats->stats.pkStatus == PKStatus::White) {
        killerStats->stats.flagAsMurderer();
        playerDirty_[killerClientId].stats = true;
        enqueuePersist(killerClientId, PersistPriority::HIGH, PersistType::Character);
    }

    // Award PvP kill/death
    killerStats->stats.pvpKills++;
    playerDirty_[killerClientId].stats = true;
    victimStats->stats.pvpDeaths++;

    // Honor system (DB-backed kill tracking)
    std::string targetCharId = victimStats->stats.characterId;
    if (!targetCharId.empty()) {
        int recentKills = pvpKillLogRepo_->countRecentKills(
            killerConn->character_id, targetCharId);
        auto honorResult = HonorSystem::processKillWithCount(
            killerStats->stats.pkStatus, victimStats->stats.pkStatus,
            recentKills, victimStats->stats.honor);

        if (honorResult.attackerGain > 0) {
            killerStats->stats.honor = (std::min)(HonorSystem::MAX_HONOR,
                killerStats->stats.honor + honorResult.attackerGain);
            playerDirty_[killerClientId].stats = true;
            enqueuePersist(killerClientId, PersistPriority::HIGH, PersistType::Character);
        }
        if (honorResult.victimLoss > 0) {
            victimStats->stats.honor = (std::max)(0,
                victimStats->stats.honor - honorResult.victimLoss);
            playerDirty_[victimClientId].stats = true;
            enqueuePersist(victimClientId, PersistPriority::HIGH, PersistType::Character);
        }

        pvpKillLogRepo_->recordKill(killerConn->character_id, targetCharId);

        LOG_INFO("Server", "PvP honor: %s gained %d, %s lost %d (recent=%d)",
                 killerConn->character_id.c_str(), honorResult.attackerGain,
                 targetCharId.c_str(), honorResult.victimLoss, recentKills);
    }

    // Send death notification to the killed player
    playerDirty_[victimClientId].stats = true;
    playerDirty_[victimClientId].vitals = true;
    SvDeathNotifyMsg deathMsg;
    deathMsg.deathSource = 1; // PvP
    // Override for Aurora zones
    if (isAuroraScene(victimStats->stats.currentScene)) {
        deathMsg.deathSource = static_cast<uint8_t>(DeathSource::Aurora);
    }
    deathMsg.xpLost = 0;
    deathMsg.honorLost = 0;
    deathMsg.respawnTimer = 5.0f;
    uint8_t dbuf[64];
    ByteWriter dw(dbuf, sizeof(dbuf));
    deathMsg.write(dw);
    server_.sendTo(victimClientId, Channel::ReliableOrdered,
                   PacketType::SvDeathNotify, dbuf, dw.size());

    LOG_INFO("Server", "Client %d killed player %d in PvP", killerClientId, victimClientId);
}

} // namespace fate
