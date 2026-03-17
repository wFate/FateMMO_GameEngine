#pragma once
#include "engine/ecs/world.h"
#include "engine/spatial/spatial_grid.h"
#include "engine/memory/arena.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include "game/components/player_controller.h"
#include "game/components/sprite_component.h"
#include "engine/core/logger.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

private:
    float gameTime_ = 0.0f;
    Arena gridArena_{1024 * 1024};  // 1 MB arena for SpatialGrid cell arrays
    SpatialGrid playerGrid_;        // engine spatial grid (replaces game-layer SpatialHash)

    // Resolve a mob attack against a player entity
    void resolveAttack(Entity* mobEntity, Entity* playerEntity, EnemyStats& mobStats) {
        if (!playerEntity) return;

        auto* playerStatsComp = playerEntity->getComponent<CharacterStatsComponent>();
        if (!playerStatsComp) return;

        auto& playerStats = playerStatsComp->stats;
        if (!playerStats.isAlive()) return;

        // Roll mob damage
        int rawDamage = mobStats.rollDamage();

        // Apply armor reduction based on whether mob deals magic or physical
        int finalDamage;
        if (mobStats.dealsMagicDamage) {
            float reduction = CombatSystem::getPlayerMagicDamageReduction(
                playerStats.getMagicResist());
            finalDamage = (std::max)(1, static_cast<int>(rawDamage * (1.0f - reduction)));
        } else {
            finalDamage = CombatSystem::applyArmorReduction(rawDamage, playerStats.getArmor());
        }

        // Apply damage to player
        playerStats.takeDamage(finalDamage);

        // Track threat from this mob (useful for aggro management)
        mobStats.takeDamageFrom(mobEntity->id(), 0);  // record interaction, 0 self-damage
    }
};

} // namespace fate
