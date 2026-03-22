#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <pqxx/pqxx>
#include "server/db/db_pool.h"

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
    // Legacy: direct connection (for temp repos in async fibers)
    explicit BankRepository(pqxx::connection& conn) : connRef_(&conn), pool_(nullptr) {}
    // Pool-based: acquires connection per operation
    explicit BankRepository(DbPool& pool) : connRef_(nullptr), pool_(&pool) {}

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
    pqxx::connection* connRef_ = nullptr;
    DbPool* pool_ = nullptr;

    DbPool::Guard acquireConn() {
        if (pool_) return pool_->acquire_guard();
        return DbPool::Guard::wrap(*connRef_);
    }
};

} // namespace fate
