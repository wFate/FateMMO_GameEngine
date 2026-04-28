#pragma once
#include <array>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace fate {

class NetClient;
class Inventory;
struct SvSkillSyncMsg;

// Minimal item-definition view used by SkillLoadout (Task 6). The full
// definition lives server-side as CachedItemDefinition; SkillLoadout only
// needs `id` to match per-itemId cooldown pokes against bound slots'
// resolved item ids. Task 11 wires `resolveItemDef` to populate this from
// the client Inventory + a client-side definition cache.
//   `displayName` mirrors the inventory ItemInstance.displayName so the
//   SkillArc can render the same first-letter glyph that InventoryPanel
//   uses when no icon is available. `iconIndex` is reserved for a future
//   icon-atlas wiring; ItemInstance does not currently carry one, so the
//   resolver always sets it to -1 today.
struct ItemDef {
    std::string id;
    std::string displayName;
    int         iconIndex = -1;
};

enum class LoadoutSlotKind : uint8_t {
    Empty = 0,
    Skill = 1,
    Item  = 2,
};

struct LoadoutSlot {
    LoadoutSlotKind kind = LoadoutSlotKind::Empty;
    std::string skillId;     // valid iff kind==Skill
    std::string instanceId;  // valid iff kind==Item -- character_inventory.instance_id
};

class SkillLoadout {
public:
    static constexpr uint8_t SLOTS_PER_PAGE = 5;
    static constexpr uint8_t PAGE_COUNT     = 4;
    static constexpr uint8_t TOTAL_SLOTS    = 20;

    SkillLoadout();

    const LoadoutSlot& slot(uint8_t flatIndex) const;
    uint8_t currentPage() const { return currentPage_; }
    void    setCurrentPage(uint8_t page);

    void assignSkill(uint8_t flatIndex, const std::string& skillId);
    void assignItem (uint8_t flatIndex, const std::string& instanceId);
    void clear      (uint8_t flatIndex);
    void swap       (uint8_t a, uint8_t b);

    void applyServerSync(const SvSkillSyncMsg& msg);

    int  resolveItemQuantity(const std::string& instanceId) const;
    bool resolveItemDef(const std::string& instanceId, ItemDef& out) const;
    float remainingCooldown(uint8_t flatIndex) const;

    void setNetClient(NetClient* nc) { netClient_ = nc; }
    void setInventory(Inventory* inv) { inventory_ = inv; }

    using ChangeCallback = std::function<void()>;
    void onChanged(ChangeCallback cb);

    // Cooldown poke — server sends SvConsumableCooldown; loadout sets per-slot end time
    // for every slot whose bound instanceId resolves to itemId.
    void applyConsumableCooldown(const std::string& itemId, uint32_t cooldownMs);

private:
    void notifyChanged();

    std::array<LoadoutSlot, TOTAL_SLOTS> slots_{};
    uint8_t currentPage_ = 0;
    NetClient* netClient_ = nullptr;
    Inventory* inventory_ = nullptr;
    std::array<uint64_t, TOTAL_SLOTS> cooldownEndsMs_{};
    std::vector<ChangeCallback> observers_;
};

} // namespace fate
