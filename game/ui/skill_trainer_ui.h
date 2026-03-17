#pragma once
#include "engine/ecs/entity.h"
#include "game/shared/game_types.h"

namespace fate {

// ============================================================================
// SkillTrainerUI — NPC skill training interface
// ============================================================================
class SkillTrainerUI {
public:
    bool isOpen = false;
    Entity* trainerNPC = nullptr;

    void open(Entity* npc);
    void close();
    void render(Entity* player);

private:
    int selectedSkillIndex_ = -1;

    static const char* getClassName(ClassType type);
    static std::string formatGold(int64_t gold);
};

} // namespace fate
