#pragma once
#include "engine/ecs/world.h"
#include "engine/ecs/entity_handle.h"
#include "engine/net/replication.h"
#include "engine/core/types.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include <vector>
#include <string>
#include <cmath>

namespace fate {

struct AOETarget {
    EntityHandle handle;
    uint64_t persistentId = 0;
    bool isPlayer = false;
};

// Gather enemy entities within a circle (for AreaAroundSelf / AreaAtTarget)
std::vector<AOETarget> gatherCircleTargets(
    World& world, ReplicationManager& repl,
    Vec2 center, float radiusPixels,
    uint32_t excludeEntityId,
    const std::string& sceneId);

// Gather enemy entities within a cone (for Cone target type)
std::vector<AOETarget> gatherConeTargets(
    World& world, ReplicationManager& repl,
    Vec2 casterPos, Vec2 targetPos,
    float lengthPixels, float halfAngleRadians,
    uint32_t excludeEntityId,
    const std::string& sceneId);

// Gather enemy entities in a line (for Line target type / Piercing Shot)
std::vector<AOETarget> gatherLineTargets(
    World& world, ReplicationManager& repl,
    Vec2 casterPos, Vec2 targetPos,
    float lengthPixels, float widthPixels,
    uint32_t excludeEntityId,
    const std::string& sceneId);

// Gather party members within a circle (for party buffs/heals/resurrection)
// If includeDead=true, includes dead players (for resurrection)
// If includeDead=false, only alive players (for buffs/heals)
std::vector<AOETarget> gatherPartyTargets(
    World& world, ReplicationManager& repl,
    Vec2 center, float radiusPixels,
    uint32_t casterEntityId,
    bool includeDead);

} // namespace fate
