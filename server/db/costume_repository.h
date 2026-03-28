#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <pqxx/pqxx>
#include "server/db/db_pool.h"

namespace fate {

struct OwnedCostume {
    std::string costumeDefId;
};

struct EquippedCostume {
    uint8_t slotType;
    std::string costumeDefId;
};

class CostumeRepository {
public:
    // Legacy: direct connection (for temp repos in async fibers)
    explicit CostumeRepository(pqxx::connection& conn) : connRef_(&conn), pool_(nullptr) {}
    // Pool-based: acquires connection per operation
    explicit CostumeRepository(DbPool& pool) : connRef_(nullptr), pool_(&pool) {}

    std::vector<OwnedCostume> loadOwnedCostumes(const std::string& characterId);
    std::vector<EquippedCostume> loadEquippedCostumes(const std::string& characterId);
    bool loadToggleState(const std::string& characterId);
    bool grantCostume(const std::string& characterId, const std::string& costumeDefId);
    bool equipCostume(const std::string& characterId, uint8_t slotType, const std::string& costumeDefId);
    bool unequipCostume(const std::string& characterId, uint8_t slotType);
    bool saveToggleState(const std::string& characterId, bool show);

private:
    pqxx::connection* connRef_ = nullptr;
    DbPool* pool_ = nullptr;

    DbPool::Guard acquireConn() {
        if (pool_) return pool_->acquire_guard();
        return DbPool::Guard::wrap(*connRef_);
    }
};

} // namespace fate
