#include "server/db/trade_repository.h"
#include "engine/core/logger.h"

namespace fate {

int TradeRepository::createSession(const std::string& playerAId, const std::string& playerBId,
                                    const std::string& sceneName) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "INSERT INTO trade_sessions ("
            "player_a_character_id, player_b_character_id, scene_name, "
            "player_a_locked, player_b_locked, player_a_confirmed, player_b_confirmed, "
            "player_a_gold, player_b_gold, status, created_at) "
            "VALUES ($1, $2, $3, FALSE, FALSE, FALSE, FALSE, 0, 0, 'active', NOW()) "
            "RETURNING session_id",
            playerAId, playerBId, sceneName);
        txn.commit();
        return result[0][0].as<int>();
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "createSession failed: %s", e.what());
    }
    return -1;
}

std::optional<TradeSessionRecord> TradeRepository::loadSession(int sessionId) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT session_id, player_a_character_id, player_b_character_id, "
            "player_a_locked, player_b_locked, player_a_confirmed, player_b_confirmed, "
            "player_a_gold, player_b_gold, status, scene_name "
            "FROM trade_sessions WHERE session_id = $1", sessionId);
        txn.commit();
        if (result.empty()) return std::nullopt;

        const auto& row = result[0];
        TradeSessionRecord r;
        r.sessionId          = row["session_id"].as<int>();
        r.playerACharacterId = row["player_a_character_id"].as<std::string>();
        r.playerBCharacterId = row["player_b_character_id"].as<std::string>();
        r.playerALocked      = row["player_a_locked"].as<bool>();
        r.playerBLocked      = row["player_b_locked"].as<bool>();
        r.playerAConfirmed   = row["player_a_confirmed"].as<bool>();
        r.playerBConfirmed   = row["player_b_confirmed"].as<bool>();
        r.playerAGold        = row["player_a_gold"].as<int64_t>();
        r.playerBGold        = row["player_b_gold"].as<int64_t>();
        r.status             = row["status"].as<std::string>();
        r.sceneName          = row["scene_name"].as<std::string>();
        return r;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "loadSession failed: %s", e.what());
    }
    return std::nullopt;
}

std::optional<TradeSessionRecord> TradeRepository::getActiveSession(const std::string& characterId) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT session_id, player_a_character_id, player_b_character_id, "
            "player_a_locked, player_b_locked, player_a_confirmed, player_b_confirmed, "
            "player_a_gold, player_b_gold, status, scene_name "
            "FROM trade_sessions "
            "WHERE status = 'active' AND "
            "(player_a_character_id = $1 OR player_b_character_id = $1) LIMIT 1",
            characterId);
        txn.commit();
        if (result.empty()) return std::nullopt;

        const auto& row = result[0];
        TradeSessionRecord r;
        r.sessionId          = row["session_id"].as<int>();
        r.playerACharacterId = row["player_a_character_id"].as<std::string>();
        r.playerBCharacterId = row["player_b_character_id"].as<std::string>();
        r.playerALocked      = row["player_a_locked"].as<bool>();
        r.playerBLocked      = row["player_b_locked"].as<bool>();
        r.playerAConfirmed   = row["player_a_confirmed"].as<bool>();
        r.playerBConfirmed   = row["player_b_confirmed"].as<bool>();
        r.playerAGold        = row["player_a_gold"].as<int64_t>();
        r.playerBGold        = row["player_b_gold"].as<int64_t>();
        r.status             = row["status"].as<std::string>();
        r.sceneName          = row["scene_name"].as<std::string>();
        return r;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "getActiveSession failed: %s", e.what());
    }
    return std::nullopt;
}

bool TradeRepository::isPlayerInTrade(const std::string& characterId) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT EXISTS(SELECT 1 FROM trade_sessions "
            "WHERE status = 'active' AND "
            "(player_a_character_id = $1 OR player_b_character_id = $1))",
            characterId);
        txn.commit();
        return result[0][0].as<bool>();
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "isPlayerInTrade failed: %s", e.what());
    }
    return false;
}

bool TradeRepository::cancelSession(int sessionId) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params("DELETE FROM trade_offers WHERE session_id = $1", sessionId);
        txn.exec_params(
            "UPDATE trade_sessions SET status = 'cancelled', completed_at = NOW() "
            "WHERE session_id = $1", sessionId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "cancelSession failed: %s", e.what());
    }
    return false;
}

bool TradeRepository::setPlayerLocked(int sessionId, const std::string& characterId, bool locked) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "UPDATE trade_sessions SET "
            "player_a_locked = CASE WHEN player_a_character_id = $2 THEN $3 ELSE player_a_locked END, "
            "player_b_locked = CASE WHEN player_b_character_id = $2 THEN $3 ELSE player_b_locked END "
            "WHERE session_id = $1 AND status = 'active'",
            sessionId, characterId, locked);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "setPlayerLocked failed: %s", e.what());
    }
    return false;
}

bool TradeRepository::setPlayerConfirmed(int sessionId, const std::string& characterId, bool confirmed) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "UPDATE trade_sessions SET "
            "player_a_confirmed = CASE WHEN player_a_character_id = $2 THEN $3 ELSE player_a_confirmed END, "
            "player_b_confirmed = CASE WHEN player_b_character_id = $2 THEN $3 ELSE player_b_confirmed END "
            "WHERE session_id = $1 AND status = 'active'",
            sessionId, characterId, confirmed);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "setPlayerConfirmed failed: %s", e.what());
    }
    return false;
}

bool TradeRepository::setPlayerGold(int sessionId, const std::string& characterId, int64_t gold) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "UPDATE trade_sessions SET "
            "player_a_gold = CASE WHEN player_a_character_id = $2 THEN $3 ELSE player_a_gold END, "
            "player_b_gold = CASE WHEN player_b_character_id = $2 THEN $3 ELSE player_b_gold END "
            "WHERE session_id = $1 AND status = 'active'",
            sessionId, characterId, gold);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "setPlayerGold failed: %s", e.what());
    }
    return false;
}

bool TradeRepository::unlockBothPlayers(int sessionId) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "UPDATE trade_sessions SET "
            "player_a_locked = FALSE, player_b_locked = FALSE, "
            "player_a_confirmed = FALSE, player_b_confirmed = FALSE "
            "WHERE session_id = $1 AND status = 'active'", sessionId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "unlockBothPlayers failed: %s", e.what());
    }
    return false;
}

bool TradeRepository::resetConfirms(int sessionId) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "UPDATE trade_sessions SET player_a_confirmed = FALSE, player_b_confirmed = FALSE "
            "WHERE session_id = $1 AND status = 'active'", sessionId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "resetConfirms failed: %s", e.what());
    }
    return false;
}

bool TradeRepository::addItemToTrade(int sessionId, const std::string& characterId, int slotIndex,
                                      int sourceSlot, const std::string& instanceId, int quantity) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "INSERT INTO trade_offers (session_id, character_id, slot_index, "
            "inventory_source_slot, item_instance_id, quantity) "
            "VALUES ($1, $2, $3, $4, $5::uuid, $6) "
            "ON CONFLICT (session_id, character_id, slot_index) DO UPDATE SET "
            "inventory_source_slot = $4, item_instance_id = $5::uuid, quantity = $6",
            sessionId, characterId, slotIndex, sourceSlot, instanceId, quantity);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "addItemToTrade failed: %s", e.what());
    }
    return false;
}

bool TradeRepository::removeItemFromTrade(int sessionId, const std::string& characterId, int slotIndex) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "DELETE FROM trade_offers "
            "WHERE session_id = $1 AND character_id = $2 AND slot_index = $3",
            sessionId, characterId, slotIndex);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "removeItemFromTrade failed: %s", e.what());
    }
    return false;
}

bool TradeRepository::clearPlayerOffers(int sessionId, const std::string& characterId) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "DELETE FROM trade_offers WHERE session_id = $1 AND character_id = $2",
            sessionId, characterId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "clearPlayerOffers failed: %s", e.what());
    }
    return false;
}

std::vector<TradeOfferRecord> TradeRepository::getTradeOffers(int sessionId,
                                                               const std::string& characterId) {
    std::vector<TradeOfferRecord> offers;
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT t.offer_id, t.slot_index, t.inventory_source_slot, "
            "t.item_instance_id::text, t.quantity, "
            "ci.item_id, ci.enchant_level, ci.is_protected, "
            "ci.rolled_stats::text, id.name, id.rarity "
            "FROM trade_offers t "
            "JOIN character_inventory ci ON ci.instance_id = t.item_instance_id "
            "JOIN item_definitions id ON id.item_id = ci.item_id "
            "WHERE t.session_id = $1 AND t.character_id = $2 "
            "ORDER BY t.slot_index", sessionId, characterId);
        txn.commit();
        offers.reserve(result.size());
        for (const auto& row : result) {
            TradeOfferRecord r;
            r.offerId            = row["offer_id"].as<int>();
            r.slotIndex          = row["slot_index"].as<int>();
            r.inventorySourceSlot = row["inventory_source_slot"].as<int>();
            r.itemInstanceId     = row["item_instance_id"].as<std::string>();
            r.quantity           = row["quantity"].as<int>();
            r.itemId             = row["item_id"].as<std::string>();
            r.enchantLevel       = row["enchant_level"].is_null() ? 0 : row["enchant_level"].as<int>();
            r.isProtected        = row["is_protected"].as<bool>();
            r.rolledStatsJson    = row["rolled_stats"].as<std::string>("{}");
            r.itemName           = row["name"].as<std::string>();
            r.rarity             = row["rarity"].is_null() ? "Common" : row["rarity"].as<std::string>();
            offers.push_back(std::move(r));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "getTradeOffers failed: %s", e.what());
    }
    return offers;
}

bool TradeRepository::transferItem(pqxx::work& txn, const std::string& instanceId,
                                    const std::string& newOwner) {
    try {
        txn.exec_params(
            "UPDATE character_inventory SET character_id = $2, "
            "slot_index = NULL, bag_slot_index = NULL, bag_item_slot = NULL, "
            "is_equipped = FALSE, equipped_slot = NULL "
            "WHERE instance_id = $1::uuid", instanceId, newOwner);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "transferItem failed: %s", e.what());
    }
    return false;
}

bool TradeRepository::updateGold(pqxx::work& txn, const std::string& characterId, int64_t delta) {
    try {
        txn.exec_params(
            "UPDATE characters SET gold = gold + $2 WHERE character_id = $1",
            characterId, delta);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "updateGold failed: %s", e.what());
    }
    return false;
}

bool TradeRepository::completeSession(pqxx::work& txn, int sessionId) {
    try {
        txn.exec_params("DELETE FROM trade_offers WHERE session_id = $1", sessionId);
        txn.exec_params(
            "UPDATE trade_sessions SET status = 'completed', completed_at = NOW() "
            "WHERE session_id = $1", sessionId);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "completeSession failed: %s", e.what());
    }
    return false;
}

bool TradeRepository::logTradeHistory(int sessionId, const std::string& playerAId,
                                       const std::string& playerBId, int64_t goldA, int64_t goldB,
                                       const std::string& itemsAJson, const std::string& itemsBJson) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "INSERT INTO trade_history (session_id, player_a_character_id, player_b_character_id, "
            "player_a_gold, player_b_gold, player_a_items, player_b_items, completed_at) "
            "VALUES ($1, $2, $3, $4, $5, $6::jsonb, $7::jsonb, NOW())",
            sessionId, playerAId, playerBId, goldA, goldB, itemsAJson, itemsBJson);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "logTradeHistory failed: %s", e.what());
    }
    return false;
}

int TradeRepository::cleanStaleSessions(int maxAgeMinutes) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "UPDATE trade_sessions SET status = 'cancelled', completed_at = NOW() "
            "WHERE status = 'active' AND created_at < NOW() - INTERVAL '1 minute' * $1",
            maxAgeMinutes);
        txn.commit();
        return static_cast<int>(result.affected_rows());
    } catch (const std::exception& e) {
        LOG_ERROR("TradeRepo", "cleanStaleSessions failed: %s", e.what());
    }
    return 0;
}

} // namespace fate
