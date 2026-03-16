#pragma once
#include "engine/ecs/world.h"
#include "engine/ecs/entity.h"
#include "engine/core/types.h"
#include "game/shared/game_types.h"
#include "game/shared/item_instance.h"
#include "game/shared/inventory.h"
#include "imgui.h"
#include <string>
#include <functional>

namespace fate {

// Inventory UI — TWOM-style tabbed panel with inventory grid, equipment, stats
class InventoryUI {
public:
    static InventoryUI& instance() {
        static InventoryUI s;
        return s;
    }

    bool isOpen() const { return open_; }
    void toggle() { open_ = !open_; }
    void open() { open_ = true; }
    void close() { open_ = false; }

    // Draw the full inventory panel (call every frame)
    void draw(World* world);

    // Which tab is active
    enum class Tab { Inventory, Stats, Skills, Community, Settings };
    Tab activeTab() const { return activeTab_; }

private:
    InventoryUI() = default;

    bool open_ = false;
    Tab activeTab_ = Tab::Inventory;

    // Drag and drop state
    int dragSourceSlot_ = -1;
    bool dragFromEquipment_ = false;
    EquipmentSlot dragEquipSlot_ = EquipmentSlot::None;

    // Helper: find the local player's inventory component
    Inventory* findPlayerInventory(World* world);
    Entity* findPlayer(World* world);

    // Tab drawing
    void drawInventoryTab(Inventory* inv);
    void drawStatsTab(World* world, Entity* player);
    void drawSkillsTab(World* world, Entity* player);

    // Inventory grid and equipment
    void drawItemSlot(Inventory* inv, int slotIndex, float size);
    void drawEquipmentSlot(Inventory* inv, EquipmentSlot slot, const char* label, float size);
    void drawItemTooltip(const ItemInstance& item);

    // Rarity color
    static ImVec4 getRarityColor(ItemRarity rarity);
    static const char* getRarityName(ItemRarity rarity);
    static const char* getEquipSlotName(EquipmentSlot slot);

    // Gold formatting
    static std::string formatGold(int64_t gold);
};

} // namespace fate
