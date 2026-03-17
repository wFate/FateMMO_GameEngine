#pragma once
#include "engine/ecs/entity.h"
#include <string>

namespace fate {

// ============================================================================
// TeleporterUI — Destination selection for teleporter NPCs
// ============================================================================
class TeleporterUI {
public:
    bool isOpen = false;
    Entity* teleporterNPC = nullptr;

    void open(Entity* npc);
    void close();
    void render(Entity* player);

private:
    static std::string formatGold(int64_t gold);
};

} // namespace fate
