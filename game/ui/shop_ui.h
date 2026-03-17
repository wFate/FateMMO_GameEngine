#pragma once
#include "engine/ecs/entity.h"
#include <string>

namespace fate {

// ============================================================================
// ShopUI — NPC merchant buy/sell interface
// ============================================================================
class ShopUI {
public:
    bool isOpen = false;
    Entity* shopNPC = nullptr;

    void open(Entity* npc);
    void close();
    void render(Entity* player);

private:
    int selectedBuyIndex_ = -1;
    int selectedSellSlot_ = -1;

    static std::string formatGold(int64_t gold);
};

} // namespace fate
