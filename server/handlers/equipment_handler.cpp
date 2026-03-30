#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "game/components/pet_component.h"
#include "game/shared/game_types.h"
#include "engine/net/game_messages.h"

namespace fate {

void ServerApp::recalcEquipmentBonuses(Entity* player) {
    auto* charStats = player->getComponent<CharacterStatsComponent>();
    auto* inv = player->getComponent<InventoryComponent>();
    if (!charStats || !inv) return;

    charStats->stats.clearEquipmentBonuses();

    for (const auto& [slot, item] : inv->inventory.getEquipmentMap()) {
        if (!item.isValid()) continue;

        // Look up base stats from item definition
        const auto* def = itemDefCache_.getDefinition(item.itemId);
        int baseWeaponMin = 0, baseWeaponMax = 0, baseArmor = 0;
        float baseAttackSpeed = 0.0f;
        if (def) {
            baseWeaponMin = def->damageMin;
            baseWeaponMax = def->damageMax;
            baseArmor = def->armor;
            baseAttackSpeed = def->getFloatAttribute("attack_speed", 0.0f);
        }

        charStats->stats.applyItemBonuses(item, baseWeaponMin, baseWeaponMax,
                                           baseArmor, baseAttackSpeed);
    }

    // Apply pet bonuses (after equipment, before recalculating stats)
    auto* petComp = player->getComponent<PetComponent>();
    if (petComp && petComp->hasPet()) {
        const auto* petDef = petDefCache_.getDefinition(petComp->equippedPet.petDefinitionId);
        if (petDef) {
            PetSystem::applyToEquipBonuses(*petDef, petComp->equippedPet,
                                           charStats->stats.equipBonusHP,
                                           charStats->stats.equipBonusCritRate);
        }
    }

    charStats->stats.recalculateStats();

    // Clamp HP/MP to new max (unequipping can lower maxHP below currentHP)
    if (charStats->stats.currentHP > charStats->stats.maxHP)
        charStats->stats.currentHP = charStats->stats.maxHP;
    if (charStats->stats.currentMP > charStats->stats.maxMP)
        charStats->stats.currentMP = charStats->stats.maxMP;
}

void ServerApp::processEquip(uint16_t clientId, const CmdEquipMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* charStats = player->getComponent<CharacterStatsComponent>();
    auto* inv = player->getComponent<InventoryComponent>();
    if (!charStats || !inv) return;

    // Block equipment changes during combat
    if (charStats->stats.isInCombat()) {
        LOG_WARN("Server", "Client %d tried to change equipment in combat", clientId);
        return;
    }

    // Block equipment changes while dead or dying
    if (!charStats->stats.isAlive()) return;

    // Block equipment changes while casting
    if (charStats->stats.isCasting()) {
        LOG_WARN("Server", "Client %d tried to change equipment while casting", clientId);
        return;
    }

    // H4-FIX: Block equipment changes during an active trade session
    if (client->activeTradeSessionId != 0) {
        LOG_WARN("Server", "Client %d tried to change equipment during active trade", clientId);
        return;
    }

    if (!isValidEquipmentSlot(msg.equipSlot)) {
        LOG_WARN("Server", "Client %d sent invalid equipment slot %d", clientId, msg.equipSlot);
        return;
    }
    auto targetSlot = static_cast<EquipmentSlot>(msg.equipSlot);

    // Validate class, level, and slot-type requirements before equipping
    if (msg.action == 0) {
        auto item = inv->inventory.getSlot(msg.inventorySlot);
        if (item.isValid()) {
            const auto* itemDef = itemDefCache_.getDefinition(item.itemId);
            if (itemDef) {
                // Class restriction
                if (!itemDef->classReq.empty() && itemDef->classReq != "All" &&
                    charStats->stats.classDef.displayName != itemDef->classReq) {
                    LOG_WARN("Server", "Client %d class %s cannot equip %s (requires %s)",
                             clientId, charStats->stats.classDef.displayName.c_str(),
                             item.itemId.c_str(), itemDef->classReq.c_str());
                    return;
                }
                // Level restriction
                if (itemDef->levelReq > charStats->stats.level) {
                    LOG_WARN("Server", "Client %d level %d too low for %s (requires %d)",
                             clientId, charStats->stats.level,
                             item.itemId.c_str(), itemDef->levelReq);
                    return;
                }
                // Slot-type restriction: item subtype must match target equipment slot
                if (!itemDef->isEquipment()) {
                    LOG_WARN("Server", "Client %d tried to equip non-equipment '%s' (type=%s)",
                             clientId, item.itemId.c_str(), itemDef->itemType.c_str());
                    return;
                }
                auto allowedSlot = [](const std::string& subtype) -> EquipmentSlot {
                    if (subtype == "Sword" || subtype == "Wand" || subtype == "Bow" ||
                        subtype == "Staff" || subtype == "Dagger" || subtype == "Axe" ||
                        subtype == "Mace" || subtype == "Spear")
                        return EquipmentSlot::Weapon;
                    if (subtype == "Shield" || subtype == "Quiver" || subtype == "Orb")
                        return EquipmentSlot::SubWeapon;
                    if (subtype == "Head" || subtype == "Hat" || subtype == "Helm" || subtype == "Hood" || subtype == "Crown")
                        return EquipmentSlot::Hat;
                    if (subtype == "Armor" || subtype == "Chest" || subtype == "Robe" || subtype == "Tunic" || subtype == "Plate")
                        return EquipmentSlot::Armor;
                    if (subtype == "Gloves" || subtype == "Gauntlets" || subtype == "Bracers")
                        return EquipmentSlot::Gloves;
                    if (subtype == "Shoes" || subtype == "Boots" || subtype == "Sandals")
                        return EquipmentSlot::Shoes;
                    if (subtype == "Belt" || subtype == "Sash")
                        return EquipmentSlot::Belt;
                    if (subtype == "Cloak" || subtype == "Cape" || subtype == "Mantle")
                        return EquipmentSlot::Cloak;
                    if (subtype == "Ring")
                        return EquipmentSlot::Ring;
                    if (subtype == "Necklace" || subtype == "Amulet" || subtype == "Pendant")
                        return EquipmentSlot::Necklace;
                    return EquipmentSlot::None;
                };
                EquipmentSlot required = allowedSlot(itemDef->subtype);
                // Auto-resolve: if client sent None, use the item's required slot
                if (targetSlot == EquipmentSlot::None && required != EquipmentSlot::None) {
                    targetSlot = required;
                }
                if (required != EquipmentSlot::None && required != targetSlot) {
                    LOG_WARN("Server", "Client %d tried to equip '%s' (subtype=%s) in wrong slot %d (needs %d)",
                             clientId, item.itemId.c_str(), itemDef->subtype.c_str(),
                             static_cast<int>(targetSlot), static_cast<int>(required));
                    return;
                }
            }
        }
    }

    bool success = false;

    if (msg.action == 0) {
        success = inv->inventory.equipItem(msg.inventorySlot, targetSlot);
    } else {
        success = inv->inventory.unequipItem(targetSlot);
    }

    if (success) {
        recalcEquipmentBonuses(player);
        playerDirty_[clientId].vitals = true;
        playerDirty_[clientId].inventory = true;
        playerDirty_[clientId].stats = true;
        enqueuePersist(clientId, PersistPriority::NORMAL, PersistType::Inventory);
        sendPlayerState(clientId);
        sendInventorySync(clientId);
        LOG_INFO("Server", "Client %d %s slot %d",
                 clientId, msg.action == 0 ? "equipped" : "unequipped", msg.equipSlot);
    }
}

} // namespace fate
