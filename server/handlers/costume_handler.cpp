#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "game/shared/game_types.h"
#include "engine/net/game_messages.h"

namespace fate {

// ---------------------------------------------------------------------------
// processEquipCostume — equip a costume the player owns into the matching slot
// ---------------------------------------------------------------------------
void ServerApp::processEquipCostume(uint16_t clientId, const CmdEquipCostumeMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* costumeComp = player->getComponent<CostumeComponent>();
    if (!costumeComp) return;

    // Validate ownership
    if (costumeComp->ownedCostumes.find(msg.costumeDefId) == costumeComp->ownedCostumes.end()) {
        LOG_WARN("Server", "Client %d tried to equip unowned costume '%s'",
                 clientId, msg.costumeDefId.c_str());
        return;
    }

    // Validate costume definition exists in cache
    const auto* costumeDef = costumeCache_.get(msg.costumeDefId);
    if (!costumeDef) {
        LOG_WARN("Server", "Client %d tried to equip unknown costume '%s'",
                 clientId, msg.costumeDefId.c_str());
        return;
    }

    uint8_t slotType = costumeDef->slotType;

    // Update in-memory state
    costumeComp->equippedBySlot[slotType] = msg.costumeDefId;

    // Persist to database
    costumeRepo_->equipCostume(client->character_id, slotType, msg.costumeDefId);

    // Send incremental update to client (type 1 = equipped)
    SvCostumeUpdateMsg update;
    update.updateType   = 1;
    update.costumeDefId = msg.costumeDefId;
    update.slotType     = slotType;
    update.show         = costumeComp->showCostumes ? 1 : 0;

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    update.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvCostumeUpdate, buf, w.size());

    // Mark entity dirty so replication picks up the costumeVisuals change
    playerDirty_[clientId].stats = true;

    LOG_INFO("Server", "Client %d equipped costume '%s' in slot %d",
             clientId, msg.costumeDefId.c_str(), slotType);
}

// ---------------------------------------------------------------------------
// processUnequipCostume — remove the costume from a given slot
// ---------------------------------------------------------------------------
void ServerApp::processUnequipCostume(uint16_t clientId, const CmdUnequipCostumeMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* costumeComp = player->getComponent<CostumeComponent>();
    if (!costumeComp) return;

    // Validate slot type
    if (!isValidEquipmentSlot(msg.slotType)) {
        LOG_WARN("Server", "Client %d sent invalid costume slot type %d", clientId, msg.slotType);
        return;
    }

    // Check if there's actually something equipped in this slot
    auto it = costumeComp->equippedBySlot.find(msg.slotType);
    if (it == costumeComp->equippedBySlot.end()) {
        LOG_WARN("Server", "Client %d tried to unequip empty costume slot %d",
                 clientId, msg.slotType);
        return;
    }

    std::string removedId = it->second;
    costumeComp->equippedBySlot.erase(it);

    // Persist to database
    costumeRepo_->unequipCostume(client->character_id, msg.slotType);

    // Send incremental update to client (type 2 = unequipped)
    SvCostumeUpdateMsg update;
    update.updateType   = 2;
    update.costumeDefId = removedId;
    update.slotType     = msg.slotType;
    update.show         = costumeComp->showCostumes ? 1 : 0;

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    update.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvCostumeUpdate, buf, w.size());

    // Mark entity dirty so replication picks up the costumeVisuals change
    playerDirty_[clientId].stats = true;

    LOG_INFO("Server", "Client %d unequipped costume from slot %d (was '%s')",
             clientId, msg.slotType, removedId.c_str());
}

// ---------------------------------------------------------------------------
// processToggleCostumes — show/hide all costumes (master toggle)
// ---------------------------------------------------------------------------
void ServerApp::processToggleCostumes(uint16_t clientId, const CmdToggleCostumesMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* costumeComp = player->getComponent<CostumeComponent>();
    if (!costumeComp) return;

    bool show = (msg.show != 0);
    costumeComp->showCostumes = show;

    // Persist to database
    costumeRepo_->saveToggleState(client->character_id, show);

    // Send incremental update to client (type 3 = toggleChanged)
    SvCostumeUpdateMsg update;
    update.updateType   = 3;
    update.costumeDefId = "";
    update.slotType     = 0;
    update.show         = show ? 1 : 0;

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    update.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvCostumeUpdate, buf, w.size());

    // Mark entity dirty so replication picks up the costumeVisuals change
    playerDirty_[clientId].stats = true;

    LOG_INFO("Server", "Client %d toggled costumes %s", clientId, show ? "ON" : "OFF");
}

// ---------------------------------------------------------------------------
// sendCostumeSync — full costume state sync (owned + equipped + toggle)
// ---------------------------------------------------------------------------
void ServerApp::sendCostumeSync(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* costumeComp = player->getComponent<CostumeComponent>();
    if (!costumeComp) return;

    SvCostumeSyncMsg syncMsg;
    syncMsg.showCostumes = costumeComp->showCostumes ? 1 : 0;

    // Owned costumes
    syncMsg.ownedCostumeIds.reserve(costumeComp->ownedCostumes.size());
    for (const auto& id : costumeComp->ownedCostumes) {
        syncMsg.ownedCostumeIds.push_back(id);
    }

    // Equipped costumes
    syncMsg.equipped.reserve(costumeComp->equippedBySlot.size());
    for (const auto& [slot, id] : costumeComp->equippedBySlot) {
        syncMsg.equipped.push_back({slot, id});
    }

    uint8_t buf[4096];
    ByteWriter w(buf, sizeof(buf));
    syncMsg.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvCostumeSync, buf, w.size());

    LOG_INFO("Server", "Client %d costume sync: %zu owned, %zu equipped, show=%d",
             clientId, costumeComp->ownedCostumes.size(),
             costumeComp->equippedBySlot.size(),
             costumeComp->showCostumes ? 1 : 0);
}

// ---------------------------------------------------------------------------
// loadPlayerCostumes — load all costume data from DB into CostumeComponent
// ---------------------------------------------------------------------------
void ServerApp::loadPlayerCostumes(uint16_t clientId, const std::string& characterId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* player = getWorldForClient(clientId).getEntity(h);
    if (!player) return;

    auto* costumeComp = player->getComponent<CostumeComponent>();
    if (!costumeComp) return;

    // Load owned costumes
    auto owned = costumeRepo_->loadOwnedCostumes(characterId);
    costumeComp->ownedCostumes.clear();
    for (const auto& oc : owned) {
        costumeComp->ownedCostumes.insert(oc.costumeDefId);
    }

    // Load equipped costumes
    auto equipped = costumeRepo_->loadEquippedCostumes(characterId);
    costumeComp->equippedBySlot.clear();
    for (const auto& ec : equipped) {
        costumeComp->equippedBySlot[ec.slotType] = ec.costumeDefId;
    }

    // Load toggle state
    costumeComp->showCostumes = costumeRepo_->loadToggleState(characterId);

    // Send full sync to client
    sendCostumeSync(clientId);

    LOG_INFO("Server", "Loaded costumes for client %d (char %s): %zu owned, %zu equipped",
             clientId, characterId.c_str(),
             costumeComp->ownedCostumes.size(),
             costumeComp->equippedBySlot.size());
}

} // namespace fate
