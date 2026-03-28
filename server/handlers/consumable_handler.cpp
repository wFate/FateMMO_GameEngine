#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include "game/shared/game_types.h"
#include "engine/net/game_messages.h"

namespace fate {

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
        } else if (subtype == "SkillBook" || subtype == "skillbook") {
            // ---- SkillBook: teach the player a new skill/rank ----
            std::string bookSkillId = def->getStringAttribute("skill_id", "");
            int bookRank = def->getIntAttribute("rank", 1);

            if (bookSkillId.empty()) {
                sendResult(false, "Invalid skillbook (no skill_id)");
                return;
            }

            auto* skillComp = player->getComponent<SkillManagerComponent>();
            if (!skillComp) {
                sendResult(false, "Cannot learn skills");
                return;
            }

            // Validate class restriction using skill definition cache
            const CachedSkillDef* skillDef = skillDefCache_.getSkill(bookSkillId);
            if (skillDef && !skillDef->classRequired.empty() && skillDef->classRequired != "Any") {
                if (charStats->stats.className != skillDef->classRequired) {
                    sendResult(false, "Wrong class for this skill");
                    return;
                }
            }

            // Validate level requirement
            if (skillDef && charStats->stats.level < skillDef->levelRequired) {
                sendResult(false, "Level too low (requires " + std::to_string(skillDef->levelRequired) + ")");
                return;
            }

            // Attempt to learn
            bool learned = skillComp->skills.learnSkill(bookSkillId, bookRank);
            if (!learned) {
                const LearnedSkill* existing = skillComp->skills.getLearnedSkill(bookSkillId);
                if (existing && existing->unlockedRank >= bookRank) {
                    sendResult(false, "Already learned this rank");
                } else if (existing) {
                    sendResult(false, "Must learn previous rank first");
                } else if (bookRank > 1) {
                    sendResult(false, "Must learn rank I first");
                } else {
                    sendResult(false, "Cannot learn this skill");
                }
                return;
            }

            // Consume the skillbook
            inv->inventory.removeItemQuantity(slotIndex, 1);

            // Persist
            wal_.appendItemRemove(client->character_id, slotIndex);
            playerDirty_[clientId].inventory = true;
            playerDirty_[clientId].skills = true;
            enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);
            enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Skills);

            // Re-sync client
            sendResult(true, "Learned " + (skillDef ? skillDef->skillName : bookSkillId) + " rank " + std::to_string(bookRank));
            sendSkillSync(clientId);
            sendInventorySync(clientId);

            LOG_INFO("Server", "Client %d learned skill '%s' rank %d from skillbook",
                     clientId, bookSkillId.c_str(), bookRank);
            return;
        } else if (subtype == "StatReset" || subtype == "stat_reset") {
            // ---- StatReset (Oblivion Potion) — reset all activated skill ranks ----
            auto* skillComp = player->getComponent<SkillManagerComponent>();
            if (!skillComp) {
                sendResult(false, "Cannot reset skills");
                return;
            }

            int totalSpent = skillComp->skills.spentPoints();
            if (totalSpent == 0) {
                sendResult(false, "You have no skill points to reset.");
                return;
            }

            // Reset all activated ranks to 0, refund all spent points
            skillComp->skills.resetAllSkillRanks();

            // Consume the item
            inv->inventory.removeItemQuantity(slotIndex, 1);

            // WAL + persist
            wal_.appendItemRemove(client->character_id, slotIndex);
            playerDirty_[clientId].skills = true;
            playerDirty_[clientId].inventory = true;
            enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);
            enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Skills);

            // Re-sync client
            sendResult(true, "All skill points have been reset.");
            sendSkillSync(clientId);
            sendInventorySync(clientId);

            LOG_INFO("Server", "Client %d used Oblivion Potion — reset %d skill points",
                     clientId, totalSpent);
            return;
        } else if (subtype == "TownRecall" || subtype == "town_recall") {
            // ---- TownRecall (Recall Scroll) — teleport to Town spawn ----

            // Block if player is in combat
            if (charStats->stats.isInCombat()) {
                sendResult(false, "Cannot recall while in combat.");
                return;
            }

            // Block if player is in an arena match
            uint32_t entityId = static_cast<uint32_t>(client->playerEntityId);
            if (arenaManager_.isPlayerInMatch(entityId)) {
                sendResult(false, "Cannot recall during an arena match.");
                return;
            }

            // Block if player is in a battlefield
            if (battlefieldManager_.isPlayerRegistered(entityId)) {
                sendResult(false, "Cannot recall during a battlefield.");
                return;
            }

            // Block if player is in a dungeon instance
            if (dungeonManager_.getInstanceForClient(clientId) != 0) {
                sendResult(false, "Cannot recall inside a dungeon.");
                return;
            }

            // Determine recall target from player's recallScene (default "Town")
            const std::string targetScene =
                charStats->stats.recallScene.empty() ? "Town" : charStats->stats.recallScene;

            // Block if already in the recall scene
            std::string previousScene = charStats->stats.currentScene;
            if (previousScene == targetScene) {
                sendResult(false, "You are already in town.");
                return;
            }

            // Validate recall scene exists
            const SceneInfoRecord* townScene = sceneCache_.get(targetScene);
            if (!townScene) {
                sendResult(false, "Town scene not found.");
                return;
            }

            // Consume the item
            inv->inventory.removeItemQuantity(slotIndex, 1);

            // WAL + persist
            wal_.appendItemRemove(client->character_id, slotIndex);
            playerDirty_[clientId].inventory = true;
            enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

            // Cancel any active trade before transitioning
            if (client->activeTradeSessionId != 0) {
                tradeRepo_->cancelSession(client->activeTradeSessionId);
                std::string otherCharId = client->tradePartnerCharId;
                server_.connections().forEach([&](ClientConnection& c) {
                    if (c.character_id == otherCharId) {
                        c.activeTradeSessionId = 0;
                        c.tradePartnerCharId.clear();
                        SvTradeUpdateMsg cancelMsg;
                        cancelMsg.updateType = 6; // cancelled
                        cancelMsg.resultCode = 10; // partner zoned
                        cancelMsg.otherPlayerName = "Trade cancelled — other player recalled to town";
                        uint8_t tbuf[256]; ByteWriter tw(tbuf, sizeof(tbuf));
                        cancelMsg.write(tw);
                        server_.sendTo(c.clientId, Channel::ReliableOrdered,
                                       PacketType::SvTradeUpdate, tbuf, tw.size());
                    }
                });
                client->activeTradeSessionId = 0;
                client->tradePartnerCharId.clear();
            }

            // Resolve Town spawn position from scene defaults (no portal needed)
            float spawnX = townScene->defaultSpawnX;
            float spawnY = townScene->defaultSpawnY;

            // Send SvZoneTransition to client
            {
                SvZoneTransitionMsg resp;
                resp.targetScene = targetScene;
                resp.spawnX = spawnX;
                resp.spawnY = spawnY;
                uint8_t zbuf[256]; ByteWriter zw(zbuf, sizeof(zbuf));
                resp.write(zw);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvZoneTransition, zbuf, zw.size());
            }

            // Update entity scene + clear combat timer
            charStats->stats.currentScene = targetScene;
            charStats->stats.combatTimer = 0.0f;

            // Purge this player's damage from all mob threat tables
            {
                World& w2 = getWorldForClient(clientId);
                w2.forEach<EnemyStatsComponent>([entityId](Entity*, EnemyStatsComponent* esc) {
                    esc->stats.damageByAttacker.erase(entityId);
                });
            }

            lastAutoAttackTime_.erase(clientId);
            playerDirty_[clientId].position = true;

            // Clear AOI state for fresh replication at new position
            client->aoi.previous.clear();
            client->aoi.current.clear();
            client->aoi.entered.clear();
            client->aoi.left.clear();
            client->aoi.stayed.clear();
            client->lastSentState.clear();

            // Remove Aurora buffs if leaving an Aurora zone
            if (isAuroraScene(previousScene) && !isAuroraScene(targetScene)) {
                removeAuroraBuffs(player);
            }

            // Update movement tracking
            lastValidPositions_[clientId] = {spawnX, spawnY};
            lastMoveTime_[clientId] = gameTime_;
            needsFirstMoveSync_.insert(clientId);

            // Save scene change to DB
            savePlayerToDBAsync(clientId);

            sendResult(true, "Recalling to town...");
            sendInventorySync(clientId);

            LOG_INFO("Server", "Client %d used Recall Scroll from '%s'",
                     clientId, previousScene.c_str());
            return;
        } else if (subtype == "fate_coin") {
            // ---- Fate Coin: 3 coins = level * 50 XP ----
            int quantity = item.quantity;
            if (quantity < 3) {
                sendResult(false, "Need at least 3 Fate Coins (have " + std::to_string(quantity) + ")");
                return;
            }

            inv->inventory.removeItemQuantity(slotIndex, 3);
            wal_.appendItemRemove(client->character_id, slotIndex);

            int64_t xpGain = static_cast<int64_t>(charStats->stats.level) * 50;
            if (client) wal_.appendXPGain(client->character_id, xpGain);
            charStats->stats.addXP(xpGain);

            playerDirty_[clientId].stats = true;
            playerDirty_[clientId].inventory = true;
            enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);
            enqueuePersist(clientId, PersistPriority::HIGH, PersistType::Character);

            char xpBuf[64];
            snprintf(xpBuf, sizeof(xpBuf), "Used 3 Fate Coins — gained %lld EXP", (long long)xpGain);
            sendResult(true, xpBuf);
            sendPlayerState(clientId);
            sendInventorySync(clientId);

            LOG_INFO("Server", "Client %d used 3 Fate Coins, gained %lld XP", clientId, (long long)xpGain);
            return;
        } else if (subtype == "exp_boost") {
            int boostPercent = def->getIntAttribute("exp_boost_percent", 10);
            int boostDuration = def->getIntAttribute("exp_boost_duration", 3600);
            float boostValue = static_cast<float>(boostPercent) / 100.0f;

            auto* seComp = player->getComponent<StatusEffectComponent>();
            if (!seComp) {
                sendResult(false, "Cannot apply buffs");
                return;
            }

            if (seComp->effects.hasEffect(EffectType::ExpGainUp)) {
                float existing = seComp->effects.getEffectValue(EffectType::ExpGainUp);
                if (std::abs(existing - boostValue) < 0.001f) {
                    sendResult(false, "Already have this boost active");
                    return;
                }
            }

            seComp->effects.applyEffect(EffectType::ExpGainUp, static_cast<float>(boostDuration), boostValue);

            inv->inventory.removeItemQuantity(slotIndex, 1);
            wal_.appendItemRemove(client->character_id, slotIndex);

            playerDirty_[clientId].inventory = true;
            enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

            char boostMsg[64];
            snprintf(boostMsg, sizeof(boostMsg), "EXP +%d%% for %d minutes", boostPercent, boostDuration / 60);
            sendResult(true, boostMsg);
            sendInventorySync(clientId);

            LOG_INFO("Server", "Client %d used EXP boost scroll (%d%% for %ds)", clientId, boostPercent, boostDuration);
            return;
        } else if (subtype == "beacon_of_calling") {
            if (msg.targetEntityId == 0) {
                sendResult(false, "Select a party member to summon");
                return;
            }

            auto* partyComp = player->getComponent<PartyComponent>();
            if (!partyComp || !partyComp->party.isInParty()) {
                sendResult(false, "You must be in a party");
                return;
            }

            uint32_t entityId = static_cast<uint32_t>(client->playerEntityId);
            if (arenaManager_.isPlayerInMatch(entityId)) {
                sendResult(false, "Cannot use during an arena match");
                return;
            }
            if (battlefieldManager_.isPlayerRegistered(entityId)) {
                sendResult(false, "Cannot use during a battlefield");
                return;
            }
            if (dungeonManager_.getInstanceForClient(clientId) != 0) {
                sendResult(false, "Cannot use inside a dungeon");
                return;
            }

            uint16_t targetClientId = 0;
            server_.connections().forEach([&](const ClientConnection& c) {
                if (c.playerEntityId == msg.targetEntityId) targetClientId = c.clientId;
            });
            if (targetClientId == 0) {
                sendResult(false, "Target player not found");
                return;
            }

            PersistentId targetPid(msg.targetEntityId);
            EntityHandle targetHandle = getReplicationForClient(targetClientId).getEntityHandle(targetPid);
            Entity* targetPlayer = getWorldForClient(targetClientId).getEntity(targetHandle);
            if (!targetPlayer) {
                sendResult(false, "Target player not found");
                return;
            }

            auto* targetPartyComp = targetPlayer->getComponent<PartyComponent>();
            if (!targetPartyComp || !targetPartyComp->party.isInParty()
                || targetPartyComp->party.partyId != partyComp->party.partyId) {
                sendResult(false, "Target is not in your party");
                return;
            }

            auto* targetStats = targetPlayer->getComponent<CharacterStatsComponent>();
            if (!targetStats || targetStats->stats.isDead) {
                sendResult(false, "Target is dead");
                return;
            }

            if (arenaManager_.isPlayerInMatch(msg.targetEntityId)) {
                sendResult(false, "Target is in an arena match");
                return;
            }
            if (battlefieldManager_.isPlayerRegistered(msg.targetEntityId)) {
                sendResult(false, "Target is in a battlefield");
                return;
            }
            if (dungeonManager_.getInstanceForClient(targetClientId) != 0) {
                sendResult(false, "Target is in a dungeon");
                return;
            }

            auto* userTransform = player->getComponent<Transform>();
            if (!userTransform) {
                sendResult(false, "Cannot determine your position");
                return;
            }
            Vec2 summonPos = userTransform->position;
            std::string summonScene = charStats->stats.currentScene;

            inv->inventory.removeItemQuantity(slotIndex, 1);
            wal_.appendItemRemove(client->character_id, slotIndex);
            playerDirty_[clientId].inventory = true;
            enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

            std::string targetPrevScene = targetStats->stats.currentScene;
            if (targetPrevScene != summonScene) {
                targetStats->stats.currentScene = summonScene;
                auto* targetConn = server_.connections().findById(targetClientId);
                if (targetConn) {
                    targetConn->aoi.previous.clear();
                    targetConn->aoi.current.clear();
                    targetConn->aoi.entered.clear();
                    targetConn->aoi.left.clear();
                    targetConn->aoi.stayed.clear();
                    targetConn->lastSentState.clear();
                }

                World& tw = getWorldForClient(targetClientId);
                tw.forEach<EnemyStatsComponent>([&](Entity*, EnemyStatsComponent* esc) {
                    esc->stats.damageByAttacker.erase(msg.targetEntityId);
                });

                if (isAuroraScene(targetPrevScene) && !isAuroraScene(summonScene)) {
                    removeAuroraBuffs(targetPlayer);
                }

                SvZoneTransitionMsg ztMsg;
                ztMsg.targetScene = summonScene;
                ztMsg.spawnX = summonPos.x;
                ztMsg.spawnY = summonPos.y;
                uint8_t zbuf[256]; ByteWriter zw(zbuf, sizeof(zbuf));
                ztMsg.write(zw);
                server_.sendTo(targetClientId, Channel::ReliableOrdered, PacketType::SvZoneTransition, zbuf, zw.size());
            } else {
                auto* targetTransform = targetPlayer->getComponent<Transform>();
                if (targetTransform) targetTransform->position = summonPos;
            }

            lastValidPositions_[targetClientId] = summonPos;
            lastMoveTime_[targetClientId] = gameTime_;
            needsFirstMoveSync_.insert(targetClientId);
            playerDirty_[targetClientId].position = true;

            sendResult(true, "Summoned " + targetStats->stats.characterName);
            sendInventorySync(clientId);

            SvConsumeResultMsg targetNotify;
            targetNotify.success = 1;
            targetNotify.message = "You have been summoned by " + charStats->stats.characterName;
            uint8_t nbuf[256]; ByteWriter nw(nbuf, sizeof(nbuf));
            targetNotify.write(nw);
            server_.sendTo(targetClientId, Channel::ReliableOrdered, PacketType::SvConsumeResult, nbuf, nw.size());

            LOG_INFO("Server", "Client %d used Beacon of Calling to summon client %d from '%s' to '%s'",
                     clientId, targetClientId, targetPrevScene.c_str(), summonScene.c_str());
            return;
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

} // namespace fate
