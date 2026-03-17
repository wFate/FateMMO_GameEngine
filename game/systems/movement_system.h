#pragma once
#include "engine/ecs/world.h"
#include "engine/input/input.h"
#include "game/components/transform.h"
#include "game/components/player_controller.h"
#include "game/components/sprite_component.h"
#include "game/components/animator.h"
#include "game/components/box_collider.h"
#include "engine/core/logger.h"

namespace fate {

// Handles player input -> movement with AABB collision (TWOM-style cardinal only)
class MovementSystem : public System {
public:
    const char* name() const override { return "MovementSystem"; }

    void update(float dt) override {
        LOG_INFO("TICK", "MovementSystem::update");
        auto& input = Input::instance();
        Direction dir = input.getCardinalDirection();

        world_->forEach<Transform, PlayerController>(
            [&](Entity* entity, Transform* transform, PlayerController* ctrl) {
                // Only the local player responds to input
                // Other entities with PlayerController (prefab copies, other players)
                // will be controlled by the server/AI, not keyboard input
                if (!ctrl->isLocalPlayer) return;

                ctrl->isMoving = (dir != Direction::None);

                if (ctrl->isMoving) {
                    ctrl->facing = dir;
                    Vec2 move = directionToVec(dir) * ctrl->moveSpeed * dt;

                    Vec2 newPos = transform->position + move;
                    bool blocked = false;

                    // Get player's collision shape
                    auto* myBox = entity->getComponent<BoxCollider>();
                    auto* myPoly = entity->getComponent<PolygonCollider>();

                    if (myBox) {
                        Rect newBounds = myBox->getBounds(newPos);

                        // Check vs box colliders
                        world_->forEach<Transform, BoxCollider>(
                            [&](Entity* other, Transform* otherT, BoxCollider* otherC) {
                                if (other == entity || otherC->isTrigger) return;
                                if (newBounds.overlaps(otherC->getBounds(otherT->position))) {
                                    blocked = true;
                                }
                            }
                        );

                        // Check vs polygon colliders
                        if (!blocked) {
                            world_->forEach<Transform, PolygonCollider>(
                                [&](Entity* other, Transform* otherT, PolygonCollider* otherP) {
                                    if (other == entity || otherP->isTrigger) return;
                                    auto otherPts = otherP->getWorldPoints(otherT->position);
                                    if (CollisionUtil::polygonOverlapsRect(otherPts, newBounds)) {
                                        blocked = true;
                                    }
                                }
                            );
                        }
                    } else if (myPoly) {
                        auto myPts = myPoly->getWorldPoints(newPos);

                        // Check vs box colliders
                        world_->forEach<Transform, BoxCollider>(
                            [&](Entity* other, Transform* otherT, BoxCollider* otherC) {
                                if (other == entity || otherC->isTrigger) return;
                                if (CollisionUtil::polygonOverlapsRect(myPts, otherC->getBounds(otherT->position))) {
                                    blocked = true;
                                }
                            }
                        );

                        // Check vs polygon colliders
                        if (!blocked) {
                            world_->forEach<Transform, PolygonCollider>(
                                [&](Entity* other, Transform* otherT, PolygonCollider* otherP) {
                                    if (other == entity || otherP->isTrigger) return;
                                    auto otherPts = otherP->getWorldPoints(otherT->position);
                                    if (CollisionUtil::polygonsOverlap(myPts, otherPts)) {
                                        blocked = true;
                                    }
                                }
                            );
                        }
                    }

                    if (!blocked) {
                        transform->position = newPos;
                    }
                }

                // Update animator if present
                auto* animator = entity->getComponent<Animator>();
                if (animator) {
                    if (ctrl->isMoving) {
                        switch (ctrl->facing) {
                            case Direction::Down:  animator->play("walk_down");  break;
                            case Direction::Up:    animator->play("walk_up");    break;
                            case Direction::Left:  animator->play("walk_left");  break;
                            case Direction::Right: animator->play("walk_right"); break;
                            default: break;
                        }
                    } else {
                        switch (ctrl->facing) {
                            case Direction::Down:  animator->play("idle_down");  break;
                            case Direction::Up:    animator->play("idle_up");    break;
                            case Direction::Left:  animator->play("idle_left");  break;
                            case Direction::Right: animator->play("idle_right"); break;
                            default: break;
                        }
                    }
                }

                // Update sprite flip for left/right facing
                auto* sprite = entity->getComponent<SpriteComponent>();
                if (sprite) {
                    sprite->flipX = (ctrl->facing == Direction::Left);
                }
            }
        );
    }
};

// Updates sprite animation timers
class AnimationSystem : public System {
public:
    const char* name() const override { return "AnimationSystem"; }

    void update(float dt) override {
        LOG_INFO("TICK", "AnimationSystem::update");
        world_->forEach<SpriteComponent, Animator>(
            [&](Entity*, SpriteComponent* sprite, Animator* anim) {
                if (anim->playing) {
                    anim->timer += dt;
                    sprite->currentFrame = anim->getCurrentFrame();
                    sprite->updateSourceRect();
                }
            }
        );
    }
};

// Locks camera to player position (TWOM-style: player always dead center)
class CameraFollowSystem : public System {
public:
    const char* name() const override { return "CameraFollowSystem"; }

    Camera* camera = nullptr;

    void update(float dt) override {
        LOG_INFO("TICK", "CameraFollowSystem::update");
        if (!camera) return;

        world_->forEach<Transform, PlayerController>(
            [&](Entity*, Transform* transform, PlayerController* ctrl) {
                if (!ctrl->isLocalPlayer) return;
                // Lock camera directly to player — no pixel snapping
                // (snapping at 480x270 causes visible jitter)
                camera->setPosition(transform->position);
            }
        );
    }
};

} // namespace fate
