#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "game/shared/collection_system.h"
#include "game/shared/game_types.h"
#include "engine/net/game_messages.h"

namespace fate {

void ServerApp::checkPlayerCollections(uint16_t clientId, const std::string& triggerType) {
    auto* conn = server_.connections().findById(clientId);
    if (!conn || conn->playerEntityId == 0) return;

    PersistentId pid(conn->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* entity = getWorldForClient(clientId).getEntity(h);
    if (!entity) return;

    auto* collComp = entity->getComponent<CollectionComponent>();
    auto* statsComp = entity->getComponent<CharacterStatsComponent>();
    if (!collComp || !statsComp) return;

    // Build player state snapshot for evaluation
    PlayerCollectionState state;
    state.level = statsComp->stats.level;
    state.totalMobKills = statsComp->stats.totalMobKills;

    auto* invComp = entity->getComponent<InventoryComponent>();
    if (invComp) {
        // Count items by rarity and find max enchant
        for (const auto& slot : invComp->inventory.getSlots()) {
            if (!slot.isValid()) continue;
            if (slot.rarity == ItemRarity::Uncommon)  state.uncommonItems++;
            else if (slot.rarity == ItemRarity::Rare)      state.rareItems++;
            else if (slot.rarity == ItemRarity::Epic)      state.epicItems++;
            else if (slot.rarity == ItemRarity::Legendary) state.legendaryItems++;
            if (slot.enchantLevel > state.maxEnchantLevel)
                state.maxEnchantLevel = slot.enchantLevel;
            state.ownedItemIds.insert(slot.itemId);
        }
        // Also check equipped items
        for (const auto& [slot, eq] : invComp->inventory.getEquipmentMap()) {
            if (!eq.isValid()) continue;
            if (eq.rarity == ItemRarity::Uncommon)  state.uncommonItems++;
            else if (eq.rarity == ItemRarity::Rare)      state.rareItems++;
            else if (eq.rarity == ItemRarity::Epic)      state.epicItems++;
            else if (eq.rarity == ItemRarity::Legendary) state.legendaryItems++;
            if (eq.enchantLevel > state.maxEnchantLevel)
                state.maxEnchantLevel = eq.enchantLevel;
            state.ownedItemIds.insert(eq.itemId);
        }
    }

    auto* guildComp = entity->getComponent<GuildComponent>();
    state.inGuild = guildComp && guildComp->guild.isInGuild();

    auto* skillComp = entity->getComponent<SkillManagerComponent>();
    if (skillComp) state.learnedSkills = static_cast<int>(skillComp->skills.getLearnedSkills().size());

    // Check relevant definitions
    auto defs = collectionCache_.getByConditionType(triggerType);
    bool anyNew = false;
    for (const auto* def : defs) {
        if (collComp->collections.isCompleted(def->collectionId)) continue;
        if (evaluateCollectionCondition(*def, state)) {
            collComp->collections.markCompleted(def->collectionId);
            collectionRepo_->saveCompletedCollection(conn->character_id, def->collectionId);
            anyNew = true;

            // Costume reward: grant via costumeRepo instead of stat bonus
            if (def->rewardType == "Costume" && !def->conditionTarget.empty()) {
                if (costumeRepo_->grantCostume(conn->character_id, def->conditionTarget)) {
                    auto* costumeComp = entity->getComponent<CostumeComponent>();
                    if (costumeComp) costumeComp->ownedCostumes.insert(def->conditionTarget);

                    SvCostumeUpdateMsg update;
                    update.updateType   = 0; // obtained
                    update.costumeDefId = def->conditionTarget;
                    update.slotType     = 0;
                    update.show         = (costumeComp && costumeComp->showCostumes) ? 1 : 0;
                    uint8_t costBuf[256]; ByteWriter costW(costBuf, sizeof(costBuf));
                    update.write(costW);
                    server_.sendTo(clientId, Channel::ReliableOrdered,
                                   PacketType::SvCostumeUpdate, costBuf, costW.size());
                }
            }

            // Send chat notification
            std::string chatMsg = "[Collection] Completed: " + def->name +
                (def->rewardType == "Costume"
                    ? " (Costume: " + def->conditionTarget + ")"
                    : " (+" + std::to_string(static_cast<int>(def->rewardValue)) + " " + def->rewardType + ")");
            SvChatMessageMsg sysMsg;
            sysMsg.channel    = static_cast<uint8_t>(ChatChannel::System);
            sysMsg.senderName = "[Collection]";
            sysMsg.message    = chatMsg;
            sysMsg.faction    = 0;
            uint8_t chatBuf[512]; ByteWriter chatW(chatBuf, sizeof(chatBuf));
            sysMsg.write(chatW);
            server_.sendTo(clientId, Channel::ReliableOrdered,
                           PacketType::SvChatMessage, chatBuf, chatW.size());

            LOG_INFO("Server", "Client %d completed collection '%s' (id=%u)",
                     clientId, def->name.c_str(), def->collectionId);
        }
    }

    if (anyNew) {
        collComp->collections.recalculateBonuses(collectionCache_.all());
        statsComp->stats.collectionBonusSTR = collComp->collections.bonuses.bonusSTR;
        statsComp->stats.collectionBonusINT = collComp->collections.bonuses.bonusINT;
        statsComp->stats.collectionBonusDEX = collComp->collections.bonuses.bonusDEX;
        statsComp->stats.collectionBonusCON = collComp->collections.bonuses.bonusCON;
        statsComp->stats.collectionBonusWIS = collComp->collections.bonuses.bonusWIS;
        statsComp->stats.collectionBonusHP  = collComp->collections.bonuses.bonusMaxHP;
        statsComp->stats.collectionBonusMP  = collComp->collections.bonuses.bonusMaxMP;
        statsComp->stats.collectionBonusDamage = collComp->collections.bonuses.bonusDamage;
        statsComp->stats.collectionBonusArmor  = collComp->collections.bonuses.bonusArmor;
        statsComp->stats.collectionBonusCritRate = collComp->collections.bonuses.bonusCritRate;
        statsComp->stats.collectionBonusMoveSpeed = collComp->collections.bonuses.bonusMoveSpeed;
        statsComp->stats.recalculateStats();
        sendPlayerState(clientId);
        sendCollectionSync(clientId);
    }
}

void ServerApp::sendCollectionSync(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
    if (!e) return;

    auto* collComp = e->getComponent<CollectionComponent>();
    if (!collComp) return;

    SvCollectionSyncMsg msg;
    for (uint32_t id : collComp->collections.completedIds) {
        msg.completedIds.push_back(id);
    }
    const auto& b = collComp->collections.bonuses;
    msg.bonusSTR       = b.bonusSTR;
    msg.bonusINT       = b.bonusINT;
    msg.bonusDEX       = b.bonusDEX;
    msg.bonusCON       = b.bonusCON;
    msg.bonusWIS       = b.bonusWIS;
    msg.bonusHP        = b.bonusMaxHP;
    msg.bonusMP        = b.bonusMaxMP;
    msg.bonusDamage    = b.bonusDamage;
    msg.bonusArmor     = b.bonusArmor;
    msg.bonusCritRate  = b.bonusCritRate;
    msg.bonusMoveSpeed = b.bonusMoveSpeed;

    uint8_t buf[4096];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvCollectionSync, buf, w.size());
}

void ServerApp::sendCollectionDefs(uint16_t clientId) {
    SvCollectionDefsMsg msg;
    for (const auto& [id, def] : collectionCache_.all()) {
        CollectionDefEntry entry;
        entry.collectionId    = def.collectionId;
        entry.name            = def.name;
        entry.description     = def.description;
        entry.category        = def.category;
        entry.conditionType   = def.conditionType;
        entry.conditionTarget = def.conditionTarget;
        entry.conditionValue  = def.conditionValue;
        entry.rewardType      = def.rewardType;
        entry.rewardValue     = def.rewardValue;
        msg.defs.push_back(std::move(entry));
    }

    uint8_t buf[4096];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvCollectionDefs, buf, w.size());
    LOG_INFO("Server", "Sent %d collection defs to client %d",
             static_cast<int>(msg.defs.size()), clientId);
}

} // namespace fate
