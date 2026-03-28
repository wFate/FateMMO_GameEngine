#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "engine/net/game_messages.h"
#include "game/shared/game_types.h"
#include <pqxx/pqxx>

namespace fate {

void ServerApp::processTrade(uint16_t clientId, ByteReader& payload) {
    uint8_t subAction = payload.readU8();
    if (!validatePayload(payload, clientId, PacketType::CmdTrade)) return;
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
    if (!e) return;
    auto* charStats = e->getComponent<CharacterStatsComponent>();

    auto sendTradeResult = [&](uint8_t type, uint8_t code, const std::string& msg) {
        SvTradeUpdateMsg resp;
        resp.updateType = type;
        resp.resultCode = code;
        resp.otherPlayerName = msg;
        uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
        resp.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvTradeUpdate, buf, w.size());
    };

    switch (subAction) {
        case TradeAction::Initiate: {
            std::string targetCharId = payload.readString();
            if (!validatePayload(payload, clientId, PacketType::CmdTrade)) return;

            // Check if already in trade
            if (tradeRepo_->isPlayerInTrade(client->character_id)) {
                sendTradeResult(6, 1, "Already in a trade"); break;
            }
            if (tradeRepo_->isPlayerInTrade(targetCharId)) {
                sendTradeResult(6, 2, "Target is already trading"); break;
            }

            // Get current scene from player's CharacterStatsComponent (server-side)
            std::string scene = (charStats && !charStats->stats.currentScene.empty())
                ? charStats->stats.currentScene : "WhisperingWoods";

            int sessionId = tradeRepo_->createSession(client->character_id, targetCharId, scene);
            if (sessionId > 0) {
                sendTradeResult(1, 0, "Trade session started");
                // Notify target player via system chat + trade invite
                std::string senderName = charStats ? charStats->stats.characterName : "Someone";
                server_.connections().forEach([&](ClientConnection& c) {
                    if (c.character_id == targetCharId) {
                        // System chat notification
                        SvChatMessageMsg chatMsg;
                        chatMsg.channel    = static_cast<uint8_t>(ChatChannel::System);
                        chatMsg.senderName = "[Trade]";
                        chatMsg.message    = senderName + " wants to trade with you.";
                        chatMsg.faction    = 0;
                        uint8_t chatBuf[512]; ByteWriter cw(chatBuf, sizeof(chatBuf));
                        chatMsg.write(cw);
                        server_.sendTo(c.clientId, Channel::ReliableOrdered,
                                       PacketType::SvChatMessage, chatBuf, cw.size());

                        // Trade invite update
                        SvTradeUpdateMsg invite;
                        invite.updateType = 0; // invited
                        invite.sessionId  = sessionId;
                        invite.otherPlayerName = senderName;
                        invite.resultCode = 0;
                        uint8_t tbuf[256]; ByteWriter tw(tbuf, sizeof(tbuf));
                        invite.write(tw);
                        server_.sendTo(c.clientId, Channel::ReliableOrdered,
                                       PacketType::SvTradeUpdate, tbuf, tw.size());
                    }
                });
            } else {
                sendTradeResult(6, 3, "Failed to create trade session");
            }
            break;
        }
        case TradeAction::AddItem: {
            uint8_t slotIdx = payload.readU8();
            int32_t sourceSlot = payload.readI32();
            std::string instanceId = payload.readString();
            int32_t quantity = payload.readI32();
            if (!validatePayload(payload, clientId, PacketType::CmdTrade)) return;

            auto session = tradeRepo_->getActiveSession(client->character_id);
            if (!session) { sendTradeResult(6, 1, "Not in a trade"); break; }

            // Validate item exists and is tradeable
            auto* inv = e->getComponent<InventoryComponent>();
            if (!inv) break;
            int invSlot = inv->inventory.findByInstanceId(instanceId);
            if (invSlot < 0) { sendTradeResult(6, 4, "Item not found"); break; }
            if (inv->inventory.isSlotLocked(invSlot)) { sendTradeResult(6, 4, "Item already in trade"); break; }
            ItemInstance item = inv->inventory.getSlot(invSlot);
            if (item.isBound()) { sendTradeResult(6, 5, "Item is soulbound"); break; }

            tradeRepo_->addItemToTrade(session->sessionId, client->character_id,
                                        slotIdx, sourceSlot, instanceId, quantity);
            inv->inventory.lockSlotForTrade(invSlot);
            // Unlock both sides when items change
            tradeRepo_->unlockBothPlayers(session->sessionId);
            sendTradeResult(2, 0, "Item added");
            break;
        }
        case TradeAction::RemoveItem: {
            uint8_t slotIdx = payload.readU8();
            if (!validatePayload(payload, clientId, PacketType::CmdTrade)) return;
            auto session = tradeRepo_->getActiveSession(client->character_id);
            if (!session) break;
            tradeRepo_->removeItemFromTrade(session->sessionId, client->character_id, slotIdx);
            tradeRepo_->unlockBothPlayers(session->sessionId);
            sendTradeResult(2, 0, "Item removed");
            break;
        }
        case TradeAction::SetGold: {
            int64_t gold = detail::readI64(payload);
            if (!validatePayload(payload, clientId, PacketType::CmdTrade)) return;
            if (gold < 0) { sendTradeResult(6, 6, "Invalid gold amount"); break; }
            auto session = tradeRepo_->getActiveSession(client->character_id);
            if (!session) break;

            auto* inv = e->getComponent<InventoryComponent>();
            if (!inv || inv->inventory.getGold() < gold) {
                sendTradeResult(6, 6, "Not enough gold"); break;
            }

            tradeRepo_->setPlayerGold(session->sessionId, client->character_id, gold);
            tradeRepo_->unlockBothPlayers(session->sessionId);
            sendTradeResult(2, 0, "Gold set");
            break;
        }
        case TradeAction::Lock: {
            auto session = tradeRepo_->getActiveSession(client->character_id);
            if (!session) break;
            tradeRepo_->setPlayerLocked(session->sessionId, client->character_id, true);
            // Issue nonce for the Confirm step
            uint64_t nonce = nonceManager_.issue(clientId, gameTime_);
            SvTradeUpdateMsg lockResp;
            lockResp.updateType = 3; // locked
            lockResp.resultCode = 0;
            lockResp.nonce = nonce;
            lockResp.otherPlayerName = "Locked";
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            lockResp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvTradeUpdate, buf, w.size());
            break;
        }
        case TradeAction::Unlock: {
            auto session = tradeRepo_->getActiveSession(client->character_id);
            if (!session) break;
            tradeRepo_->unlockBothPlayers(session->sessionId);
            sendTradeResult(3, 0, "Unlocked");
            break;
        }
        case TradeAction::Confirm: {
            uint64_t nonce = detail::readU64(payload);
            if (!validatePayload(payload, clientId, PacketType::CmdTrade)) return;
            if (!nonceManager_.consume(clientId, nonce, gameTime_)) {
                sendTradeResult(6, 9, "Invalid or expired trade nonce");
                break;
            }
            auto session = tradeRepo_->getActiveSession(client->character_id);
            if (!session || !session->bothLocked()) {
                sendTradeResult(6, 7, "Both players must lock first"); break;
            }

            tradeRepo_->setPlayerConfirmed(session->sessionId, client->character_id, true);

            // Reload session to check if both confirmed
            session = tradeRepo_->loadSession(session->sessionId);
            if (session && session->bothConfirmed()) {
                // Acquire both player locks in consistent address order to
                // prevent deadlocks, then execute the trade atomically.
                auto lockA = playerLocks_.get(session->playerACharacterId);
                auto lockB = playerLocks_.get(session->playerBCharacterId);
                std::scoped_lock tradeLock(
                    (lockA.get() < lockB.get()) ? *lockA : *lockB,
                    (lockA.get() < lockB.get()) ? *lockB : *lockA
                );
                // Validate gold overflow before committing
                auto getGoldForChar = [&](const std::string& charId) -> int64_t {
                    int64_t g = 0;
                    server_.connections().forEach([&](ClientConnection& c) {
                        if (c.character_id == charId && c.playerEntityId != 0) {
                            PersistentId p(c.playerEntityId);
                            EntityHandle eh = getReplicationForClient(c.clientId).getEntityHandle(p);
                            Entity* ent = getWorldForClient(c.clientId).getEntity(eh);
                            if (ent) {
                                auto* inv = ent->getComponent<InventoryComponent>();
                                if (inv) g = inv->inventory.getGold();
                            }
                        }
                    });
                    return g;
                };
                int64_t goldA = getGoldForChar(session->playerACharacterId);
                int64_t goldB = getGoldForChar(session->playerBCharacterId);
                // Verify both players can still afford their gold offers
                if (goldA < session->playerAGold || goldB < session->playerBGold) {
                    sendTradeResult(6, 8, "Insufficient gold — trade cancelled");
                    tradeRepo_->cancelSession(session->sessionId);
                    break;
                }
                int64_t netA = goldA - session->playerAGold + session->playerBGold;
                int64_t netB = goldB - session->playerBGold + session->playerAGold;
                if (netA > InventoryConstants::MAX_GOLD || netB > InventoryConstants::MAX_GOLD) {
                    sendTradeResult(6, 8, "Trade would exceed gold cap");
                    break;
                }

                // Re-validate gold sufficiency (may have changed since lock)
                if (goldA < session->playerAGold) {
                    sendTradeResult(6, 8, "Trade failed — insufficient gold");
                    break;
                }
                if (goldB < session->playerBGold) {
                    sendTradeResult(6, 8, "Trade failed — insufficient gold");
                    break;
                }

                // Get offers and re-validate items still exist in inventory
                auto offersA = tradeRepo_->getTradeOffers(session->sessionId, session->playerACharacterId);
                auto offersB = tradeRepo_->getTradeOffers(session->sessionId, session->playerBCharacterId);
                {
                    auto findInvForValidation = [&](const std::string& charId) -> InventoryComponent* {
                        InventoryComponent* result = nullptr;
                        server_.connections().forEach([&](ClientConnection& c) {
                            if (c.character_id == charId && c.playerEntityId != 0) {
                                PersistentId p(c.playerEntityId);
                                EntityHandle eh = getReplicationForClient(c.clientId).getEntityHandle(p);
                                Entity* ent = getWorldForClient(c.clientId).getEntity(eh);
                                if (ent) result = ent->getComponent<InventoryComponent>();
                            }
                        });
                        return result;
                    };
                    auto* preInvA = findInvForValidation(session->playerACharacterId);
                    auto* preInvB = findInvForValidation(session->playerBCharacterId);
                    if (!preInvA || !preInvB) {
                        sendTradeResult(6, 8, "Trade failed — player offline");
                        break;
                    }
                    bool itemsMissing = false;
                    for (const auto& offer : offersA) {
                        if (preInvA->inventory.findByInstanceId(offer.itemInstanceId) < 0) {
                            itemsMissing = true; break;
                        }
                    }
                    if (!itemsMissing) {
                        for (const auto& offer : offersB) {
                            if (preInvB->inventory.findByInstanceId(offer.itemInstanceId) < 0) {
                                itemsMissing = true; break;
                            }
                        }
                    }
                    if (itemsMissing) {
                        sendTradeResult(6, 8, "Trade failed — an offered item no longer exists");
                        break;
                    }

                    // Verify both players have enough inventory space for incoming items.
                    // Free slots after removing offered items = current free + items being sent out.
                    int freeSlotsA = preInvA->inventory.freeSlots() + static_cast<int>(offersA.size());
                    int freeSlotsB = preInvB->inventory.freeSlots() + static_cast<int>(offersB.size());
                    if (freeSlotsA < static_cast<int>(offersB.size()) ||
                        freeSlotsB < static_cast<int>(offersA.size())) {
                        sendTradeResult(6, 8, "Trade failed — not enough inventory space");
                        tradeRepo_->cancelSession(session->sessionId);
                        break;
                    }
                }

                // Execute trade atomically
                try {
                    auto guard = dbPool_.acquire_guard();
                    pqxx::work txn(guard.connection());

                    // Transfer items A->B
                    for (const auto& offer : offersA) {
                        tradeRepo_->transferItem(txn, offer.itemInstanceId, session->playerBCharacterId);
                    }
                    // Transfer items B->A
                    for (const auto& offer : offersB) {
                        tradeRepo_->transferItem(txn, offer.itemInstanceId, session->playerACharacterId);
                    }

                    // Transfer gold (check return values to abort on failure)
                    if (session->playerAGold > 0) {
                        if (!tradeRepo_->updateGold(txn, session->playerACharacterId, -session->playerAGold) ||
                            !tradeRepo_->updateGold(txn, session->playerBCharacterId, session->playerAGold)) {
                            throw std::runtime_error("Gold transfer A->B failed");
                        }
                    }
                    if (session->playerBGold > 0) {
                        if (!tradeRepo_->updateGold(txn, session->playerBCharacterId, -session->playerBGold) ||
                            !tradeRepo_->updateGold(txn, session->playerACharacterId, session->playerBGold)) {
                            throw std::runtime_error("Gold transfer B->A failed");
                        }
                    }

                    // Complete session
                    tradeRepo_->completeSession(txn, session->sessionId);
                    txn.commit();

                    // Log history
                    tradeRepo_->logTradeHistory(session->sessionId,
                        session->playerACharacterId, session->playerBCharacterId,
                        session->playerAGold, session->playerBGold, "[]", "[]");

                    // Sync in-memory inventories to match DB state
                    auto findInvForCharId = [&](const std::string& charId) -> InventoryComponent* {
                        InventoryComponent* result = nullptr;
                        server_.connections().forEach([&](ClientConnection& c) {
                            if (c.character_id == charId && c.playerEntityId != 0) {
                                PersistentId p(c.playerEntityId);
                                EntityHandle eh = getReplicationForClient(c.clientId).getEntityHandle(p);
                                Entity* ent = getWorldForClient(c.clientId).getEntity(eh);
                                if (ent) result = ent->getComponent<InventoryComponent>();
                            }
                        });
                        return result;
                    };
                    auto* invA = findInvForCharId(session->playerACharacterId);
                    auto* invB = findInvForCharId(session->playerBCharacterId);
                    if (invA && invB) {
                        // Remove all offered items first (frees slots before adding)
                        std::vector<ItemInstance> itemsFromA, itemsFromB;
                        for (const auto& offer : offersA) {
                            int slot = invA->inventory.findByInstanceId(offer.itemInstanceId);
                            if (slot >= 0) {
                                itemsFromA.push_back(invA->inventory.getSlot(slot));
                                invA->inventory.removeItem(slot);
                            }
                        }
                        for (const auto& offer : offersB) {
                            int slot = invB->inventory.findByInstanceId(offer.itemInstanceId);
                            if (slot >= 0) {
                                itemsFromB.push_back(invB->inventory.getSlot(slot));
                                invB->inventory.removeItem(slot);
                            }
                        }
                        // Now add received items (slots freed above)
                        for (auto& item : itemsFromA) invB->inventory.addItem(item);
                        for (auto& item : itemsFromB) invA->inventory.addItem(item);
                        if (session->playerAGold > 0) {
                            invA->inventory.setGold(invA->inventory.getGold() - session->playerAGold);
                            invB->inventory.setGold(invB->inventory.getGold() + session->playerAGold);
                        }
                        if (session->playerBGold > 0) {
                            invB->inventory.setGold(invB->inventory.getGold() - session->playerBGold);
                            invA->inventory.setGold(invA->inventory.getGold() + session->playerBGold);
                        }
                    }
                    if (invA) invA->inventory.unlockAllTradeSlots();
                    if (invB) invB->inventory.unlockAllTradeSlots();

                    sendTradeResult(5, 0, "Trade completed!");
                    playerDirty_[clientId].inventory = true;
                    enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);
                    LOG_INFO("Server", "Trade %d completed: %s <-> %s",
                             session->sessionId,
                             session->playerACharacterId.c_str(),
                             session->playerBCharacterId.c_str());
                    saveInventoryForClient(clientId);
                    // Save other player's inventory too
                    std::string otherCharId = (client->character_id == session->playerACharacterId)
                        ? session->playerBCharacterId : session->playerACharacterId;
                    server_.connections().forEach([&](ClientConnection& c) {
                        if (c.character_id == otherCharId) {
                            playerDirty_[c.clientId].inventory = true;
                            enqueuePersist(c.clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);
                            saveInventoryForClient(c.clientId);
                        }
                    });
                } catch (const std::exception& ex) {
                    LOG_ERROR("Server", "Trade execution failed: %s", ex.what());
                    sendTradeResult(6, 8, "Trade failed — please try again");
                }
            } else {
                sendTradeResult(4, 0, "Confirmed — waiting for other player");
            }
            break;
        }
        case TradeAction::Cancel: {
            auto session = tradeRepo_->getActiveSession(client->character_id);
            if (session) {
                tradeRepo_->cancelSession(session->sessionId);
            }
            auto* inv = e->getComponent<InventoryComponent>();
            if (inv) inv->inventory.unlockAllTradeSlots();
            sendTradeResult(6, 0, "Trade cancelled");
            break;
        }
        default:
            LOG_WARN("Server", "Unknown trade sub-action %d from client %d", subAction, clientId);
            break;
    }
}

} // namespace fate
