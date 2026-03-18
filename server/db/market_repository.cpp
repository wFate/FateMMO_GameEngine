#include "server/db/market_repository.h"
#include "engine/core/logger.h"

namespace fate {

MarketListingRecord MarketRepository::rowToListing(const pqxx::row& row) {
    MarketListingRecord r;
    r.listingId          = row["listing_id"].as<int>();
    r.sellerCharacterId  = row["seller_character_id"].as<std::string>();
    r.sellerCharacterName = row["seller_character_name"].as<std::string>();
    r.itemInstanceId     = row["item_instance_id"].as<std::string>();
    r.itemId             = row["item_id"].as<std::string>();
    r.itemName           = row["item_name"].as<std::string>();
    r.quantity           = row["quantity"].as<int>();
    r.enchantLevel       = row["enchant_level"].is_null() ? 0 : row["enchant_level"].as<int>();
    r.rolledStatsJson    = row["rolled_stats"].as<std::string>("{}");
    r.socketStat         = row["socket_stat"].is_null() ? "" : row["socket_stat"].as<std::string>();
    r.socketValue        = row["socket_value"].is_null() ? 0 : row["socket_value"].as<int>();
    r.priceGold          = row["price_gold"].as<int64_t>();
    r.listedAtUnix       = row["listed_at_unix"].is_null() ? 0 : row["listed_at_unix"].as<int64_t>();
    r.expiresAtUnix      = row["expires_at_unix"].is_null() ? 0 : row["expires_at_unix"].as<int64_t>();
    r.itemCategory       = row["item_category"].is_null() ? "" : row["item_category"].as<std::string>();
    r.itemSubtype        = row["item_subtype"].is_null() ? "" : row["item_subtype"].as<std::string>();
    r.itemRarity         = row["item_rarity"].is_null() ? "Common" : row["item_rarity"].as<std::string>();
    r.itemLevel          = row["item_level"].is_null() ? 1 : row["item_level"].as<int>();
    r.isActive           = row["is_active"].as<bool>();
    return r;
}

int MarketRepository::countActiveListings(const std::string& characterId) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT COUNT(*) FROM market_listings "
            "WHERE seller_character_id = $1 AND is_active = TRUE AND expires_at > NOW()",
            characterId);
        txn.commit();
        return result[0][0].as<int>();
    } catch (const std::exception& e) {
        LOG_ERROR("MarketRepo", "countActiveListings failed: %s", e.what());
    }
    return 0;
}

int MarketRepository::createListing(const std::string& sellerId, const std::string& sellerName,
                                     const std::string& instanceId, const std::string& itemId,
                                     const std::string& itemName, int quantity, int enchantLevel,
                                     const std::string& rolledStats, const std::string& socketStat,
                                     int socketValue, int64_t priceGold,
                                     const std::string& category, const std::string& subtype,
                                     const std::string& rarity, int itemLevel) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "INSERT INTO market_listings ("
            "seller_character_id, seller_character_name, item_instance_id, item_id, "
            "quantity, rolled_stats, socket_stat, socket_value, enchant_level, "
            "price_gold, listed_at, expires_at, item_name, item_category, "
            "item_subtype, item_rarity, item_level, is_active) "
            "VALUES ($1, $2, $3::uuid, $4, $5, $6::jsonb, $7, $8, $9, $10, "
            "NOW(), NOW() + INTERVAL '2 days', $11, $12, $13, $14, $15, TRUE) "
            "RETURNING listing_id",
            sellerId, sellerName, instanceId, itemId,
            quantity, rolledStats,
            socketStat.empty() ? std::optional<std::string>(std::nullopt) : std::optional(socketStat),
            socketValue != 0 ? std::optional(socketValue) : std::optional<int>(std::nullopt),
            enchantLevel, priceGold,
            itemName, category, subtype, rarity, itemLevel);
        txn.commit();
        return result[0][0].as<int>();
    } catch (const std::exception& e) {
        LOG_ERROR("MarketRepo", "createListing failed: %s", e.what());
    }
    return -1;
}

std::optional<MarketListingRecord> MarketRepository::getListing(int listingId) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT listing_id, seller_character_id, seller_character_name, "
            "item_instance_id::text, item_id, item_name, quantity, enchant_level, "
            "rolled_stats::text, socket_stat, socket_value, price_gold, "
            "EXTRACT(EPOCH FROM listed_at)::BIGINT AS listed_at_unix, "
            "EXTRACT(EPOCH FROM expires_at)::BIGINT AS expires_at_unix, "
            "item_category, item_subtype, item_rarity, item_level, is_active "
            "FROM market_listings WHERE listing_id = $1", listingId);
        txn.commit();
        if (result.empty()) return std::nullopt;
        return rowToListing(result[0]);
    } catch (const std::exception& e) {
        LOG_ERROR("MarketRepo", "getListing failed: %s", e.what());
    }
    return std::nullopt;
}

std::vector<MarketListingRecord> MarketRepository::getPlayerListings(const std::string& characterId) {
    std::vector<MarketListingRecord> listings;
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT listing_id, seller_character_id, seller_character_name, "
            "item_instance_id::text, item_id, item_name, quantity, enchant_level, "
            "rolled_stats::text, socket_stat, socket_value, price_gold, "
            "EXTRACT(EPOCH FROM listed_at)::BIGINT AS listed_at_unix, "
            "EXTRACT(EPOCH FROM expires_at)::BIGINT AS expires_at_unix, "
            "item_category, item_subtype, item_rarity, item_level, is_active "
            "FROM market_listings "
            "WHERE seller_character_id = $1 AND is_active = TRUE "
            "ORDER BY listed_at DESC", characterId);
        txn.commit();
        listings.reserve(result.size());
        for (const auto& row : result) {
            listings.push_back(rowToListing(row));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("MarketRepo", "getPlayerListings failed: %s", e.what());
    }
    return listings;
}

bool MarketRepository::cancelListing(int listingId, const std::string& characterId) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "UPDATE market_listings SET is_active = FALSE "
            "WHERE listing_id = $1 AND seller_character_id = $2 AND is_active = TRUE",
            listingId, characterId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("MarketRepo", "cancelListing failed: %s", e.what());
    }
    return false;
}

bool MarketRepository::deactivateListing(pqxx::work& txn, int listingId) {
    try {
        txn.exec_params(
            "UPDATE market_listings SET is_active = FALSE WHERE listing_id = $1", listingId);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("MarketRepo", "deactivateListing failed: %s", e.what());
    }
    return false;
}

bool MarketRepository::logTransaction(int listingId, const std::string& sellerId,
                                       const std::string& sellerName, const std::string& buyerId,
                                       const std::string& buyerName, const std::string& itemId,
                                       const std::string& itemName, int quantity, int enchantLevel,
                                       const std::string& rolledStats, int64_t salePrice,
                                       int64_t taxAmount, int64_t sellerReceived) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "INSERT INTO market_transactions ("
            "listing_id, seller_character_id, seller_character_name, "
            "buyer_character_id, buyer_character_name, item_id, item_name, "
            "quantity, enchant_level, rolled_stats, sale_price, tax_amount, "
            "seller_received, sold_at) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10::jsonb, $11, $12, $13, NOW())",
            listingId, sellerId, sellerName, buyerId, buyerName,
            itemId, itemName, quantity, enchantLevel, rolledStats,
            salePrice, taxAmount, sellerReceived);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("MarketRepo", "logTransaction failed: %s", e.what());
    }
    return false;
}

std::vector<MarketTransactionRecord> MarketRepository::getPlayerTransactions(
    const std::string& characterId, int limit) {
    std::vector<MarketTransactionRecord> txns;
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT transaction_id, seller_character_id, seller_character_name, "
            "buyer_character_id, buyer_character_name, item_id, item_name, "
            "quantity, enchant_level, sale_price, tax_amount, seller_received, "
            "EXTRACT(EPOCH FROM sold_at)::BIGINT AS sold_at_unix "
            "FROM market_transactions "
            "WHERE seller_character_id = $1 OR buyer_character_id = $1 "
            "ORDER BY sold_at DESC LIMIT $2", characterId, limit);
        txn.commit();
        txns.reserve(result.size());
        for (const auto& row : result) {
            MarketTransactionRecord r;
            r.transactionId      = row["transaction_id"].as<int>();
            r.sellerCharacterId  = row["seller_character_id"].as<std::string>();
            r.sellerCharacterName = row["seller_character_name"].as<std::string>();
            r.buyerCharacterId   = row["buyer_character_id"].as<std::string>();
            r.buyerCharacterName = row["buyer_character_name"].as<std::string>();
            r.itemId             = row["item_id"].as<std::string>();
            r.itemName           = row["item_name"].as<std::string>();
            r.quantity           = row["quantity"].as<int>();
            r.enchantLevel       = row["enchant_level"].is_null() ? 0 : row["enchant_level"].as<int>();
            r.salePrice          = row["sale_price"].as<int64_t>();
            r.taxAmount          = row["tax_amount"].as<int64_t>();
            r.sellerReceived     = row["seller_received"].as<int64_t>();
            r.soldAtUnix         = row["sold_at_unix"].as<int64_t>();
            txns.push_back(std::move(r));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("MarketRepo", "getPlayerTransactions failed: %s", e.what());
    }
    return txns;
}

JackpotState MarketRepository::getJackpotState() {
    JackpotState state;
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec(
            "SELECT current_pool, EXTRACT(EPOCH FROM next_payout_at)::BIGINT AS next_unix "
            "FROM jackpot_pool WHERE id = 1");
        txn.commit();
        if (!result.empty()) {
            state.currentPool = result[0]["current_pool"].as<int64_t>();
            state.nextPayoutAtUnix = result[0]["next_unix"].as<int64_t>();
        }
    } catch (const std::exception& e) {
        LOG_ERROR("MarketRepo", "getJackpotState failed: %s", e.what());
    }
    return state;
}

int64_t MarketRepository::addToJackpot(int64_t amount) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "UPDATE jackpot_pool SET current_pool = current_pool + $1, last_updated_at = NOW() "
            "WHERE id = 1 RETURNING current_pool", amount);
        txn.commit();
        return result.empty() ? 0 : result[0][0].as<int64_t>();
    } catch (const std::exception& e) {
        LOG_ERROR("MarketRepo", "addToJackpot failed: %s", e.what());
    }
    return 0;
}

bool MarketRepository::resetJackpot(int64_t nextPayoutAtUnix) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "UPDATE jackpot_pool SET current_pool = 0, "
            "next_payout_at = to_timestamp($1), last_updated_at = NOW() WHERE id = 1",
            nextPayoutAtUnix);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("MarketRepo", "resetJackpot failed: %s", e.what());
    }
    return false;
}

int MarketRepository::deactivateExpiredListings() {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec(
            "UPDATE market_listings SET is_active = FALSE "
            "WHERE is_active = TRUE AND expires_at <= NOW()");
        txn.commit();
        return static_cast<int>(result.affected_rows());
    } catch (const std::exception& e) {
        LOG_ERROR("MarketRepo", "deactivateExpiredListings failed: %s", e.what());
    }
    return 0;
}

} // namespace fate
