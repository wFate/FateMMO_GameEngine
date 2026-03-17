#pragma once
#include "engine/ecs/entity.h"

namespace fate {

// ============================================================================
// QuestLogUI — Tracks active quests and objective progress
// ============================================================================
class QuestLogUI {
public:
    bool isOpen = false;

    void toggle() { isOpen = !isOpen; }
    void render(Entity* player);

private:
    bool showCompleted_ = false;
};

} // namespace fate
