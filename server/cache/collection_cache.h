#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace fate {

struct CachedCollection {
    uint32_t collectionId = 0;
    std::string name;
    std::string description;
    std::string category;        // "Items", "Combat", "Progression"
    std::string conditionType;   // "OwnItemRarity", "TotalMobKills", etc.
    std::string conditionTarget; // mob_def_id, item_id, rarity, etc.
    int32_t conditionValue = 1;
    std::string rewardType;      // "STR", "Damage", "MoveSpeed", etc.
    float rewardValue = 0.0f;
};

} // namespace fate

// CollectionCache requires pqxx (server-only dependency)
#if __has_include(<pqxx/pqxx>)
#include <pqxx/pqxx>

namespace fate {

class CollectionCache {
public:
    bool loadFromDatabase(pqxx::connection& conn) {
        try {
            pqxx::work txn(conn);
            auto result = txn.exec(
                "SELECT collection_id, name, description, category, "
                "condition_type, condition_target, condition_value, "
                "reward_type, reward_value FROM collection_definitions"
            );
            for (const auto& row : result) {
                CachedCollection c;
                c.collectionId    = row["collection_id"].as<uint32_t>();
                c.name            = row["name"].as<std::string>();
                c.description     = row["description"].is_null() ? "" : row["description"].as<std::string>();
                c.category        = row["category"].as<std::string>();
                c.conditionType   = row["condition_type"].as<std::string>();
                c.conditionTarget = row["condition_target"].is_null() ? "" : row["condition_target"].as<std::string>();
                c.conditionValue  = row["condition_value"].as<int32_t>();
                c.rewardType      = row["reward_type"].as<std::string>();
                c.rewardValue     = row["reward_value"].as<float>();
                collections_[c.collectionId] = c;
            }
            txn.commit();
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    const CachedCollection* get(uint32_t id) const {
        auto it = collections_.find(id);
        return it != collections_.end() ? &it->second : nullptr;
    }

    std::vector<const CachedCollection*> getByConditionType(const std::string& type) const {
        std::vector<const CachedCollection*> result;
        for (const auto& [id, c] : collections_) {
            if (c.conditionType == type) result.push_back(&c);
        }
        return result;
    }

    std::vector<const CachedCollection*> getByCategory(const std::string& cat) const {
        std::vector<const CachedCollection*> result;
        for (const auto& [id, c] : collections_) {
            if (c.category == cat) result.push_back(&c);
        }
        return result;
    }

    const std::unordered_map<uint32_t, CachedCollection>& all() const { return collections_; }
    size_t size() const { return collections_.size(); }
    void reload(pqxx::connection& conn) { collections_.clear(); loadFromDatabase(conn); }
    void clear() { collections_.clear(); }

private:
    std::unordered_map<uint32_t, CachedCollection> collections_;
};

} // namespace fate

#endif // __has_include(<pqxx/pqxx>)
