#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "game/components/pet_component.h"
#include "game/components/dropped_item_component.h"
#include "game/shared/game_types.h"
#include "game/shared/xp_calculator.h"
#include "game/shared/item_stat_roller.h"
#include "game/shared/pet_system.h"
#include "game/entity_factory.h"
#include "engine/net/game_messages.h"
#include <random>

namespace fate {

void ServerApp::processMobDeath(
    uint16_t killerClientId,
    CharacterStatsComponent* killerStats,
    EnemyStatsComponent* mobStats,
    Vec2 deathPos,
    World& world,
    ReplicationManager& repl)
{
    EnemyStats& es = mobStats->stats;
    auto* client = server_.connections().findById(killerClientId);
    if (!client) return;

    // Resolve killer entity
    PersistentId killerPid(client->playerEntityId);
    EntityHandle killerHandle = repl.getEntityHandle(killerPid);
    Entity* killer = world.getEntity(killerHandle);
    if (!killer) return;

    // Find target entity for sprite hiding / component access
    // (mobStats component lives on the target entity; we can get it from the archetype)
    Entity* target = nullptr;
    world.forEach<EnemyStatsComponent>([&](Entity* e, EnemyStatsComponent* esc) {
        if (esc == mobStats) target = e;
    });

  try {
    // Award XP to the killer (scaled by level gap)
    if (killerStats) {
        int xp = XPCalculator::calculateXPReward(
            es.xpReward, es.level, killerStats->stats.level);
        // Party XP bonus
        auto* partyComp = killer->getComponent<PartyComponent>();
        if (partyComp && partyComp->party.isInParty()) {
            xp = static_cast<int>(xp * (1.0f + partyComp->party.getXPBonus()));
        }
        // Aurora ExpGainUp buff
        auto* seComp = killer->getComponent<StatusEffectComponent>();
        if (seComp) {
            float expBonus = seComp->effects.getExpGainBonus();
            if (expBonus > 0.0f) xp = static_cast<int>(xp * (1.0f + expBonus));
        }
        if (xp > 0) {
            // WAL: record XP gain before mutating
            if (client) wal_.appendXPGain(client->character_id, static_cast<int64_t>(xp));
            killerStats->stats.addXP(xp);
            playerDirty_[killerClientId].stats = true;
            enqueuePersist(killerClientId, PersistPriority::HIGH, PersistType::Character);
            LOG_INFO("Server", "Client %d gained %d XP from '%s' (base=%d, mob Lv%d, player Lv%d)",
                     killerClientId, xp, es.enemyName.c_str(), es.xpReward,
                     es.level, killerStats->stats.level);

            // Pet XP sharing (50%)
            auto* petComp = killer->getComponent<PetComponent>();
            if (petComp && petComp->hasPet()) {
                const auto* petDef = petDefCache_.getDefinition(petComp->equippedPet.petDefinitionId);
                if (petDef) {
                    int64_t petXP = static_cast<int64_t>(xp * PetSystem::PET_XP_SHARE);
                    if (petXP > 0) {
                        int levelBefore = petComp->equippedPet.level;
                        PetSystem::addXP(*petDef, petComp->equippedPet, petXP,
                                         killerStats->stats.level);
                        playerDirty_[killerClientId].pet = true;
                        if (petComp->equippedPet.level != levelBefore) {
                            recalcEquipmentBonuses(killer);
                        }
                        sendPetUpdate(killerClientId, killer);
                    }
                }
            }
        } else {
            LOG_INFO("Server", "Client %d: trivial mob '%s' Lv%d — no XP (player Lv%d)",
                     killerClientId, es.enemyName.c_str(), es.level, killerStats->stats.level);
        }
    }

    // Dungeon honor: +1 per mob, +50 for MiniBoss, to all party members in instance
    {
        uint32_t dungeonInstId = dungeonManager_.getInstanceForClient(killerClientId);
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
    // Threat entries for disconnected/scene-changed players already purged at disconnect/transition time
    // Uses current party state — disbanded parties lose their grouped advantage
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
    if (baseOwner == 0) baseOwner = killerHandle.value;

    std::string killScene;
    if (killerStats) killScene = killerStats->stats.currentScene;
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
                goldComp->ownerEntityId = pickOwner();
                goldComp->spawnTime = gameTime_;
                goldComp->sceneId = es.sceneId;
            }

            PersistentId goldPid = PersistentId::generate(1);
            repl.registerEntity(goldEntity->handle(), goldPid);
        }
    }

    // Hide mob sprite (SpawnSystem handles respawn)
    if (target) {
        auto* mobSprite = target->getComponent<SpriteComponent>();
        if (mobSprite) mobSprite->enabled = false;
    }

    // Notify gauntlet of mob kill
    if (gauntletManager_.isPlayerInActiveInstance(static_cast<uint32_t>(killerClientId))) {
        auto* inst = gauntletManager_.getInstanceForPlayer(static_cast<uint32_t>(killerClientId));
        if (inst) {
            bool isBoss = (es.monsterType == "Boss" || es.monsterType == "RaidBoss");
            gauntletManager_.notifyMobKill(static_cast<uint32_t>(killerClientId),
                                            es.level, isBoss, inst->divisionId);
        }
    }

    LOG_INFO("Server", "Client %d killed mob '%s'", killerClientId, es.enemyName.c_str());
  } catch (const std::exception& ex) {
    LOG_ERROR("Server", "CRASH in mob-death path for client %d: %s", killerClientId, ex.what());
  } catch (...) {
    LOG_ERROR("Server", "CRASH (unknown) in mob-death path for client %d", killerClientId);
  }
}

} // namespace fate
