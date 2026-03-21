#include "engine/net/replication.h"
#include "engine/net/packet.h"
#include "game/components/dropped_item_component.h"
#include <cmath>

namespace fate {

void ReplicationManager::update(World& world, NetServer& server) {
    // Rebuild spatial index once per tick with all registered entity positions
    rebuildSpatialIndex(world);

    server.connections().forEach([&](ClientConnection& client) {
        if (client.playerEntityId == 0) return; // not spawned yet
        buildVisibility(world, client);
        sendDiffs(world, server, client);
    });
}

void ReplicationManager::rebuildSpatialIndex(World& world) {
    spatialIndex_.beginRebuild(static_cast<uint32_t>(handleToPid_.size()));

    for (const auto& [handleValue, pid] : handleToPid_) {
        EntityHandle handle(handleValue);
        Entity* entity = world.getEntity(handle);
        if (!entity || !entity->isActive()) continue;

        auto* transform = entity->getComponent<Transform>();
        if (!transform) continue;

        // Use EntityHandle's packed value as EntityId for the spatial hash
        spatialIndex_.addEntity(handleValue, transform->position);
    }

    spatialIndex_.endRebuild();
}

void ReplicationManager::registerEntity(EntityHandle handle, PersistentId pid) {
    handleToPid_[handle.value] = pid;
    pidToHandle_[pid.value()] = handle;
    entitySeqCounters_[handle.value] = 0;
}

void ReplicationManager::unregisterEntity(EntityHandle handle) {
    auto it = handleToPid_.find(handle.value);
    if (it != handleToPid_.end()) {
        pidToHandle_.erase(it->second.value());
        handleToPid_.erase(it);
    }
    entitySeqCounters_.erase(handle.value);
}

PersistentId ReplicationManager::getPersistentId(EntityHandle handle) const {
    auto it = handleToPid_.find(handle.value);
    if (it != handleToPid_.end()) return it->second;
    return PersistentId::null();
}

EntityHandle ReplicationManager::getEntityHandle(PersistentId pid) const {
    auto it = pidToHandle_.find(pid.value());
    if (it != pidToHandle_.end()) return it->second;
    return EntityHandle{};
}

void ReplicationManager::buildVisibility(World& world, ClientConnection& client) {
    // Scene-based visibility: replicate ALL registered entities to the client
    // (except the client's own player). At current scale (12 mobs + few players
    // per zone), distance-based AOI adds complexity and causes invisible-mob bugs
    // without meaningful bandwidth savings. Re-add AOI only when zone populations
    // justify it (100+ entities).
    client.aoi.current.clear();

    for (const auto& [handleValue, pid] : handleToPid_) {
        // Exclude the client's own player entity
        if (pid.value() == client.playerEntityId) continue;

        // Verify entity still exists and is active
        Entity* entity = world.getEntity(EntityHandle(handleValue));
        if (!entity || !entity->isActive()) continue;

        client.aoi.current.push_back(EntityHandle(handleValue));
    }

    client.aoi.computeDiff();
    client.aoi.advance();
}

void ReplicationManager::sendDiffs(World& world, NetServer& server, ClientConnection& client) {
    // Process entered entities
    for (const auto& handle : client.aoi.entered) {
        PersistentId pid = getPersistentId(handle);
        if (pid.isNull()) continue;

        Entity* entity = world.getEntity(handle);
        if (!entity) continue;

        SvEntityEnterMsg enterMsg = buildEnterMessage(world, entity, pid);

        // Serialize and send reliable
        uint8_t buf[MAX_PAYLOAD_SIZE];
        ByteWriter writer(buf, sizeof(buf));
        enterMsg.write(writer);
        server.sendTo(client.clientId, Channel::ReliableOrdered,
                      PacketType::SvEntityEnter,
                      writer.data(), writer.size());

        // Initialize last acked state
        auto state = buildCurrentState(world, entity, pid);
        state.updateSeq = entitySeqCounters_[handle.value];
        client.lastAckedState[pid.value()] = state;
    }

    // Process left entities
    for (const auto& handle : client.aoi.left) {
        PersistentId pid = getPersistentId(handle);
        if (pid.isNull()) continue;

        SvEntityLeaveMsg leaveMsg;
        leaveMsg.persistentId = pid.value();

        uint8_t buf[MAX_PAYLOAD_SIZE];
        ByteWriter writer(buf, sizeof(buf));
        leaveMsg.write(writer);
        server.sendTo(client.clientId, Channel::ReliableOrdered,
                      PacketType::SvEntityLeave,
                      writer.data(), writer.size());

        client.lastAckedState.erase(pid.value());
    }

    // Process stayed entities (delta updates)
    for (const auto& handle : client.aoi.stayed) {
        PersistentId pid = getPersistentId(handle);
        if (pid.isNull()) continue;

        Entity* entity = world.getEntity(handle);
        if (!entity) continue;

        SvEntityUpdateMsg current = buildCurrentState(world, entity, pid);

        // Compare against last acked state to build delta
        auto lastIt = client.lastAckedState.find(pid.value());
        if (lastIt == client.lastAckedState.end()) continue;

        const SvEntityUpdateMsg& last = lastIt->second;
        uint16_t dirtyMask = 0;

        if (current.position.x != last.position.x || current.position.y != last.position.y)
            dirtyMask |= (1 << 0);
        if (current.animFrame != last.animFrame)
            dirtyMask |= (1 << 1);
        if (current.flipX != last.flipX)
            dirtyMask |= (1 << 2);
        if (current.currentHP != last.currentHP)
            dirtyMask |= (1 << 3);

        if (dirtyMask == 0) continue; // Nothing changed

        uint8_t& seq = entitySeqCounters_[handle.value];
        seq++; // wraps naturally at 255->0

        SvEntityUpdateMsg deltaMsg;
        deltaMsg.persistentId = pid.value();
        deltaMsg.fieldMask = dirtyMask;
        deltaMsg.position = current.position;
        deltaMsg.animFrame = current.animFrame;
        deltaMsg.flipX = current.flipX;
        deltaMsg.currentHP = current.currentHP;
        deltaMsg.updateSeq = seq;

        uint8_t buf[MAX_PAYLOAD_SIZE];
        ByteWriter writer(buf, sizeof(buf));
        deltaMsg.write(writer);
        server.sendTo(client.clientId, Channel::Unreliable,
                      PacketType::SvEntityUpdate,
                      writer.data(), writer.size());

        // Update last acked state
        lastIt->second = current;
        lastIt->second.updateSeq = seq;
    }
}

SvEntityEnterMsg ReplicationManager::buildEnterMessage(World& world, Entity* entity, PersistentId pid) {
    SvEntityEnterMsg msg;
    msg.persistentId = pid.value();

    // Position from Transform
    auto* transform = entity->getComponent<Transform>();
    if (transform) {
        msg.position = transform->position;
    }

    // Determine entity type and fill type-specific fields
    auto* charStats = entity->getComponent<CharacterStatsComponent>();
    auto* enemyStats = entity->getComponent<EnemyStatsComponent>();
    auto* npcComp = entity->getComponent<NPCComponent>();
    auto* droppedItem = entity->getComponent<DroppedItemComponent>();

    if (charStats) {
        msg.entityType = 0; // player
        msg.level = charStats->stats.level;
        msg.currentHP = charStats->stats.currentHP;
        msg.maxHP = charStats->stats.maxHP;

        auto* nameplate = entity->getComponent<NameplateComponent>();
        if (nameplate) {
            msg.name = nameplate->displayName;
        }
    } else if (enemyStats) {
        msg.entityType = 1; // mob
        msg.level = enemyStats->stats.level;
        msg.currentHP = enemyStats->stats.currentHP;
        msg.maxHP = enemyStats->stats.maxHP;

        auto* mobNameplate = entity->getComponent<MobNameplateComponent>();
        if (mobNameplate) {
            msg.name = mobNameplate->displayName;
        }
        msg.mobDefId = enemyStats->stats.enemyId;  // enemyId stores the mob_def_id
        msg.isBoss   = enemyStats->stats.isBoss ? 1 : 0;
    } else if (npcComp) {
        msg.entityType = 2; // npc
        msg.name = npcComp->displayName;
    } else if (droppedItem) {
        msg.entityType = 3; // dropped item
        msg.name = droppedItem->isGold ? "Gold" : droppedItem->itemId;
        msg.itemId = droppedItem->itemId;
        msg.quantity = droppedItem->quantity;
        msg.isGold = droppedItem->isGold ? 1 : 0;
        msg.goldAmount = droppedItem->goldAmount;
        msg.enchantLevel = droppedItem->enchantLevel;
        msg.rarity = droppedItem->rarity;
    }

    // Faction
    auto* factionComp = entity->getComponent<FactionComponent>();
    if (factionComp) {
        msg.faction = static_cast<uint8_t>(factionComp->faction);
    }

    return msg;
}

SvEntityUpdateMsg ReplicationManager::buildCurrentState(World& world, Entity* entity, PersistentId pid) {
    SvEntityUpdateMsg msg;
    msg.persistentId = pid.value();
    msg.fieldMask = 0x000F; // all 4 bits set

    // Position
    auto* transform = entity->getComponent<Transform>();
    if (transform) {
        msg.position = transform->position;
    }

    // Animation frame and flip
    auto* sprite = entity->getComponent<SpriteComponent>();
    if (sprite) {
        msg.animFrame = static_cast<uint8_t>(sprite->currentFrame);
        msg.flipX = sprite->flipX ? 1 : 0;
    }

    // HP
    auto* charStats = entity->getComponent<CharacterStatsComponent>();
    if (charStats) {
        msg.currentHP = charStats->stats.currentHP;
    } else {
        auto* enemyStats = entity->getComponent<EnemyStatsComponent>();
        if (enemyStats) {
            msg.currentHP = enemyStats->stats.currentHP;
        }
    }

    return msg;
}

} // namespace fate
