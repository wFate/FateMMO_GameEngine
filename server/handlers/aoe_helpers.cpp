#include "server/handlers/aoe_helpers.h"
#include "engine/ecs/persistent_id.h"
#include "engine/core/logger.h"

namespace fate {

// ============================================================================
// gatherCircleTargets — enemies (mobs + PvP players) within a circle
// ============================================================================
std::vector<AOETarget> gatherCircleTargets(
    World& world, ReplicationManager& repl,
    Vec2 center, float radiusPixels,
    uint32_t excludeEntityId,
    const std::string& sceneId)
{
    std::vector<AOETarget> results;
    const float radiusSq = radiusPixels * radiusPixels;

    // Gather mobs
    world.forEach<EnemyStatsComponent, Transform>(
        [&](Entity* entity, EnemyStatsComponent* enemyComp, Transform* transform) {
            if (entity->id() == excludeEntityId) return;
            if (!enemyComp->stats.isAlive) return;
            if (!sceneId.empty() && enemyComp->stats.sceneId != sceneId) return;

            float distSq = (transform->position - center).lengthSq();
            if (distSq > radiusSq) return;

            PersistentId pid = repl.getPersistentId(entity->handle());
            AOETarget t;
            t.handle = entity->handle();
            t.persistentId = pid.value();
            t.isPlayer = false;
            results.push_back(t);
        });

    // Gather PvP player targets
    world.forEach<CharacterStatsComponent, Transform>(
        [&](Entity* entity, CharacterStatsComponent* statsComp, Transform* transform) {
            if (entity->id() == excludeEntityId) return;
            if (!statsComp->stats.isAlive()) return;
            if (!sceneId.empty() && statsComp->stats.currentScene != sceneId) return;

            float distSq = (transform->position - center).lengthSq();
            if (distSq > radiusSq) return;

            PersistentId pid = repl.getPersistentId(entity->handle());
            AOETarget t;
            t.handle = entity->handle();
            t.persistentId = pid.value();
            t.isPlayer = true;
            results.push_back(t);
        });

    return results;
}

// ============================================================================
// gatherConeTargets — enemies within a cone from caster toward target
// ============================================================================
std::vector<AOETarget> gatherConeTargets(
    World& world, ReplicationManager& repl,
    Vec2 casterPos, Vec2 targetPos,
    float lengthPixels, float halfAngleRadians,
    uint32_t excludeEntityId,
    const std::string& sceneId)
{
    std::vector<AOETarget> results;
    const float lengthSq = lengthPixels * lengthPixels;

    Vec2 dir = (targetPos - casterPos).normalized();
    // If caster and target overlap, default to facing right
    if (dir.lengthSq() < 0.0001f) {
        dir = Vec2(1.0f, 0.0f);
    }

    float cosHalf = std::cos(halfAngleRadians);

    auto checkEntity = [&](Entity* entity, Vec2 entityPos, bool alive, const std::string& entityScene, bool isPlayer) {
        if (entity->id() == excludeEntityId) return;
        if (!alive) return;
        if (!sceneId.empty() && entityScene != sceneId) return;

        Vec2 toEntity = entityPos - casterPos;
        float distSq = toEntity.lengthSq();
        if (distSq > lengthSq || distSq < 0.0001f) return;

        // Normalize toEntity and check angle via dot product
        float invLen = 1.0f / std::sqrt(distSq);
        Vec2 toEntityDir(toEntity.x * invLen, toEntity.y * invLen);
        float dot = dir.dot(toEntityDir);
        if (dot < cosHalf) return;

        PersistentId pid = repl.getPersistentId(entity->handle());
        AOETarget t;
        t.handle = entity->handle();
        t.persistentId = pid.value();
        t.isPlayer = isPlayer;
        results.push_back(t);
    };

    // Gather mobs
    world.forEach<EnemyStatsComponent, Transform>(
        [&](Entity* entity, EnemyStatsComponent* enemyComp, Transform* transform) {
            checkEntity(entity, transform->position, enemyComp->stats.isAlive,
                        enemyComp->stats.sceneId, false);
        });

    // Gather PvP player targets
    world.forEach<CharacterStatsComponent, Transform>(
        [&](Entity* entity, CharacterStatsComponent* statsComp, Transform* transform) {
            checkEntity(entity, transform->position, statsComp->stats.isAlive(),
                        statsComp->stats.currentScene, true);
        });

    return results;
}

// ============================================================================
// gatherLineTargets — enemies along a line from caster toward target
// ============================================================================
std::vector<AOETarget> gatherLineTargets(
    World& world, ReplicationManager& repl,
    Vec2 casterPos, Vec2 targetPos,
    float lengthPixels, float widthPixels,
    uint32_t excludeEntityId,
    const std::string& sceneId)
{
    std::vector<AOETarget> results;
    const float halfWidth = widthPixels * 0.5f;

    Vec2 dir = (targetPos - casterPos).normalized();
    // If caster and target overlap, default to facing right
    if (dir.lengthSq() < 0.0001f) {
        dir = Vec2(1.0f, 0.0f);
    }

    auto checkEntity = [&](Entity* entity, Vec2 entityPos, bool alive, const std::string& entityScene, bool isPlayer) {
        if (entity->id() == excludeEntityId) return;
        if (!alive) return;
        if (!sceneId.empty() && entityScene != sceneId) return;

        Vec2 toEntity = entityPos - casterPos;

        // Project onto line direction
        float projDist = toEntity.dot(dir);
        if (projDist < 0.0f || projDist > lengthPixels) return;

        // Perpendicular distance (2D cross product magnitude)
        float perpDist = std::abs(toEntity.x * dir.y - toEntity.y * dir.x);
        if (perpDist > halfWidth) return;

        PersistentId pid = repl.getPersistentId(entity->handle());
        AOETarget t;
        t.handle = entity->handle();
        t.persistentId = pid.value();
        t.isPlayer = isPlayer;
        results.push_back(t);
    };

    // Gather mobs
    world.forEach<EnemyStatsComponent, Transform>(
        [&](Entity* entity, EnemyStatsComponent* enemyComp, Transform* transform) {
            checkEntity(entity, transform->position, enemyComp->stats.isAlive,
                        enemyComp->stats.sceneId, false);
        });

    // Gather PvP player targets
    world.forEach<CharacterStatsComponent, Transform>(
        [&](Entity* entity, CharacterStatsComponent* statsComp, Transform* transform) {
            checkEntity(entity, transform->position, statsComp->stats.isAlive(),
                        statsComp->stats.currentScene, true);
        });

    return results;
}

// ============================================================================
// gatherPartyTargets — party members within a circle (buffs/heals/res)
// ============================================================================
std::vector<AOETarget> gatherPartyTargets(
    World& world, ReplicationManager& repl,
    Vec2 center, float radiusPixels,
    uint32_t casterEntityId,
    bool includeDead)
{
    std::vector<AOETarget> results;
    const float radiusSq = radiusPixels * radiusPixels;

    // Find the caster's party ID
    Entity* casterEntity = world.getEntity(EntityHandle(casterEntityId));
    if (!casterEntity) return results;

    auto* casterParty = casterEntity->getComponent<PartyComponent>();
    if (!casterParty || !casterParty->party.isInParty()) return results;

    int casterPartyId = casterParty->party.partyId;

    // Use 2-component forEach (CharacterStatsComponent + Transform),
    // then manually check for PartyComponent on each entity
    world.forEach<CharacterStatsComponent, Transform>(
        [&](Entity* entity, CharacterStatsComponent* statsComp, Transform* transform) {
            // Check distance
            float distSq = (transform->position - center).lengthSq();
            if (distSq > radiusSq) return;

            // Check party membership
            auto* partyComp = entity->getComponent<PartyComponent>();
            if (!partyComp || !partyComp->party.isInParty()) return;
            if (partyComp->party.partyId != casterPartyId) return;

            // Filter by alive/dead status
            if (includeDead) {
                // Resurrection: only include dead players
                if (statsComp->stats.isAlive()) return;
            } else {
                // Buffs/heals: only include alive players (including caster)
                if (!statsComp->stats.isAlive()) return;
            }

            PersistentId pid = repl.getPersistentId(entity->handle());
            AOETarget t;
            t.handle = entity->handle();
            t.persistentId = pid.value();
            t.isPlayer = true;
            results.push_back(t);
        });

    return results;
}

} // namespace fate
