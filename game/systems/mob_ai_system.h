#pragma once
#include "engine/ecs/world.h"
#include "engine/spatial/spatial_grid.h"
#include "engine/memory/arena.h"
#include "engine/job/job_system.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include "game/components/player_controller.h"
#include "game/components/sprite_component.h"
#include "engine/core/logger.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

namespace fate {

// Ticks MobAI for all mob entities each frame with DEAR tick scaling:
//   - Builds engine SpatialGrid of alive players for O(1) nearest-player queries
//   - Distance-proportional tick rate (DEAR): closer mobs tick more frequently
//   - Calls MobAI::tick() to get desired velocity
//   - Applies movement to transform
//   - Flips sprite based on facing direction
//   - Resolves attack callbacks (mob -> player damage)
class MobAISystem : public System {
public:
    const char* name() const override { return "MobAISystem"; }

    void update(float dt) override {
        // Skip if parallel path already ran this frame
        if (parallelUpdateRan_) {
            parallelUpdateRan_ = false;
            return;
        }
        if (!world_) return;

        gameTime_ += dt;

        // Lazy-init the SpatialGrid on first frame
        if (!playerGrid_.isInitialized()) {
            // 256x256 tiles, 32px per tile, 128px cells (4-tile cells)
            playerGrid_.init(gridArena_, 256, 256, 32.0f, 128.0f);
        }

        // Count alive players for reservation hint
        uint32_t playerCount = 0;
        world_->forEach<CharacterStatsComponent, Transform>(
            [&](Entity*, CharacterStatsComponent* statsComp, Transform*) {
                if (statsComp->stats.isAlive()) {
                    playerCount++;
                }
            }
        );

        // Rebuild player spatial grid each frame
        playerGrid_.beginRebuild(playerCount);
        world_->forEach<CharacterStatsComponent, Transform>(
            [&](Entity* entity, CharacterStatsComponent* statsComp, Transform* transform) {
                if (statsComp->stats.isAlive()) {
                    playerGrid_.addEntity(entity->handle(),
                                          transform->position.x,
                                          transform->position.y);
                }
            }
        );
        playerGrid_.endRebuild();

        // Find local player position for DEAR distance calculations
        Vec2 localPlayerPos{0, 0};
        bool hasLocalPlayer = false;
        world_->forEach<Transform, PlayerController>(
            [&](Entity*, Transform* t, PlayerController* ctrl) {
                if (ctrl->isLocalPlayer) {
                    localPlayerPos = t->position;
                    hasLocalPlayer = true;
                }
            }
        );

        // Iterate every entity with MobAI + EnemyStats (DEAR tick scaling)
        world_->forEach<MobAIComponent, EnemyStatsComponent>(
            [&](Entity* entity, MobAIComponent* aiComp, EnemyStatsComponent* enemyComp) {
                auto* transform = entity->getComponent<Transform>();
                if (!transform) return;

                auto& ai    = aiComp->ai;
                auto& stats = enemyComp->stats;

                if (!stats.isAlive) return;

                Vec2 currentPos = transform->position;

                // --- DEAR tick scaling ---
                float tickInterval = 0.0f; // 0 = every frame
                if (hasLocalPlayer) {
                    float distSq = (currentPos - localPlayerPos).lengthSq();
                    float distTiles = std::sqrt(distSq) / 32.0f;

                    if (distTiles > 48.0f) {
                        return; // Fully dormant — beyond activation range
                    }

                    // DEAR formula: interval scales with distance²
                    // 3 tiles:  ~0.02s (every frame at 60fps)
                    // 10 tiles: ~0.2s  (~5 fps effective)
                    // 20 tiles: ~0.8s  (~1.3 fps)
                    // 40 tiles: ~3.1s
                    tickInterval = (distTiles * distTiles) / 512.0f;
                }

                aiComp->tickAccumulator += dt;
                if (aiComp->tickAccumulator < tickInterval) {
                    return; // Not time to tick yet
                }
                // Clamp accumulated dt to prevent teleportation on large intervals
                float aiDt = (std::min)(aiComp->tickAccumulator, 0.5f);
                aiComp->tickAccumulator = 0.0f;

                // Find nearest alive player via engine SpatialGrid
                Entity* nearestPlayer = nullptr;
                float searchRadius = (std::max)(ai.acquireRadius, ai.contactRadius);
                auto result = playerGrid_.findNearest(currentPos.x, currentPos.y, searchRadius);
                if (result.has_value()) {
                    nearestPlayer = world_->getEntity(result.value());
                }

                Vec2 targetPos;
                Vec2* targetPosPtr = nullptr;
                if (nearestPlayer) {
                    auto* playerTransform = nearestPlayer->getComponent<Transform>();
                    if (playerTransform) {
                        targetPos = playerTransform->position;
                        targetPosPtr = &targetPos;
                    }
                }

                ai.onAttackFired = [&, entity, nearestPlayer]() {
                    resolveAttack(entity, nearestPlayer, stats);
                };

                // Tick MobAI with accumulated delta time
                Vec2 velocity = ai.tick(gameTime_, aiDt, currentPos, targetPosPtr);

                transform->position += velocity * aiDt;

                auto* sprite = entity->getComponent<SpriteComponent>();
                if (sprite) {
                    Direction facing = ai.getFacingDirection();
                    sprite->flipX = (facing == Direction::Left);
                }
            }
        );
    }

    // Callback fired after each mob→player attack resolves (for network broadcast)
    std::function<void(Entity* mob, Entity* player, int damage, bool isCrit, bool isKill, bool isMiss)> onMobAttackResolved;

    // Deferred attack command — processed sequentially after parallel AI completes
    struct DeferredAttack {
        Entity* mobEntity = nullptr;
        Entity* playerEntity = nullptr;
        int rawDamage = 0;
        bool isMagic = false;
    };

    // Submit AI ticking as parallel jobs. Returns counter to wait on.
    // After waiting, call processDeferredAttacks() to apply damage.
    Counter* submitParallelUpdate(float dt) {
        if (!world_) return nullptr;

        parallelUpdateRan_ = true;
        gameTime_ += dt;

        // Spatial grid rebuild (main thread — reads all transforms)
        if (!playerGrid_.isInitialized()) {
            playerGrid_.init(gridArena_, 256, 256, 32.0f, 128.0f);
        }

        uint32_t playerCount = 0;
        world_->forEach<CharacterStatsComponent, Transform>(
            [&](Entity*, CharacterStatsComponent* statsComp, Transform*) {
                if (statsComp->stats.isAlive()) playerCount++;
            }
        );

        playerGrid_.beginRebuild(playerCount);
        world_->forEach<CharacterStatsComponent, Transform>(
            [&](Entity* entity, CharacterStatsComponent* statsComp, Transform* transform) {
                if (statsComp->stats.isAlive()) {
                    playerGrid_.addEntity(entity->handle(),
                                          transform->position.x,
                                          transform->position.y);
                }
            }
        );
        playerGrid_.endRebuild();

        // Find local player position (main thread)
        localPlayerPos_ = {0, 0};
        hasLocalPlayer_ = false;
        world_->forEach<Transform, PlayerController>(
            [&](Entity*, Transform* t, PlayerController* ctrl) {
                if (ctrl->isLocalPlayer) {
                    localPlayerPos_ = t->position;
                    hasLocalPlayer_ = true;
                }
            }
        );

        // Collect mobs to tick
        pendingMobs_.clear();
        deferredAttacks_.clear();

        world_->forEach<MobAIComponent, EnemyStatsComponent>(
            [&](Entity* entity, MobAIComponent* aiComp, EnemyStatsComponent* enemyComp) {
                auto* transform = entity->getComponent<Transform>();
                if (!transform) return;
                if (!enemyComp->stats.isAlive) return;

                // DEAR tick scaling
                float tickInterval = 0.0f;
                if (hasLocalPlayer_) {
                    float distSq = (transform->position - localPlayerPos_).lengthSq();
                    float distTiles = std::sqrt(distSq) / 32.0f;
                    if (distTiles > 48.0f) return;
                    tickInterval = (distTiles * distTiles) / 512.0f;
                }

                aiComp->tickAccumulator += dt;
                if (aiComp->tickAccumulator < tickInterval) return;

                pendingMobs_.push_back({entity, transform, aiComp, enemyComp,
                                        aiComp->tickAccumulator});
                aiComp->tickAccumulator = 0.0f;
            }
        );

        if (pendingMobs_.empty()) return nullptr;

        // Partition into groups of 4 and submit as jobs
        int groupCount = ((int)pendingMobs_.size() + 3) / 4;
        aiJobs_.resize(groupCount);
        aiJobParams_.resize(groupCount);

        // Pre-allocate deferred attacks (one slot per mob max)
        deferredAttacks_.resize(pendingMobs_.size());
        deferredAttackCount_.store(0, std::memory_order_relaxed);

        for (int g = 0; g < groupCount; ++g) {
            int start = g * 4;
            int end = (std::min)(start + 4, (int)pendingMobs_.size());
            aiJobParams_[g] = {this, start, end};
            aiJobs_[g].function = [](void* param) {
                auto* p = static_cast<AIJobParam*>(param);
                p->system->tickMobGroup(p->start, p->end);
            };
            aiJobs_[g].param = &aiJobParams_[g];
        }

        return JobSystem::instance().submit(aiJobs_.data(), groupCount);
    }

    // Process deferred attacks on main thread after parallel AI completes
    void processDeferredAttacks() {
        int count = deferredAttackCount_.load(std::memory_order_acquire);
        for (int i = 0; i < count; ++i) {
            auto& atk = deferredAttacks_[i];
            if (!atk.playerEntity) continue;

            auto* playerStatsComp = atk.playerEntity->getComponent<CharacterStatsComponent>();
            if (!playerStatsComp || !playerStatsComp->stats.isAlive()) continue;

            int finalDamage;
            if (atk.isMagic) {
                float reduction = CombatSystem::getPlayerMagicDamageReduction(
                    playerStatsComp->stats.getMagicResist());
                finalDamage = (std::max)(1, static_cast<int>(atk.rawDamage * (1.0f - reduction)));
            } else {
                finalDamage = CombatSystem::applyArmorReduction(atk.rawDamage,
                    playerStatsComp->stats.getArmor());
            }
            playerStatsComp->stats.takeDamage(finalDamage);
        }
    }

private:
    float gameTime_ = 0.0f;
    Arena gridArena_{1024 * 1024};  // 1 MB arena for SpatialGrid cell arrays
    SpatialGrid playerGrid_;        // engine spatial grid (replaces game-layer SpatialHash)
    bool parallelUpdateRan_ = false;

    // --- Parallel AI state ---
    struct MobEntry {
        Entity* entity;
        Transform* transform;
        MobAIComponent* aiComp;
        EnemyStatsComponent* enemyComp;
        float accumulatedDt;
    };

    struct AIJobParam {
        MobAISystem* system;
        int start, end;
    };

    std::vector<MobEntry> pendingMobs_;
    std::vector<Job> aiJobs_;
    std::vector<AIJobParam> aiJobParams_;
    std::vector<DeferredAttack> deferredAttacks_;
    std::atomic<int> deferredAttackCount_{0};
    Vec2 localPlayerPos_{0, 0};
    bool hasLocalPlayer_ = false;

    // Process a group of mobs (runs on a worker fiber)
    void tickMobGroup(int start, int end) {
        for (int i = start; i < end; ++i) {
            auto& mob = pendingMobs_[i];
            auto& ai = mob.aiComp->ai;
            auto& stats = mob.enemyComp->stats;

            Vec2 currentPos = mob.transform->position;
            float aiDt = (std::min)(mob.accumulatedDt, 0.5f);

            // Find nearest player
            Entity* nearestPlayer = nullptr;
            float searchRadius = (std::max)(ai.acquireRadius, ai.contactRadius);
            auto result = playerGrid_.findNearest(currentPos.x, currentPos.y, searchRadius);
            if (result.has_value()) {
                nearestPlayer = world_->getEntity(result.value());
            }

            Vec2 targetPos;
            Vec2* targetPosPtr = nullptr;
            if (nearestPlayer) {
                auto* playerTransform = nearestPlayer->getComponent<Transform>();
                if (playerTransform) {
                    targetPos = playerTransform->position;
                    targetPosPtr = &targetPos;
                }
            }

            // Defer attacks instead of resolving immediately
            ai.onAttackFired = [&, this, mobEntity = mob.entity, nearestPlayer]() {
                int idx = deferredAttackCount_.fetch_add(1, std::memory_order_relaxed);
                if (idx < (int)deferredAttacks_.size()) {
                    deferredAttacks_[idx] = {mobEntity, nearestPlayer,
                                              stats.rollDamage(), stats.dealsMagicDamage};
                }
            };

            Vec2 velocity = ai.tick(gameTime_, aiDt, currentPos, targetPosPtr);
            mob.transform->position += velocity * aiDt;

            auto* sprite = mob.entity->getComponent<SpriteComponent>();
            if (sprite) {
                Direction facing = ai.getFacingDirection();
                sprite->flipX = (facing == Direction::Left);
            }
        }
    }

    // Resolve a mob attack against a player entity
    void resolveAttack(Entity* mobEntity, Entity* playerEntity, EnemyStats& mobStats) {
        if (!playerEntity) return;

        auto* playerStatsComp = playerEntity->getComponent<CharacterStatsComponent>();
        if (!playerStatsComp) return;

        auto& playerStats = playerStatsComp->stats;
        if (!playerStats.isAlive()) return;

        // Hit/miss roll (matches Unity CombatHitRateSystem.RollMobVsPlayerHit)
        bool hit = CombatSystem::rollToHit(
            mobStats.level, static_cast<int>(mobStats.mobHitRate),
            playerStats.level, 0);

        if (!hit) {
            if (onMobAttackResolved) {
                onMobAttackResolved(mobEntity, playerEntity, 0, false, false, true);
            }
            return;
        }

        // Roll mob damage with crit
        int rawDamage = mobStats.rollDamage();
        // Crit roll: simple random check against mob's crit rate
        thread_local std::mt19937 critRng{std::random_device{}()};
        std::uniform_real_distribution<float> critDist(0.0f, 1.0f);
        bool isCrit = critDist(critRng) < mobStats.critRate;
        if (isCrit) {
            rawDamage = static_cast<int>(rawDamage * 1.95f);
        }

        // Apply armor/MR reduction based on whether mob deals magic or physical
        int finalDamage;
        if (mobStats.dealsMagicDamage) {
            float reduction = CombatSystem::getPlayerMagicDamageReduction(
                playerStats.getMagicResist());
            finalDamage = (std::max)(1, static_cast<int>(rawDamage * (1.0f - reduction)));
        } else {
            finalDamage = CombatSystem::applyArmorReduction(rawDamage, playerStats.getArmor());
        }

        // Apply damage to player
        bool wasDead = playerStats.isDead;
        playerStats.takeDamage(finalDamage);
        bool isKill = !wasDead && playerStats.isDead;

        // Fire callback for network broadcast
        if (onMobAttackResolved) {
            onMobAttackResolved(mobEntity, playerEntity, finalDamage, isCrit, isKill, false);
        }
    }
};

} // namespace fate
