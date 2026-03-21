#pragma once
#include <string>
#include <vector>
#include <pqxx/pqxx>

namespace fate {

struct InventorySlotRecord {
    std::string instance_id;     // UUID
    std::string character_id;
    std::string item_id;
    int slot_index = -1;         // -1 = not in main inventory
    int bag_slot_index = -1;     // -1 = not in bag
    int bag_item_slot = -1;
    std::string rolled_stats;    // JSON string (stored as jsonb in DB)
    std::string socket_stat;
    int socket_value = 0;
    int enchant_level = 0;
    bool is_protected = false;
    bool is_soulbound = false;
    bool is_broken = false;
    bool is_equipped = false;
    std::string equipped_slot;
    int quantity = 1;
};

class InventoryRepository {
public:
    explicit InventoryRepository(pqxx::connection& conn) : conn_(conn) {}

    // Load all inventory items for a character
    std::vector<InventorySlotRecord> loadInventory(const std::string& characterId);

    // Save entire inventory (delete existing, re-insert all).
    // Simple approach for Phase 7 — optimize to upserts later if needed.
    bool saveInventory(const std::string& characterId, const std::vector<InventorySlotRecord>& slots);

private:
    pqxx::connection& conn_;
};

} // namespace fate
