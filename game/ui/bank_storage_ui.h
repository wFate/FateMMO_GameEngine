#pragma once
#include "engine/ecs/entity.h"
#include <string>

namespace fate {

// ============================================================================
// BankStorageUI — Bank deposit/withdraw interface
// ============================================================================
class BankStorageUI {
public:
    bool isOpen = false;

    void open(Entity* npc, Entity* player);
    void close();
    void render(Entity* player);

private:
    Entity* bankerNPC_ = nullptr;
    int goldInputAmount_ = 0;

    static std::string formatGold(int64_t gold);
};

} // namespace fate
