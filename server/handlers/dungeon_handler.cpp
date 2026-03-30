#include "server/server_app.h"
#include "server/server_spawn_manager.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/entity_factory.h"
#include "game/components/game_components.h"
#include "game/components/spawn_point_component.h"
#include "game/components/pet_component.h"
#include "game/shared/game_types.h"
#include "game/shared/item_stat_roller.h"
#include "game/systems/mob_ai_system.h"
#include "engine/net/game_messages.h"

namespace fate {

void ServerApp::tickDungeonInstances(float dt) {
    dungeonManager_.tick(dt, gameTime_);

    // Tick decline cooldowns
    for (auto it = dungeonDeclineCooldowns_.begin(); it != dungeonDeclineCooldowns_.end(); ) {
        it->second -= dt;
        if (it->second <= 0.0f) {
            it = dungeonDeclineCooldowns_.erase(it);
        } else {
            ++it;
        }
    }

    // Tick replication for each active instance
    for (auto& [id, inst] : dungeonManager_.allInstances()) {
        if (!inst->expired) {
            inst->replication.update(inst->world, server_);
        }
    }

    // Per-minute time remaining broadcast
    for (auto& [id, inst] : dungeonManager_.allInstances()) {
        if (inst->expired || inst->pendingAccepts.size() > 0) continue;
        if (inst->playerClientIds.empty()) continue;

        int elapsedMinutes = static_cast<int>(inst->elapsedTime / 60.0f);
        if (elapsedMinutes > inst->lastMinuteBroadcast) {
            inst->lastMinuteBroadcast = elapsedMinutes;
            int remainingSec = static_cast<int>(inst->timeLimitSeconds - inst->elapsedTime);
            int remainingMin = (remainingSec + 59) / 60;  // round up
            if (remainingMin <= 0) continue;

            std::string timeMsg = "Dungeon time remaining: " + std::to_string(remainingMin) + " minute" +
                                  (remainingMin != 1 ? "s" : "");

            SvChatMessageMsg chat;
            chat.channel = 0;  // system channel
            chat.senderName = "[System]";
            chat.message = timeMsg;
            uint8_t buf[256];
            ByteWriter w(buf, sizeof(buf));
            chat.write(w);

            for (uint16_t cid : inst->playerClientIds) {
                server_.sendTo(cid, Channel::ReliableOrdered, PacketType::SvChatMessage, buf, w.size());
            }
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

        // Collection hook: dungeon completion
        checkPlayerCollections(cid, "CompleteDungeon");

        LOG_INFO("Server", "Dungeon rewards for client %d: %lld gold, 50 honor, treasure box '%s'",
                 cid, static_cast<long long>(goldReward), treasureBoxId.c_str());
    }
}

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

        auto* conn = server_.connections().findById(cid);
        if (!conn) continue;

        // Clear event lock BEFORE transfer (keyed on current dungeon entity ID)
        playerEventLocks_.erase(static_cast<uint32_t>(conn->playerEntityId));

        // Get return point
        Vec2 returnPos = {0.0f, 0.0f};
        std::string returnScene = "WhisperingWoods";
        auto it = inst->returnPoints.find(cid);
        if (it != inst->returnPoints.end()) {
            returnPos = {it->second.x, it->second.y};
            returnScene = it->second.scene;
        }

        // Transfer back to overworld (generates new entity ID)
        transferPlayerToWorld(cid, inst->world, inst->replication, world_, replication_, returnPos, returnScene);

        dungeonManager_.removePlayer(instanceId, cid);
    }

    dungeonManager_.destroyInstance(instanceId);
}

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

    // Check decline cooldown
    if (dungeonDeclineCooldowns_.count(partyComp->party.partyId) &&
        dungeonDeclineCooldowns_[partyComp->party.partyId] > 0.0f) {
        LOG_WARN("Server", "Client %d: dungeon cooldown active for party %d",
                 clientId, partyComp->party.partyId);
        return;
    }

    // H9: Validate all party members are online before starting dungeon
    for (const auto& member : partyComp->party.members) {
        if (member.characterId == conn->character_id) continue; // skip leader
        uint16_t memberCid = 0;
        server_.connections().forEach([&](const ClientConnection& c) {
            if (c.character_id == member.characterId) memberCid = c.clientId;
        });
        if (memberCid == 0) {
            LOG_WARN("Server", "Dungeon start rejected: party member '%s' is offline",
                     member.characterName.c_str());
            return;
        }
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
        if (memberConn && (playerEventLocks_.count(static_cast<uint32_t>(memberConn->playerEntityId))
            || dungeonManager_.getInstanceForClient(memberCid))) {
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
        dungeonDeclineCooldowns_[inst->partyId] = 10.0f;
        dungeonManager_.destroyInstance(instId);
    }
}

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

        // Consume ticket
        consumeDungeonTicket(conn->character_id);

        // Transfer to instance world (generates new entity ID)
        transferPlayerToWorld(cid, world_, replication_, inst->world, inst->replication, spawnPos, inst->sceneId);

        // Event lock — set AFTER transfer so key matches the new entity ID
        playerEventLocks_[static_cast<uint32_t>(conn->playerEntityId)] = "Dungeon";

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
            int level = def->minSpawnLevel;
            if (def->maxSpawnLevel > def->minSpawnLevel) {
                std::uniform_int_distribution<int> levelDist(def->minSpawnLevel, def->maxSpawnLevel);
                level = levelDist(s_rng);
            }

            std::uniform_real_distribution<float> xDist(zone.centerX - zone.radius, zone.centerX + zone.radius);
            std::uniform_real_distribution<float> yDist(zone.centerY - zone.radius, zone.centerY + zone.radius);
            Vec2 pos = {xDist(s_rng), yDist(s_rng)};

            MobCreateParams params;
            params.def = def;
            params.position = pos;
            params.level = level;
            params.sceneId = inst->sceneId;
            params.zoneRadius = zone.radius;

            ServerSpawnManager::createMobEntity(inst->world, inst->replication, params);
        }
    }

    LOG_INFO("Server", "Spawned dungeon mobs for instance %u, scene '%s'",
             inst->instanceId, inst->sceneId.c_str());
}

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
