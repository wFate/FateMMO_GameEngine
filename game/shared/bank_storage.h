#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace fate {

struct StoredItem {
    std::string itemId;
    uint16_t count = 0;
};

class BankStorage {
public:
    void initialize(uint16_t maxSlots) {
        maxSlots_ = maxSlots;
        items_.clear();
        storedGold_ = 0;
    }

    bool depositGold(int64_t amount, float feePercent) {
        if (amount <= 0) return false;
        int64_t fee = static_cast<int64_t>(std::floor(amount * feePercent));
        int64_t deposited = amount - fee;
        if (deposited <= 0) return false;
        storedGold_ += deposited;
        return true;
    }

    bool withdrawGold(int64_t amount) {
        if (amount <= 0 || amount > storedGold_) return false;
        storedGold_ -= amount;
        return true;
    }

    int64_t getStoredGold() const { return storedGold_; }

    bool depositItem(const std::string& itemId, uint16_t count) {
        if (count == 0) return false;
        for (auto& item : items_) {
            if (item.itemId == itemId) {
                item.count += count;
                return true;
            }
        }
        if (isFull()) return false;
        items_.push_back({itemId, count});
        return true;
    }

    bool withdrawItem(const std::string& itemId, uint16_t count) {
        if (count == 0) return false;
        for (auto it = items_.begin(); it != items_.end(); ++it) {
            if (it->itemId == itemId) {
                if (it->count < count) return false;
                it->count -= count;
                if (it->count == 0) {
                    items_.erase(it);
                }
                return true;
            }
        }
        return false;
    }

    const std::vector<StoredItem>& getItems() const { return items_; }
    bool isFull() const { return static_cast<uint16_t>(items_.size()) >= maxSlots_; }
    uint16_t getMaxSlots() const { return maxSlots_; }

    void setSerializedState(int64_t gold, uint16_t maxSlots, std::vector<StoredItem> items) {
        storedGold_ = gold;
        maxSlots_ = maxSlots;
        items_ = std::move(items);
    }

private:
    std::vector<StoredItem> items_;
    int64_t storedGold_ = 0;
    uint16_t maxSlots_ = 30;
};

} // namespace fate
