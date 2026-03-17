#pragma once
#include "engine/ecs/world.h"
#include "engine/render/camera.h"
#include "engine/render/sprite_batch.h"
#include "engine/core/logger.h"
#include "game/components/transform.h"
#include "game/components/player_controller.h"
#include "game/components/zone_component.h"
#include <string>
#include <functional>

namespace fate {

// Manages zone transitions: detects when player enters a portal,
// triggers fade effect, teleports player, updates camera bounds.
class ZoneSystem : public System {
public:
    const char* name() const override { return "ZoneSystem"; }

    Camera* camera = nullptr;

    // Callbacks for scene loading (wired by GameApp)
    std::function<void(const std::string& scene)> onSceneTransition;
    std::function<void()> onFadeStart;
    std::function<void()> onFadeEnd;

    void update(float dt) override {
        if (!camera) return;

        // Find the local player
        Entity* player = nullptr;
        Transform* playerTransform = nullptr;

        world_->forEach<Transform, PlayerController>(
            [&](Entity* entity, Transform* t, PlayerController* ctrl) {
                if (ctrl->isLocalPlayer) {
                    player = entity;
                    playerTransform = t;
                }
            }
        );

        if (!player || !playerTransform) return;

        // Check which zone the player is in
        std::string currentZone;
        world_->forEach<Transform, ZoneComponent>(
            [&](Entity*, Transform* zoneT, ZoneComponent* zone) {
                if (zone->contains(zoneT->position, playerTransform->position)) {
                    currentZone = zone->zoneName;

                    // Clamp camera to zone bounds if desired
                    // (Optional: only clamp if zone is large enough)
                }
            }
        );

        if (!currentZone.empty() && currentZone != activeZone_) {
            LOG_INFO("Zone", "Player entered zone: %s", currentZone.c_str());
            activeZone_ = currentZone;
        }

        // Check portal collisions
        if (transitioning_) {
            transitionTimer_ -= dt;
            if (transitionTimer_ <= 0.0f) {
                // Transition complete
                transitioning_ = false;
                if (onFadeEnd) onFadeEnd();
            }
            return; // don't check portals during transition
        }

        world_->forEach<Transform, PortalComponent>(
            [&](Entity*, Transform* portalT, PortalComponent* portal) {
                if (transitioning_) return;

                Rect trigger = portal->getTriggerBounds(portalT->position);
                if (trigger.contains(playerTransform->position)) {
                    triggerTransition(playerTransform, portal);
                }
            }
        );
    }

    // Get the zone the player is currently in
    const std::string& activeZone() const { return activeZone_; }

    // Render zone boundaries and portals in debug mode
    void renderDebug(SpriteBatch& batch, Camera& cam) {
        Mat4 vp = cam.getViewProjection();
        batch.begin(vp);

        // Draw zone boundaries
        world_->forEach<Transform, ZoneComponent>(
            [&](Entity*, Transform* t, ZoneComponent* zone) {
                Rect bounds = zone->getBounds(t->position);

                // Zone outline (blue dashed)
                Color zoneColor(0.2f, 0.5f, 1.0f, 0.3f);
                batch.drawRect({bounds.x + bounds.w * 0.5f, bounds.y + bounds.h},
                              {bounds.w, 2.0f}, zoneColor, 98.0f);
                batch.drawRect({bounds.x + bounds.w * 0.5f, bounds.y},
                              {bounds.w, 2.0f}, zoneColor, 98.0f);
                batch.drawRect({bounds.x, bounds.y + bounds.h * 0.5f},
                              {2.0f, bounds.h}, zoneColor, 98.0f);
                batch.drawRect({bounds.x + bounds.w, bounds.y + bounds.h * 0.5f},
                              {2.0f, bounds.h}, zoneColor, 98.0f);

                // Zone fill
                batch.drawRect(t->position, zone->size, Color(0.2f, 0.4f, 0.8f, 0.05f), 97.0f);
            }
        );

        // Draw portals
        world_->forEach<Transform, PortalComponent>(
            [&](Entity*, Transform* t, PortalComponent* portal) {
                Rect trigger = portal->getTriggerBounds(t->position);
                Color portalColor(1.0f, 0.8f, 0.0f, 0.4f);

                batch.drawRect(t->position, portal->triggerSize, portalColor, 98.0f);

                // Arrow showing target direction
                Color arrowColor(1.0f, 1.0f, 0.0f, 0.8f);
                Vec2 dir = (portal->targetSpawnPos - t->position).normalized();
                Vec2 arrowEnd = t->position + dir * 16.0f;
                batch.drawRect(arrowEnd, {4.0f, 4.0f}, arrowColor, 99.0f);
            }
        );

        batch.end();
    }

    // Fade state for rendering
    bool isTransitioning() const { return transitioning_; }
    float fadeAlpha() const {
        if (!transitioning_) return 0.0f;
        float halfDuration = fadeDuration_ * 0.5f;
        if (transitionTimer_ > halfDuration) {
            // Fading in (going dark)
            return 1.0f - (transitionTimer_ - halfDuration) / halfDuration;
        } else {
            // Fading out (going light)
            return transitionTimer_ / halfDuration;
        }
    }

private:
    std::string activeZone_;
    bool transitioning_ = false;
    float transitionTimer_ = 0.0f;
    float fadeDuration_ = 0.6f;

    void triggerTransition(Transform* playerTransform, PortalComponent* portal) {
        transitioning_ = true;
        fadeDuration_ = portal->fadeDuration * 2.0f; // total = fade out + fade in
        transitionTimer_ = fadeDuration_;

        if (onFadeStart) onFadeStart();

        if (!portal->targetScene.empty()) {
            // Cross-scene transition
            LOG_INFO("Zone", "Scene transition: -> %s (zone: %s)",
                     portal->targetScene.c_str(), portal->targetZone.c_str());
            if (onSceneTransition) {
                onSceneTransition(portal->targetScene);
            }
        } else {
            // Same-scene zone transition (teleport)
            LOG_INFO("Zone", "Zone transition: -> %s at (%.0f, %.0f)",
                     portal->targetZone.c_str(),
                     portal->targetSpawnPos.x, portal->targetSpawnPos.y);
        }

        // Teleport player to target position
        playerTransform->position = portal->targetSpawnPos;
    }
};

} // namespace fate
