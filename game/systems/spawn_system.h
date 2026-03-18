#pragma once
#include "engine/ecs/world.h"
#include "engine/ecs/reflect.h"
#include "game/shared/spawn_zone.h"
#include "game/entity_factory.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "engine/core/logger.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/camera.h"

#include <random>
#include <algorithm>
#include <cmath>

namespace fate {

// ============================================================================
// SpawnZoneComponent — attached to a zone entity to define mob spawning rules.
// The entity's Transform provides the zone center position.
// Size defines the rectangular bounds mobs spawn and roam within.
// ============================================================================
struct SpawnZoneComponent {
    FATE_COMPONENT(SpawnZoneComponent)
    SpawnZoneConfig config;
    std::vector<TrackedMob> trackedMobs;
    float nextTickTime = 0.0f;
    bool initialized = false;
    bool showBounds = false;        // Draw zone outline in editor/debug (toggle in inspector)

    // Get world-space bounds using entity position as center
    Rect getBounds(const Vec2& entityPos) const {
        return {
            entityPos.x - config.size.x * 0.5f,
            entityPos.y - config.size.y * 0.5f,
            config.size.x,
            config.size.y
        };
    }
};

// ============================================================================
// SpawnSystem — iterates SpawnZoneComponents to spawn, track, and respawn mobs.
// Uses the entity's Transform position as zone center (draggable in editor).
// Constrains mob roaming to zone bounds and returns mobs to zone after leash.
// ============================================================================
class SpawnSystem : public System {
public:
    const char* name() const override { return "SpawnSystem"; }

    void update(float dt) override {
        gameTime_ += dt;

        // Collect zone entity handles first — spawning new entities during
        // forEach invalidates archetype iterators (causes crash with large worlds).
        std::vector<EntityHandle> zoneHandles;
        world_->forEach<Transform, SpawnZoneComponent>(
            [&](Entity* zoneEntity, Transform*, SpawnZoneComponent*) {
                zoneHandles.push_back(zoneEntity->handle());
            }
        );

        // Now process each zone outside the forEach
        for (auto handle : zoneHandles) {
            Entity* zoneEntity = world_->getEntity(handle);
            if (!zoneEntity) continue;
            auto* zoneTransform = zoneEntity->getComponent<Transform>();
            auto* sz = zoneEntity->getComponent<SpawnZoneComponent>();
            if (!zoneTransform || !sz) continue;

            // Sync zone center from transform (so dragging entity in editor moves zone)
            sz->config.position = zoneTransform->position;

            // First-tick initialization: fill zone with mobs
            if (!sz->initialized) {
                spawnMissingToTargets(*world_, *sz, gameTime_);
                sz->initialized = true;
                sz->nextTickTime = gameTime_ + sz->config.serverTickInterval;
                continue;
            }

            // Throttled tick
            if (gameTime_ < sz->nextTickTime) continue;
            sz->nextTickTime = gameTime_ + sz->config.serverTickInterval;

            detectDeaths(*world_, *sz, gameTime_);
            processRespawns(*world_, *sz, gameTime_);
            constrainMobsToZone(*world_, *sz);
            spawnMissingToTargets(*world_, *sz, gameTime_);
        }
    }

    // ------------------------------------------------------------------
    // Debug rendering — draw zone boundaries in world space
    // ------------------------------------------------------------------
    void renderDebug(SpriteBatch& batch, Camera& camera) {
        if (!world_) return;

        Mat4 vp = camera.getViewProjection();
        batch.begin(vp);

        world_->forEach<Transform, SpawnZoneComponent>(
            [&](Entity*, Transform* t, SpawnZoneComponent* sz) {
                if (!sz->showBounds) return;

                Rect bounds = sz->getBounds(t->position);
                Color color(0.2f, 0.8f, 0.2f, 0.4f); // Green outline
                float thick = 1.5f;

                // Top
                batch.drawRect({bounds.x + bounds.w * 0.5f, bounds.y + bounds.h}, {bounds.w, thick}, color, 94.0f);
                // Bottom
                batch.drawRect({bounds.x + bounds.w * 0.5f, bounds.y}, {bounds.w, thick}, color, 94.0f);
                // Left
                batch.drawRect({bounds.x, bounds.y + bounds.h * 0.5f}, {thick, bounds.h}, color, 94.0f);
                // Right
                batch.drawRect({bounds.x + bounds.w, bounds.y + bounds.h * 0.5f}, {thick, bounds.h}, color, 94.0f);

                // Zone name label at top-center (using drawRect for visibility without text)
                // Corner markers for easier selection
                float m = 4.0f;
                Color corner(0.3f, 1.0f, 0.3f, 0.7f);
                batch.drawRect({bounds.x, bounds.y}, {m, m}, corner, 94.5f);
                batch.drawRect({bounds.x + bounds.w, bounds.y}, {m, m}, corner, 94.5f);
                batch.drawRect({bounds.x, bounds.y + bounds.h}, {m, m}, corner, 94.5f);
                batch.drawRect({bounds.x + bounds.w, bounds.y + bounds.h}, {m, m}, corner, 94.5f);
            }
        );

        batch.end();
    }

private:
    float gameTime_ = 0.0f;

    static std::mt19937& rng() {
        thread_local std::mt19937 gen{std::random_device{}()};
        return gen;
    }

    // ------------------------------------------------------------------
    // spawnMissingToTargets — top-up each rule to its target count
    // ------------------------------------------------------------------
    void spawnMissingToTargets(World& world, SpawnZoneComponent& sz, float /*gameTime*/) {
        auto& config = sz.config;

        for (int ri = 0; ri < (int)config.rules.size(); ++ri) {
            auto& rule = config.rules[ri];

            // Count alive tracked mobs for this rule
            int aliveCount = 0;
            for (auto& tm : sz.trackedMobs) {
                if (tm.ruleIndex == ri && tm.lastAlive && tm.respawnAt < 0.0f) {
                    Entity* e = world.getEntity(tm.entityId);
                    if (e) ++aliveCount;
                }
            }

            int toSpawn = rule.targetCount - aliveCount;
            for (int i = 0; i < toSpawn; ++i) {
                std::uniform_int_distribution<int> levelDist(rule.minLevel, rule.maxLevel);
                int level = levelDist(rng());

                Vec2 spawnPos = getRandomSpawnPosition(sz, world);

                Entity* mob = EntityFactory::createMob(
                    world, rule.enemyId, level,
                    rule.baseHP, rule.baseDamage, spawnPos,
                    rule.isAggressive, rule.isBoss
                );

                if (!mob) continue;

                // Configure mob AI to respect zone bounds
                if (auto* aiComp = mob->getComponent<MobAIComponent>()) {
                    // Roam radius = half the zone's smaller dimension (mobs stay in zone)
                    float zoneRoamRadius = (config.size.x < config.size.y
                        ? config.size.x : config.size.y) * 0.4f;
                    aiComp->ai.roamRadius = zoneRoamRadius;

                    // Non-aggressive mobs: stationary
                    if (!rule.isAggressive) {
                        aiComp->ai.canRoam = false;
                        aiComp->ai.canChase = false;
                        aiComp->ai.roamWhileIdle = false;
                    }
                }

                // Track
                TrackedMob tm;
                tm.entityId = mob->id();
                tm.enemyId = rule.enemyId;
                tm.ruleIndex = ri;
                tm.level = level;
                tm.lastAlive = true;
                tm.respawnAt = -1.0f;
                tm.homePosition = spawnPos;
                sz.trackedMobs.push_back(tm);

                LOG_DEBUG("Spawn", "Spawned %s Lv%d at (%.0f, %.0f) in zone '%s'",
                          rule.enemyId.c_str(), level, spawnPos.x, spawnPos.y,
                          config.zoneName.c_str());
            }
        }
    }

    // ------------------------------------------------------------------
    // detectDeaths — check each tracked mob for alive -> dead transitions
    // ------------------------------------------------------------------
    void detectDeaths(World& world, SpawnZoneComponent& sz, float gameTime) {
        auto& config = sz.config;

        for (auto& tm : sz.trackedMobs) {
            Entity* entity = world.getEntity(tm.entityId);
            if (!entity) {
                if (tm.lastAlive) {
                    tm.lastAlive = false;
                    if (tm.ruleIndex >= 0 && tm.ruleIndex < (int)config.rules.size())
                        tm.respawnAt = gameTime + config.rules[tm.ruleIndex].respawnSeconds;
                }
                continue;
            }

            auto* enemyComp = entity->getComponent<EnemyStatsComponent>();
            if (!enemyComp) continue;

            bool alive = enemyComp->stats.isAlive;

            if (tm.lastAlive && !alive) {
                tm.lastAlive = false;
                if (tm.ruleIndex >= 0 && tm.ruleIndex < (int)config.rules.size()) {
                    tm.respawnAt = gameTime + config.rules[tm.ruleIndex].respawnSeconds;
                    LOG_DEBUG("Spawn", "%s died, respawn in %.0fs",
                              tm.enemyId.c_str(),
                              config.rules[tm.ruleIndex].respawnSeconds);
                }
            }

            tm.lastAlive = alive;
        }
    }

    // ------------------------------------------------------------------
    // processRespawns — respawn mobs whose timer has expired
    // ------------------------------------------------------------------
    void processRespawns(World& world, SpawnZoneComponent& sz, float gameTime) {
        for (auto& tm : sz.trackedMobs) {
            if (tm.respawnAt < 0.0f) continue;
            if (gameTime < tm.respawnAt) continue;

            Entity* entity = world.getEntity(tm.entityId);
            if (!entity) {
                tm.respawnAt = -1.0f;
                continue;
            }

            Vec2 newPos = getRandomSpawnPosition(sz, world);

            auto* enemyComp = entity->getComponent<EnemyStatsComponent>();
            if (enemyComp) enemyComp->stats.respawn();

            auto* transform = entity->getComponent<Transform>();
            if (transform) transform->position = newPos;

            auto* sprite = entity->getComponent<SpriteComponent>();
            if (sprite) sprite->enabled = true;

            auto* aiComp = entity->getComponent<MobAIComponent>();
            if (aiComp) aiComp->ai.onRespawned(newPos);

            tm.homePosition = newPos;
            tm.lastAlive = true;
            tm.respawnAt = -1.0f;

            LOG_INFO("Spawn", "%s respawned at (%.0f, %.0f) in '%s'",
                     tm.enemyId.c_str(), newPos.x, newPos.y,
                     sz.config.zoneName.c_str());
        }
    }

    // ------------------------------------------------------------------
    // constrainMobsToZone — push mobs back toward zone when they've
    // returned from chasing a player (ReturnHome mode should bring
    // them back, but we also clamp their home position to zone bounds)
    // ------------------------------------------------------------------
    void constrainMobsToZone(World& world, SpawnZoneComponent& sz) {
        Rect bounds = sz.getBounds(sz.config.position);

        for (auto& tm : sz.trackedMobs) {
            if (!tm.lastAlive) continue;

            Entity* entity = world.getEntity(tm.entityId);
            if (!entity) continue;

            auto* aiComp = entity->getComponent<MobAIComponent>();
            if (!aiComp) continue;

            // If mob is in Idle or Roam mode, ensure its home position is inside zone
            AIMode mode = aiComp->ai.getMode();
            if (mode == AIMode::Idle || mode == AIMode::Roam) {
                auto* transform = entity->getComponent<Transform>();
                if (!transform) continue;

                // Check if home position drifted outside zone (shouldn't happen, but safety)
                Vec2 home = tm.homePosition;
                bool homeOutside = home.x < bounds.x || home.x > bounds.x + bounds.w ||
                                   home.y < bounds.y || home.y > bounds.y + bounds.h;

                if (homeOutside) {
                    // Clamp home back inside zone
                    Vec2 clamped;
                    clamped.x = home.x < bounds.x ? bounds.x + 16.0f :
                               (home.x > bounds.x + bounds.w ? bounds.x + bounds.w - 16.0f : home.x);
                    clamped.y = home.y < bounds.y ? bounds.y + 16.0f :
                               (home.y > bounds.x + bounds.h ? bounds.y + bounds.h - 16.0f : home.y);
                    tm.homePosition = clamped;
                    aiComp->ai.onRespawned(clamped); // Reset AI home
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // getRandomSpawnPosition — random point inside zone with min distance
    // ------------------------------------------------------------------
    Vec2 getRandomSpawnPosition(SpawnZoneComponent& sz, World& world) {
        auto& config = sz.config;
        float halfW = config.size.x * 0.5f;
        float halfH = config.size.y * 0.5f;

        std::uniform_real_distribution<float> distX(config.position.x - halfW, config.position.x + halfW);
        std::uniform_real_distribution<float> distY(config.position.y - halfH, config.position.y + halfH);

        float minDistSq = config.minSpawnDistance * config.minSpawnDistance;

        for (int attempt = 0; attempt < config.maxSpawnAttempts; ++attempt) {
            Vec2 candidate = {distX(rng()), distY(rng())};

            bool tooClose = false;
            for (auto& tm : sz.trackedMobs) {
                if (!tm.lastAlive || tm.respawnAt >= 0.0f) continue;
                Entity* e = world.getEntity(tm.entityId);
                if (!e) continue;
                auto* t = e->getComponent<Transform>();
                if (!t) continue;
                if ((t->position - candidate).lengthSq() < minDistSq) {
                    tooClose = true;
                    break;
                }
            }

            if (!tooClose) return candidate;
        }

        return {distX(rng()), distY(rng())};
    }
};

} // namespace fate

// SpawnZoneComponent has complex inner types (SpawnZoneConfig, vector<TrackedMob>) — custom serializer in Task 6
// Only reflect the simple debug toggle field
FATE_REFLECT(fate::SpawnZoneComponent,
    FATE_FIELD(showBounds, Bool)
)
