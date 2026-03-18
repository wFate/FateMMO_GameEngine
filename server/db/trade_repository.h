#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <pqxx/pqxx>

namespace fate {

struct TradeSessionRecord {
    int sessionId = 0;
    std::string playerACharacterId;
    std::string playerBCharacterId;
    bool playerALocked = false;
    bool playerBLocked = false;
    bool playerAConfirmed = false;
    bool playerBConfirmed = false;
    int64_t playerAGold = 0;
    int64_t playerBGold = 0;
    std::string status;   // "active", "completed", "cancelled"
    std::string sceneName;

    [[nodiscard]] bool isActive() const { return status == "active"; }
    [[nodiscard]] bool bothLocked() const { return playerALocked && playerBLocked; }
    [[nodiscard]] bool bothConfirmed() const { return playerAConfirmed && playerBConfirmed; }
};

struct TradeOfferRecord {
    int offerId = 0;
    int slotIndex = 0;
    int inventorySourceSlot = 0;
    std::string itemInstanceId;
    int quantity = 1;
    std::string itemId;
    int enchantLevel = 0;
    bool isProtected = false;
    std::string rolledStatsJson;
    std::string itemName;
    std::string rarity;
};

class TradeRepository {
public:
    explicit TradeRepository(pqxx::connection& conn) : conn_(conn) {}

    // Sessions
    int createSession(const std::string& playerAId, const std::string& playerBId,
                      const std::string& sceneName);
    std::optional<TradeSessionRecord> loadSession(int sessionId);
    std::optional<TradeSessionRecord> getActiveSession(const std::string& characterId);
    bool isPlayerInTrade(const std::string& characterId);
    bool cancelSession(int sessionId);
    bool setPlayerLocked(int sessionId, const std::string& characterId, bool locked);
    bool setPlayerConfirmed(int sessionId, const std::string& characterId, bool confirmed);
    bool setPlayerGold(int sessionId, const std::string& characterId, int64_t gold);
    bool unlockBothPlayers(int sessionId);
    bool resetConfirms(int sessionId);

    // Offers
    bool addItemToTrade(int sessionId, const std::string& characterId, int slotIndex,
                        int sourceSlot, const std::string& instanceId, int quantity);
    bool removeItemFromTrade(int sessionId, const std::string& characterId, int slotIndex);
    bool clearPlayerOffers(int sessionId, const std::string& characterId);
    std::vector<TradeOfferRecord> getTradeOffers(int sessionId, const std::string& characterId);

    // Execution
    bool transferItem(pqxx::work& txn, const std::string& instanceId, const std::string& newOwner);
    bool updateGold(pqxx::work& txn, const std::string& characterId, int64_t delta);
    bool completeSession(pqxx::work& txn, int sessionId);

    // History
    bool logTradeHistory(int sessionId, const std::string& playerAId, const std::string& playerBId,
                         int64_t goldA, int64_t goldB,
                         const std::string& itemsAJson, const std::string& itemsBJson);

    // Cleanup
    int cleanStaleSessions(int maxAgeMinutes = 30);

private:
    pqxx::connection& conn_;
};

} // namespace fate
