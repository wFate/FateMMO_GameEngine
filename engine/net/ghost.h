#pragma once
#include "engine/ecs/entity_handle.h"
#include "engine/ecs/component_registry.h"
#include "engine/memory/arena.h"

namespace fate {

struct GhostFlag {
    FATE_COMPONENT_COLD(GhostFlag)
    EntityHandle sourceHandle;
    uint16_t sourceZoneId = 0;
};

class GhostArena {
public:
    explicit GhostArena(size_t reserve = 16 * 1024 * 1024) : arena_(reserve) {}
    Arena& arena() { return arena_; }
    void reset() { arena_.reset(); }
private:
    Arena arena_;
};

} // namespace fate
