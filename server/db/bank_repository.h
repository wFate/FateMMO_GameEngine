#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <pqxx/pqxx>

namespace fate {

struct BankSlotRecord {
    int slotIndex = 0;
    std::string itemId;
    int quantity = 1;
    std::string rolledStatsJson;
    std::string socketStat;
    int socketValue = 0;
    int enchantLevel = 0;
    bool isProtected = false;
    std::string instanceId;  // UUID as string
};

class BankRepository {
public:
    explicit BankRepository(pqxx::connection& conn) : conn_(conn) {}

    // Items
    std::vector<BankSlotRecord> loadBankItems(const std::string& characterId);
    bool depositItem(const std::string& characterId, int slotIndex, const std::string& itemId,
                     int quantity, const std::string& rolledStats, const std::string& socketStat,
                     int socketValue, int enchantLevel, bool isProtected);
    bool withdrawItem(const std::string& characterId, int slotIndex);
    bool saveBankItems(const std::string& characterId, const std::vector<BankSlotRecord>& items);

    // Gold
    int64_t loadBankGold(const std::string& characterId);
    bool depositGold(const std::string& characterId, int64_t amount);
    bool withdrawGold(const std::string& characterId, int64_t amount);

private:
    pqxx::connection& conn_;
};

} // namespace fate
