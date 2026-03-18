#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <pqxx/pqxx>
#include "game/shared/market_manager.h"  // JackpotState

namespace fate {

struct MarketListingRecord {
    int listingId = 0;
    std::string sellerCharacterId;
    std::string sellerCharacterName;
    std::string itemInstanceId;     // UUID as string
    std::string itemId;
    std::string itemName;
    int quantity = 1;
    int enchantLevel = 0;
    std::string rolledStatsJson;
    std::string socketStat;
    int socketValue = 0;
    int64_t priceGold = 0;
    int64_t listedAtUnix = 0;
    int64_t expiresAtUnix = 0;
    std::string itemCategory;
    std::string itemSubtype;
    std::string itemRarity;
    int itemLevel = 1;
    bool isActive = true;
};

struct MarketTransactionRecord {
    int transactionId = 0;
    std::string sellerCharacterId;
    std::string sellerCharacterName;
    std::string buyerCharacterId;
    std::string buyerCharacterName;
    std::string itemId;
    std::string itemName;
    int quantity = 1;
    int enchantLevel = 0;
    int64_t salePrice = 0;
    int64_t taxAmount = 0;
    int64_t sellerReceived = 0;
    int64_t soldAtUnix = 0;
};

class MarketRepository {
public:
    explicit MarketRepository(pqxx::connection& conn) : conn_(conn) {}

    // Listings
    int countActiveListings(const std::string& characterId);
    int createListing(const std::string& sellerId, const std::string& sellerName,
                      const std::string& instanceId, const std::string& itemId,
                      const std::string& itemName, int quantity, int enchantLevel,
                      const std::string& rolledStats, const std::string& socketStat,
                      int socketValue, int64_t priceGold,
                      const std::string& category, const std::string& subtype,
                      const std::string& rarity, int itemLevel);
    std::optional<MarketListingRecord> getListing(int listingId);
    std::vector<MarketListingRecord> getPlayerListings(const std::string& characterId);
    bool cancelListing(int listingId, const std::string& characterId);
    bool deactivateListing(pqxx::work& txn, int listingId);

    // Transactions
    bool logTransaction(int listingId, const std::string& sellerId, const std::string& sellerName,
                        const std::string& buyerId, const std::string& buyerName,
                        const std::string& itemId, const std::string& itemName,
                        int quantity, int enchantLevel, const std::string& rolledStats,
                        int64_t salePrice, int64_t taxAmount, int64_t sellerReceived);
    std::vector<MarketTransactionRecord> getPlayerTransactions(const std::string& characterId,
                                                                 int limit = 50);

    // Jackpot
    JackpotState getJackpotState();
    int64_t addToJackpot(int64_t amount);
    bool resetJackpot(int64_t nextPayoutAtUnix);

    // Expiration
    int deactivateExpiredListings();

private:
    pqxx::connection& conn_;

    static MarketListingRecord rowToListing(const pqxx::row& row);
};

} // namespace fate
