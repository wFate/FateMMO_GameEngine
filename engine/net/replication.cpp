#include "engine/net/replication.h"
#include "engine/net/packet.h"
#include "game/components/dropped_item_component.h"
#include "game/components/game_components.h"
#include "game/shared/honor_system.h"
#include "server/cache/item_definition_cache.h"
#include "server/cache/costume_cache.h"
#include <cmath>

namespace fate {

void ReplicationManager::update(World& world, NetServer& server) {
    ++tickCounter_;
    // NOTE: Spatial index rebuild is commented out — it was rebuilt every tick but
    // never queried, wasting CPU. Scene-based filtering (buildVisibility iterates all
    // entities in the same scene) is sufficient at current scale. Distance-based AOI
    // was removed because a 128px hysteresis gap caused entity flickering at boundaries
    // (activate 640px / deactivate 768px was too narrow for moving mobs). Re-enable
    // distance-based AOI with wider hysteresis (e.g. 500/900 + min visibility duration)
    // only when zone populations exceed ~200 entities and bandwidth becomes a bottleneck.
    // rebuildSpatialIndex(world);

    server.connections().forEach([&](ClientConnection& client) {
        if (client.playerEntityId == 0) return; // not spawned yet
        buildVisibility(world, client);
        sendDiffs(world, server, client);
    });

    // Clear after all diffs are sent — unregisterEntity() populates this during the tick
    recentlyUnregistered_.clear();
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

// IMPORTANT: Do NOT erase handleToPid_ without saving the PID first.
// sendDiffs() needs the PID to send SvEntityLeave for entities that left the AOI.
// If the PID is erased before sendDiffs runs, the leave message is silently skipped
// and the client keeps a zombie ghost entity forever.
void ReplicationManager::unregisterEntity(EntityHandle handle) {
    auto it = handleToPid_.find(handle.value);
    if (it != handleToPid_.end()) {
        recentlyUnregistered_[handle.value] = it->second;
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
    // Scene-filtered visibility: only replicate entities that share the client's
    // current scene. Without this filter, mobs from other zones leak through
    // as ghost entities (visible but non-interactive).
    client.aoi.current.clear();

    // Determine the client's current scene from their player entity
    std::string clientScene;
    if (client.playerEntityId != 0) {
        auto pit = pidToHandle_.find(client.playerEntityId);
        if (pit != pidToHandle_.end()) {
            Entity* playerEntity = world.getEntity(pit->second);
            if (playerEntity) {
                auto* cs = playerEntity->getComponent<CharacterStatsComponent>();
                if (cs) clientScene = cs->stats.currentScene;
            }
        }
    }

    for (const auto& [handleValue, pid] : handleToPid_) {
        // Exclude the client's own player entity
        if (pid.value() == client.playerEntityId) continue;

        // Verify entity still exists and is active
        Entity* entity = world.getEntity(EntityHandle(handleValue));
        if (!entity || !entity->isActive()) continue;

        // Scene filter: only include entities in the same scene as the client
        if (!clientScene.empty()) {
            // Check mob scene (EnemyStatsComponent::sceneId)
            auto* es = entity->getComponent<EnemyStatsComponent>();
            if (es && !es->stats.sceneId.empty() && es->stats.sceneId != clientScene) continue;

            // Check player scene (CharacterStatsComponent::currentScene)
            auto* otherCs = entity->getComponent<CharacterStatsComponent>();
            if (otherCs && !otherCs->stats.currentScene.empty() && otherCs->stats.currentScene != clientScene) continue;

            // Check NPC scene (NPCComponent::sceneId)
            auto* npc = entity->getComponent<NPCComponent>();
            if (npc && !npc->sceneId.empty() && npc->sceneId != clientScene) continue;

            // Check dropped item scene (DroppedItemComponent::sceneId)
            auto* drop = entity->getComponent<DroppedItemComponent>();
            if (drop && !drop->sceneId.empty() && drop->sceneId != clientScene) continue;
        }

        // Custom visibility filter (e.g. GM invisibility)
        if (visibilityFilter && visibilityFilter(pid.value(), client)) continue;

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

        // Initialize last sent state
        auto state = buildCurrentState(world, entity, pid);
        state.updateSeq = entitySeqCounters_[handle.value];
        client.lastSentState[pid.value()] = state;
    }

    // Process left entities
    for (const auto& handle : client.aoi.left) {
        PersistentId pid = getPersistentId(handle);
        // Fallback: entity was unregistered this tick (pickup/despawn) — PID already erased
        if (pid.isNull()) {
            auto uit = recentlyUnregistered_.find(handle.value);
            if (uit != recentlyUnregistered_.end()) pid = uit->second;
        }
        if (pid.isNull()) continue;

        SvEntityLeaveMsg leaveMsg;
        leaveMsg.persistentId = pid.value();

        uint8_t buf[MAX_PAYLOAD_SIZE];
        ByteWriter writer(buf, sizeof(buf));
        leaveMsg.write(writer);
        server.sendTo(client.clientId, Channel::ReliableOrdered,
                      PacketType::SvEntityLeave,
                      writer.data(), writer.size());

        client.lastSentState.erase(pid.value());
    }

    // Process stayed entities (delta updates) — batched into single packets

    // Compute client player position once for tier checks
    Vec2 clientPos{};
    {
        auto clientHandle = getEntityHandle(PersistentId(client.playerEntityId));
        Entity* clientEntity = world.getEntity(clientHandle);
        if (clientEntity) {
            auto* ct = clientEntity->getComponent<Transform>();
            if (ct) clientPos = ct->position;
        }
    }

    // Batch buffer: accumulate multiple entity deltas into one packet
    uint8_t batchBuf[MAX_PAYLOAD_SIZE];
    ByteWriter batchWriter(batchBuf, sizeof(batchBuf));
    // Reserve 1 byte for entity count at the start
    batchWriter.writeU8(0);
    uint8_t batchCount = 0;

    auto flushBatch = [&]() {
        if (batchCount == 0) return;
        // Patch the count byte at position 0
        batchBuf[0] = batchCount;
        server.sendTo(client.clientId, Channel::Unreliable,
                      PacketType::SvEntityUpdateBatch,
                      batchWriter.data(), batchWriter.size());
        // Reset for next batch
        batchWriter = ByteWriter(batchBuf, sizeof(batchBuf));
        batchWriter.writeU8(0);
        batchCount = 0;
    };

    for (const auto& handle : client.aoi.stayed) {
        PersistentId pid = getPersistentId(handle);
        if (pid.isNull()) continue;

        Entity* entity = world.getEntity(handle);
        if (!entity) continue;

        SvEntityUpdateMsg current = buildCurrentState(world, entity, pid);

        // Compare against last sent state to build delta
        auto lastIt = client.lastSentState.find(pid.value());
        if (lastIt == client.lastSentState.end()) continue;

        const SvEntityUpdateMsg& last = lastIt->second;

        // Tiered update frequency — skip if not this entity's turn
        auto* entityTransform = entity->getComponent<Transform>();
        if (entityTransform) {
            float dx = entityTransform->position.x - clientPos.x;
            float dy = entityTransform->position.y - clientPos.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            UpdateTier tier = getUpdateTier(dist);

            // Always send HP changes regardless of tier
            bool hasHPChange = (current.currentHP != last.currentHP);
            if (!hasHPChange && !shouldSendUpdate(tier, tickCounter_)) {
                continue;
            }
        }
        uint32_t dirtyMask = 0;

        if (current.position.x != last.position.x || current.position.y != last.position.y)
            dirtyMask |= (1 << 0);
        if (current.animFrame != last.animFrame)
            dirtyMask |= (1 << 1);
        if (current.flipX != last.flipX)
            dirtyMask |= (1 << 2);
        if (current.currentHP != last.currentHP)
            dirtyMask |= (1 << 3);
        if (current.maxHP != last.maxHP)
            dirtyMask |= (1 << 4);
        if (current.moveState != last.moveState)
            dirtyMask |= (1 << 5);
        if (current.animId != last.animId)
            dirtyMask |= (1 << 6);
        if (current.statusEffectMask != last.statusEffectMask)
            dirtyMask |= (1 << 7);
        if (current.deathState != last.deathState)
            dirtyMask |= (1 << 8);
        if (current.castingSkillId != last.castingSkillId || current.castingProgress != last.castingProgress)
            dirtyMask |= (1 << 9);
        if (current.targetEntityId != last.targetEntityId)
            dirtyMask |= (1 << 10);
        if (current.level != last.level)
            dirtyMask |= (1 << 11);
        if (current.faction != last.faction)
            dirtyMask |= (1 << 12);
        if (current.armorStyle != last.armorStyle ||
            current.hatStyle != last.hatStyle ||
            current.weaponStyle != last.weaponStyle)
            dirtyMask |= (1 << 13);
        if (current.pkStatus != last.pkStatus)
            dirtyMask |= (1 << 14);
        if (current.honorRank != last.honorRank)
            dirtyMask |= (1 << 15);
        if (current.costumeVisuals != last.costumeVisuals)
            dirtyMask |= (1 << 16);

        if (dirtyMask == 0) continue; // Nothing changed

        uint8_t& seq = entitySeqCounters_[handle.value];
        seq++; // wraps naturally at 255->0

        SvEntityUpdateMsg deltaMsg;
        deltaMsg.persistentId    = pid.value();
        deltaMsg.fieldMask       = dirtyMask;
        deltaMsg.position        = current.position;
        deltaMsg.animFrame       = current.animFrame;
        deltaMsg.flipX           = current.flipX;
        deltaMsg.currentHP       = current.currentHP;
        deltaMsg.maxHP           = current.maxHP;
        deltaMsg.moveState       = current.moveState;
        deltaMsg.animId          = current.animId;
        deltaMsg.statusEffectMask = current.statusEffectMask;
        deltaMsg.deathState      = current.deathState;
        deltaMsg.castingSkillId  = current.castingSkillId;
        deltaMsg.castingProgress = current.castingProgress;
        deltaMsg.targetEntityId  = current.targetEntityId;
        deltaMsg.level           = current.level;
        deltaMsg.faction         = current.faction;
        deltaMsg.armorStyle      = current.armorStyle;
        deltaMsg.hatStyle        = current.hatStyle;
        deltaMsg.weaponStyle     = current.weaponStyle;
        deltaMsg.pkStatus        = current.pkStatus;
        deltaMsg.honorRank       = current.honorRank;
        deltaMsg.costumeVisuals  = current.costumeVisuals;
        deltaMsg.updateSeq       = seq;

        // Serialize delta into a temp buffer to check size
        uint8_t tmpBuf[MAX_PAYLOAD_SIZE];
        ByteWriter tmpWriter(tmpBuf, sizeof(tmpBuf));
        deltaMsg.write(tmpWriter);

        // If this delta won't fit in the current batch, flush first
        if (batchWriter.size() + tmpWriter.size() > MAX_PAYLOAD_SIZE) {
            flushBatch();
        }

        // Write delta into batch buffer
        deltaMsg.write(batchWriter);
        batchCount++;

        // Update last sent state
        lastIt->second = current;
        lastIt->second.updateSeq = seq;
    }

    // Flush remaining deltas
    flushBatch();
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
        msg.pkStatus  = static_cast<uint8_t>(charStats->stats.pkStatus);
        msg.honorRank = static_cast<uint8_t>(HonorSystem::getHonorRank(charStats->stats.honor));

        auto* appearance = entity->getComponent<AppearanceComponent>();
        if (appearance) {
            msg.gender    = appearance->gender;
            msg.hairstyle = appearance->hairstyle;
        }

        auto* nameplate = entity->getComponent<NameplateComponent>();
        if (nameplate) {
            msg.name = nameplate->displayName;
        }

        auto* costumeComp = entity->getComponent<CostumeComponent>();
        if (costumeComp && costumeComp->showCostumes && costumeCache_) {
            uint16_t cW = 0, cA = 0, cH = 0, cS = 0, cG = 0, cB = 0;
            for (const auto& [slot, defId] : costumeComp->equippedBySlot) {
                const auto* def = costumeCache_->get(defId);
                if (!def) continue;
                switch (static_cast<EquipmentSlot>(slot)) {
                    case EquipmentSlot::Weapon:    cW = def->visualIndex; break;
                    case EquipmentSlot::Armor:     cA = def->visualIndex; break;
                    case EquipmentSlot::Hat:       cH = def->visualIndex; break;
                    case EquipmentSlot::SubWeapon: cS = def->visualIndex; break;
                    case EquipmentSlot::Gloves:    cG = def->visualIndex; break;
                    case EquipmentSlot::Shoes:     cB = def->visualIndex; break;
                    default: break;
                }
            }
            msg.costumeVisuals = packCostumeVisuals(cW, cA, cH, cS, cG, cB);
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
    msg.fieldMask = 0x1FFFF; // bits 0-16 all set

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

    // HP, maxHP, level
    auto* charStats = entity->getComponent<CharacterStatsComponent>();
    if (charStats) {
        msg.currentHP = charStats->stats.currentHP;
        msg.maxHP     = charStats->stats.maxHP;
        msg.level     = static_cast<uint8_t>(charStats->stats.level);
        msg.pkStatus  = static_cast<uint8_t>(charStats->stats.pkStatus);
        msg.honorRank = static_cast<uint8_t>(HonorSystem::getHonorRank(charStats->stats.honor));
    } else {
        auto* enemyStats = entity->getComponent<EnemyStatsComponent>();
        if (enemyStats) {
            msg.currentHP = enemyStats->stats.currentHP;
            msg.maxHP     = enemyStats->stats.maxHP;
            msg.level     = static_cast<uint8_t>(enemyStats->stats.level);
        }
    }

    // Faction
    auto* factionComp = entity->getComponent<FactionComponent>();
    if (factionComp) {
        msg.faction = static_cast<uint8_t>(factionComp->faction);
    }

    // Status effect mask from active effects
    auto* seComp = entity->getComponent<StatusEffectComponent>();
    msg.statusEffectMask = seComp ? seComp->effects.getActiveEffectMask() : 0;

    // Death state from character/enemy stats
    if (charStats) {
        msg.deathState = charStats->stats.isAlive() ? 0 : static_cast<uint8_t>(charStats->stats.lifeState); // 0=alive, 1=dying, 2=dead
    } else {
        auto* es2 = entity->getComponent<EnemyStatsComponent>();
        msg.deathState = (es2 && !es2->stats.isAlive) ? 2 : 0;
    }

    // moveState: walking/idle from PlayerController or MobAI
    auto* pc = entity->getComponent<PlayerController>();
    auto* mobAi = entity->getComponent<MobAIComponent>();
    if (pc) {
        msg.moveState = pc->isMoving ? static_cast<uint8_t>(MoveState::Walking)
                                     : static_cast<uint8_t>(MoveState::Idle);
    } else if (mobAi) {
        auto mode = mobAi->ai.getMode();
        msg.moveState = (mode == AIMode::Chase || mode == AIMode::ReturnHome || mode == AIMode::Roam)
                        ? static_cast<uint8_t>(MoveState::Walking)
                        : static_cast<uint8_t>(MoveState::Idle);
    }

    // animId: direction + animation type
    // Note: animType 2=attack and 3=cast are deferred until server tracks attack/cast state per-entity
    {
        uint8_t animDir = 0;
        uint8_t animType = msg.moveState; // 0=idle, 1=walk maps directly
        if (pc) {
            animDir = facingToAnimDir(pc->facing);
        } else if (mobAi) {
            animDir = facingToAnimDir(mobAi->ai.getFacingDirection());
        }
        // Death override
        if (msg.deathState >= 2) {
            msg.animId = 12; // death animation
        } else {
            msg.animId = encodeAnimId(animDir, animType);
        }
    }

    // castingSkillId + castingProgress: deferred (no server-side cast times yet)
    msg.castingSkillId  = 0;
    msg.castingProgress = 0;

    // targetEntityId: from TargetingComponent
    auto* targeting = entity->getComponent<TargetingComponent>();
    msg.targetEntityId = targeting ? static_cast<uint16_t>(targeting->selectedTargetId & 0xFFFF) : 0;

    // Equipment visual styles
    auto* invComp = entity->getComponent<InventoryComponent>();
    if (invComp && itemDefCache_) {
        const auto& equip = invComp->inventory.getEquipmentMap();
        auto getStyle = [&](EquipmentSlot slot) -> std::string {
            auto it = equip.find(slot);
            if (it != equip.end() && it->second.isValid())
                return itemDefCache_->getVisualStyle(it->second.itemId);
            return "";
        };
        msg.weaponStyle = getStyle(EquipmentSlot::Weapon);
        msg.armorStyle  = getStyle(EquipmentSlot::Armor);
        msg.hatStyle    = getStyle(EquipmentSlot::Hat);
    } else {
        msg.weaponStyle.clear();
        msg.armorStyle.clear();
        msg.hatStyle.clear();
    }

    // costumeVisuals: packed costume visual indices (6 slots)
    auto* costumeComp = entity->getComponent<CostumeComponent>();
    if (costumeComp && costumeComp->showCostumes && costumeCache_) {
        uint16_t cWeapon = 0, cArmor = 0, cHat = 0, cShield = 0, cGloves = 0, cBoots = 0;
        for (const auto& [slot, defId] : costumeComp->equippedBySlot) {
            const auto* def = costumeCache_->get(defId);
            if (!def) continue;
            switch (static_cast<EquipmentSlot>(slot)) {
                case EquipmentSlot::Weapon:    cWeapon = def->visualIndex; break;
                case EquipmentSlot::Armor:     cArmor  = def->visualIndex; break;
                case EquipmentSlot::Hat:       cHat    = def->visualIndex; break;
                case EquipmentSlot::SubWeapon: cShield = def->visualIndex; break;
                case EquipmentSlot::Gloves:    cGloves = def->visualIndex; break;
                case EquipmentSlot::Shoes:     cBoots  = def->visualIndex; break;
                default: break;
            }
        }
        msg.costumeVisuals = packCostumeVisuals(cWeapon, cArmor, cHat, cShield, cGloves, cBoots);
    } else {
        msg.costumeVisuals = 0;
    }

    return msg;
}

} // namespace fate
