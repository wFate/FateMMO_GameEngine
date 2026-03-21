#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "game/shared/game_types.h"
#include "game/shared/item_instance.h"
#include "game/shared/enchant_system.h"

namespace fate {

// ============================================================================
// Inventory — Server-authoritative player inventory management
// ============================================================================
class Inventory {
public:
    Inventory();

    // ---- Initialization ----------------------------------------------------
    void initialize(const std::string& charId, int64_t startGold);

    // ---- Properties --------------------------------------------------------
    [[nodiscard]] int totalSlots() const;
    [[nodiscard]] int usedSlots() const;
    [[nodiscard]] int freeSlots() const;
    [[nodiscard]] int64_t getGold() const;

    // ---- Item Operations ---------------------------------------------------
    bool addItem(const ItemInstance& item);
    bool addItemToSlot(int slotIndex, const ItemInstance& item);
    bool removeItem(int slotIndex);
    bool removeItemQuantity(int slotIndex, int quantity);
    [[nodiscard]] ItemInstance getSlot(int index) const;

    // ---- Equipment ---------------------------------------------------------
    [[nodiscard]] ItemInstance getEquipment(EquipmentSlot slot) const;
    bool equipItem(int inventorySlot, EquipmentSlot targetSlot);
    bool unequipItem(EquipmentSlot slot);
    bool setEquipment(EquipmentSlot slot, const ItemInstance& item);

    // ---- Slot Movement -----------------------------------------------------
    bool moveItem(int fromSlot, int toSlot);

    // ---- Gold --------------------------------------------------------------
    bool addGold(int64_t amount);
    bool removeGold(int64_t amount);
    void setGold(int64_t amount) { gold_ = (amount >= 0) ? amount : 0; }

    // ---- Trade Locking -----------------------------------------------------
    void lockSlotForTrade(int slot);
    void unlockSlotForTrade(int slot);
    void unlockAllTradeSlots();
    [[nodiscard]] bool isSlotLocked(int slot) const;

    // ---- Search / Query ----------------------------------------------------
    [[nodiscard]] int findItemById(const std::string& itemId) const;
    [[nodiscard]] int findByInstanceId(const std::string& instanceId) const;
    [[nodiscard]] int countItem(const std::string& itemId) const;

    // ---- Callbacks ---------------------------------------------------------
    std::function<void()> onInventoryChanged;
    std::function<void()> onGoldChanged;
    std::function<void(EquipmentSlot)> onEquipmentChanged;

    // Serialization support
    [[nodiscard]] const std::vector<ItemInstance>& getSlots() const { return slots_; }
    [[nodiscard]] const std::unordered_map<EquipmentSlot, ItemInstance>& getEquipmentMap() const { return equipment_; }
    void setSerializedState(int64_t gold, std::vector<ItemInstance> slots,
                            std::unordered_map<EquipmentSlot, ItemInstance> equipment) {
        gold_ = gold;
        slots_ = std::move(slots);
        equipment_ = std::move(equipment);
    }

private:
    std::vector<ItemInstance> slots_;
    std::unordered_map<EquipmentSlot, ItemInstance> equipment_;
    int64_t gold_ = 0;
    std::string characterId_;
    std::unordered_set<int> lockedTradeSlots_;

    // ---- Helpers -----------------------------------------------------------
    [[nodiscard]] bool isValidSlot(int index) const;
    [[nodiscard]] int findFirstEmptySlot() const;
};

} // namespace fate
