#pragma once
#include "engine/ecs/world.h"
#include "engine/render/sprite_batch.h"
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "engine/core/logger.h"

namespace fate {

// Renders all entities with Transform + SpriteComponent
class SpriteRenderSystem : public System {
public:
    const char* name() const override { return "SpriteRenderSystem"; }

    SpriteBatch* batch = nullptr;
    Camera* camera = nullptr;

    void update(float) override {
        LOG_INFO("TICK", "SpriteRenderSystem::update");
        if (!batch || !camera) return;

        Mat4 vp = camera->getViewProjection();
        Rect visible = camera->getVisibleBounds();

        batch->begin(vp);

        world_->forEach<Transform, SpriteComponent>(
            [&](Entity*, Transform* transform, SpriteComponent* sprite) {
                if (!sprite->texture) return;

                // Frustum culling: skip sprites outside visible area
                float halfW = sprite->size.x * 0.5f;
                float halfH = sprite->size.y * 0.5f;
                Rect bounds = {
                    transform->position.x - halfW,
                    transform->position.y - halfH,
                    sprite->size.x,
                    sprite->size.y
                };
                if (!bounds.overlaps(visible)) return;

                SpriteDrawParams params;
                params.position = transform->position;
                params.size = sprite->size * transform->scale;
                params.sourceRect = sprite->sourceRect;
                params.color = sprite->tint;
                params.rotation = transform->rotation;
                params.depth = transform->depth;
                params.flipX = sprite->flipX;
                params.flipY = sprite->flipY;

                batch->draw(sprite->texture, params);
            }
        );

        batch->end();
    }
};

} // namespace fate
