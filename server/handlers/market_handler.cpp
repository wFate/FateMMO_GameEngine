#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "game/shared/game_types.h"
#include "game/shared/item_stat_roller.h"
#include "engine/net/game_messages.h"
#include <pqxx/pqxx>

namespace fate {

void ServerApp::processMarket(uint16_t clientId, ByteReader& payload) {
    uint8_t subAction = payload.readU8();
    if (!validatePayload(payload, clientId, PacketType::CmdMarket)) return;
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    switch (subAction) {
        case MarketAction::ListItem: {
            std::string instanceId = payload.readString();
            int64_t priceGold = detail::readI64(payload);
            uint64_t nonce = detail::readU64(payload);
            if (!validatePayload(payload, clientId, PacketType::CmdMarket)) return;

            PersistentId pid(client->playerEntityId);
            EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
            Entity* e = getWorldForClient(clientId).getEntity(h);
            if (!e) break;

            auto* inv = e->getComponent<InventoryComponent>();
            if (!inv) break;

            auto sendMarketError = [&](const std::string& msg) {
                SvMarketResultMsg resp;
                resp.action = MarketAction::ListItem;
                resp.resultCode = 1;
                resp.message = msg;
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvMarketResult, buf, w.size());
            };

            // Validate nonce
            if (!nonceManager_.consume(clientId, nonce, gameTime_)) {
                sendMarketError("Invalid or expired nonce — reopen market");
                break;
            }

            // Validate price
            if (priceGold <= 0 || priceGold > MarketConstants::MAX_LISTING_PRICE) {
                sendMarketError("Invalid price"); break;
            }

            // Validate listing count
            int activeCount = marketRepo_->countActiveListings(client->character_id);
            if (activeCount >= MarketConstants::MAX_LISTINGS_PER_PLAYER) {
                sendMarketError("Maximum listings reached (7)"); break;
            }

            // Find item in inventory by instance ID
            int slot = inv->inventory.findByInstanceId(instanceId);
            if (slot < 0) { sendMarketError("Item not found in inventory"); break; }

            ItemInstance item = inv->inventory.getSlot(slot);
            if (item.isBound()) { sendMarketError("Soulbound items cannot be listed"); break; }
            if (inv->inventory.isSlotLocked(slot)) { sendMarketError("Item is locked for trade"); break; }

            // Look up item definition for listing metadata
            const auto* def = itemDefCache_.getDefinition(item.itemId);
            std::string itemName = def ? def->displayName : item.itemId;
            std::string category = def ? def->itemType : "";
            std::string subtype  = def ? def->subtype : "";
            std::string rarity   = def ? def->rarity : "Common";
            int itemLevel        = def ? def->levelReq : 1;

            std::string rolledJson = ItemStatRoller::rolledStatsToJson(item.rolledStats);
            std::string socketStat;
            int socketVal = 0;
            if (item.hasSocket()) {
                // Convert StatType to string for DB
                switch (item.socket.statType) {
                    case StatType::Strength:     socketStat = "STR"; break;
                    case StatType::Dexterity:    socketStat = "DEX"; break;
                    case StatType::Intelligence: socketStat = "INT"; break;
                    default: break;
                }
                socketVal = item.socket.value;
            }

            // Create listing in DB
            int listingId = marketRepo_->createListing(
                client->character_id, "", // seller name filled by DB or we fetch it
                instanceId, item.itemId, itemName,
                item.quantity, item.enchantLevel,
                rolledJson, socketStat, socketVal, priceGold,
                category, subtype, rarity, itemLevel);

            if (listingId > 0) {
                // WAL: record item removal before mutating inventory
                wal_.appendItemRemove(client->character_id, slot);
                // Remove from inventory
                inv->inventory.removeItem(slot);
                playerDirty_[clientId].inventory = true;
                enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

                SvMarketResultMsg resp;
                resp.action = MarketAction::ListItem;
                resp.resultCode = 0;
                resp.listingId = listingId;
                resp.message = itemName + " listed for " + std::to_string(priceGold) + " gold";
                resp.nonce = nonceManager_.issue(clientId, gameTime_);
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvMarketResult, buf, w.size());
                sendPlayerState(clientId);
                saveInventoryForClient(clientId);
            } else {
                sendMarketError("Failed to create listing");
            }
            break;
        }
        case MarketAction::BuyItem: {
            int32_t listingId = payload.readI32();
            uint64_t nonce = detail::readU64(payload);
            if (!validatePayload(payload, clientId, PacketType::CmdMarket)) return;

            PersistentId pid(client->playerEntityId);
            EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
            Entity* e = getWorldForClient(clientId).getEntity(h);
            if (!e) break;

            auto* inv = e->getComponent<InventoryComponent>();
            if (!inv) break;

            auto sendMarketError = [&](const std::string& msg) {
                SvMarketResultMsg resp;
                resp.action = MarketAction::BuyItem;
                resp.resultCode = 1;
                resp.message = msg;
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvMarketResult, buf, w.size());
            };

            // Validate nonce
            if (!nonceManager_.consume(clientId, nonce, gameTime_)) {
                sendMarketError("Invalid or expired nonce — reopen market");
                break;
            }

            // Get listing
            auto listing = marketRepo_->getListing(listingId);
            if (!listing || !listing->isActive) { sendMarketError("Listing not found or expired"); break; }

            // Can't buy own listing
            if (listing->sellerCharacterId == client->character_id) {
                sendMarketError("Cannot buy your own listing"); break;
            }

            // Check buyer gold
            if (inv->inventory.getGold() < listing->priceGold) {
                sendMarketError("Not enough gold"); break;
            }

            // Check buyer inventory space
            if (inv->inventory.freeSlots() <= 0) {
                sendMarketError("Inventory full"); break;
            }

            // Execute purchase in a transaction via the pool
            try {
                auto guard = dbPool_.acquire_guard();
                pqxx::work txn(guard.connection());

                // Atomically claim listing — fails if another buyer got it first
                if (!marketRepo_->deactivateListing(txn, listingId)) {
                    txn.abort();
                    sendMarketError("Listing already sold");
                    break;
                }

                // Calculate tax (use double to preserve precision above 16M gold)
                int64_t tax = static_cast<int64_t>(static_cast<double>(listing->priceGold) * static_cast<double>(MarketConstants::TAX_RATE));
                int64_t sellerReceived = listing->priceGold - tax;

                // WAL: record gold deduction before mutating
                wal_.appendGoldChange(client->character_id, -listing->priceGold);
                // Deduct buyer gold
                inv->inventory.removeGold(listing->priceGold);

                // Add item to buyer inventory
                ItemInstance boughtItem;
                boughtItem.instanceId   = listing->itemInstanceId;
                boughtItem.itemId       = listing->itemId;
                boughtItem.quantity      = listing->quantity;
                boughtItem.enchantLevel = listing->enchantLevel;
                boughtItem.rolledStats  = ItemStatRoller::parseRolledStats(listing->rolledStatsJson);
                // Look up display info from item definition cache
                if (auto* def = itemDefCache_.getDefinition(listing->itemId)) {
                    boughtItem.displayName = def->displayName;
                    boughtItem.rarity = parseItemRarity(def->rarity);
                }
                // WAL: record item add (slot=-1 = auto-slot; recovery matches by instanceId)
                wal_.appendItemAdd(client->character_id, -1, boughtItem.instanceId);
                inv->inventory.addItem(boughtItem);
                playerDirty_[clientId].inventory = true;

                // Credit seller gold (update DB directly — seller may be offline)
                txn.exec_params(
                    "UPDATE characters SET gold = gold + $2 WHERE character_id = $1",
                    listing->sellerCharacterId, sellerReceived);

                // Add tax to jackpot
                txn.exec_params(
                    "UPDATE jackpot_pool SET current_pool = current_pool + $1, "
                    "last_updated_at = NOW() WHERE id = 1", tax);

                txn.commit();

                // Log transaction
                marketRepo_->logTransaction(listingId, listing->sellerCharacterId,
                                             listing->sellerCharacterName,
                                             client->character_id, "",
                                             listing->itemId, listing->itemName,
                                             listing->quantity, listing->enchantLevel,
                                             listing->rolledStatsJson,
                                             listing->priceGold, tax, sellerReceived);

                enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

                SvMarketResultMsg resp;
                resp.action = MarketAction::BuyItem;
                resp.resultCode = 0;
                resp.listingId = listingId;
                resp.message = "Purchased " + listing->itemName + " for " + std::to_string(listing->priceGold) + " gold";
                resp.nonce = nonceManager_.issue(clientId, gameTime_);
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvMarketResult, buf, w.size());
                sendPlayerState(clientId);
                saveInventoryForClient(clientId);
            } catch (const std::exception& ex) {
                LOG_ERROR("Server", "Market buy failed: %s", ex.what());
                sendMarketError("Purchase failed — please try again");
            }
            break;
        }
        case MarketAction::CancelListing: {
            int32_t listingId = payload.readI32();
            if (!validatePayload(payload, clientId, PacketType::CmdMarket)) return;

            PersistentId cancelPid(client->playerEntityId);
            EntityHandle cancelH = getReplicationForClient(clientId).getEntityHandle(cancelPid);
            Entity* cancelE = getWorldForClient(clientId).getEntity(cancelH);
            if (!cancelE) break;
            auto* inv = cancelE->getComponent<InventoryComponent>();
            if (!inv) break;

            auto sendCancelError = [&](const std::string& msg) {
                SvMarketResultMsg errResp;
                errResp.action = MarketAction::CancelListing;
                errResp.resultCode = 1;
                errResp.message = msg;
                uint8_t ebuf[256]; ByteWriter ew(ebuf, sizeof(ebuf));
                errResp.write(ew);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvMarketResult, ebuf, ew.size());
            };

            // Fetch listing before deactivating so we can return the item
            auto listing = marketRepo_->getListing(listingId);
            if (!listing || listing->sellerCharacterId != client->character_id || !listing->isActive) {
                sendCancelError("Listing not found or not yours");
                break;
            }

            marketRepo_->cancelListing(listingId, client->character_id);

            // Reconstruct item and return to seller inventory
            ItemInstance returnedItem;
            returnedItem.instanceId   = listing->itemInstanceId;
            returnedItem.itemId       = listing->itemId;
            returnedItem.quantity      = listing->quantity;
            returnedItem.enchantLevel = listing->enchantLevel;
            returnedItem.rolledStats  = ItemStatRoller::parseRolledStats(listing->rolledStatsJson);
            if (!listing->socketStat.empty() && listing->socketValue > 0) {
                returnedItem.socket.isEmpty = false;
                returnedItem.socket.value = listing->socketValue;
                if (listing->socketStat == "STR") returnedItem.socket.statType = StatType::Strength;
                else if (listing->socketStat == "DEX") returnedItem.socket.statType = StatType::Dexterity;
                else if (listing->socketStat == "INT") returnedItem.socket.statType = StatType::Intelligence;
            }
            if (auto* def = itemDefCache_.getDefinition(listing->itemId)) {
                returnedItem.displayName = def->displayName;
                returnedItem.rarity = parseItemRarity(def->rarity);
            }

            inv->inventory.addItem(returnedItem);
            playerDirty_[clientId].inventory = true;
            enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);

            SvMarketResultMsg resp;
            resp.action = MarketAction::CancelListing;
            resp.resultCode = 0;
            resp.listingId = listingId;
            resp.message = "Listing cancelled";
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            resp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvMarketResult, buf, w.size());
            sendPlayerState(clientId);
            saveInventoryForClient(clientId);
            break;
        }
        case MarketAction::GetListings: {
            // Issue a nonce for subsequent buy/list operations
            SvMarketResultMsg resp;
            resp.action = MarketAction::GetListings;
            resp.resultCode = 0;
            resp.nonce = nonceManager_.issue(clientId, gameTime_);
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            resp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvMarketResult, buf, w.size());
            break;
        }
        case MarketAction::GetMyListings: {
            // Issue a nonce for subsequent buy/list operations
            SvMarketResultMsg resp;
            resp.action = MarketAction::GetMyListings;
            resp.resultCode = 0;
            resp.nonce = nonceManager_.issue(clientId, gameTime_);
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            resp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvMarketResult, buf, w.size());
            break;
        }
        default:
            LOG_WARN("Server", "Unknown market sub-action %d from client %d", subAction, clientId);
            break;
    }
}

} // namespace fate
