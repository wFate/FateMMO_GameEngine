#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "engine/net/game_messages.h"
#include "game/shared/game_types.h"

namespace fate {

void ServerApp::processBounty(uint16_t clientId, ByteReader& payload) {
    uint8_t subAction = payload.readU8();
    if (!validatePayload(payload, clientId, PacketType::CmdBounty)) return;
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    switch (subAction) {
        case BountyAction::PlaceBounty: {
            std::string targetCharId = payload.readString();
            int64_t amount = detail::readI64(payload);
            if (!validatePayload(payload, clientId, PacketType::CmdBounty)) return;

            // Validate via BountyManager logic
            int placerGuildId = guildRepo_->getPlayerGuildId(client->character_id);
            int targetGuildId = guildRepo_->getPlayerGuildId(targetCharId);
            int activeCount = bountyRepo_->getActiveBountyCount();
            bool targetHasBounty = bountyRepo_->hasActiveBounty(targetCharId);

            // H1-FIX: Validate placer has enough gold
            Entity* bountyEntity = getWorldForClient(clientId).getEntity(
                getReplicationForClient(clientId).getEntityHandle(PersistentId(client->playerEntityId)));
            auto* bountyInv = bountyEntity ? bountyEntity->getComponent<InventoryComponent>() : nullptr;
            if (!bountyInv || amount <= 0 || bountyInv->inventory.getGold() < amount) {
                SvBountyUpdateMsg errResp;
                errResp.updateType = 4;
                errResp.resultCode = static_cast<uint8_t>(BountyResult::InsufficientGold);
                errResp.message = "Not enough gold";
                uint8_t buf2[256]; ByteWriter w2(buf2, sizeof(buf2));
                errResp.write(w2);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvBountyUpdate, buf2, w2.size());
                break;
            }

            BountyResult canPlace = BountyManager::canPlaceBounty(
                client->character_id, targetCharId, amount,
                placerGuildId, targetGuildId, activeCount, targetHasBounty);

            SvBountyUpdateMsg resp;
            resp.updateType = 4; // result
            if (canPlace != BountyResult::Success) {
                resp.resultCode = static_cast<uint8_t>(canPlace);
                resp.message = BountyManager::getResultMessage(canPlace, targetCharId);
            } else {
                wal_.appendGoldChange(client->character_id, -amount);
                bountyInv->inventory.setGold(bountyInv->inventory.getGold() - amount);
                playerDirty_[clientId].inventory = true;
                enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

                BountyResult dbResult;
                bountyRepo_->placeBounty(targetCharId, targetCharId,
                                          client->character_id, "",
                                          amount, dbResult);
                resp.resultCode = static_cast<uint8_t>(dbResult);
                resp.message = BountyManager::getResultMessage(dbResult, targetCharId);
            }
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            resp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvBountyUpdate, buf, w.size());
            break;
        }
        case BountyAction::CancelBounty: {
            std::string targetCharId = payload.readString();
            if (!validatePayload(payload, clientId, PacketType::CmdBounty)) return;
            int64_t taxAmount = 0;
            BountyResult dbResult;
            int64_t refund = bountyRepo_->cancelContribution(
                targetCharId, client->character_id, taxAmount, dbResult);

            SvBountyUpdateMsg resp;
            resp.updateType = 4;
            resp.resultCode = static_cast<uint8_t>(dbResult);
            resp.message = BountyManager::getResultMessage(dbResult, targetCharId);
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            resp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvBountyUpdate, buf, w.size());

            // Refund gold if successful
            if (dbResult == BountyResult::Success && refund > 0) {
                PersistentId pid(client->playerEntityId);
                EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
                Entity* e = getWorldForClient(clientId).getEntity(h);
                if (e) {
                    auto* inv = e->getComponent<InventoryComponent>();
                    if (inv) {
                        // WAL: record gold refund before mutating
                        wal_.appendGoldChange(client->character_id, refund);
                        inv->inventory.setGold(inv->inventory.getGold() + refund);
                        playerDirty_[clientId].inventory = true;
                        enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);
                        sendPlayerState(clientId);
                    }
                }
            }
            break;
        }
        default:
            LOG_WARN("Server", "Unknown bounty sub-action %d from client %d", subAction, clientId);
            break;
    }
}

} // namespace fate
