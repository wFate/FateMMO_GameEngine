#pragma once
#include "engine/ecs/world.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/texture.h"
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/player_controller.h"
#include "game/components/game_components.h"
#include "game/components/animator.h"
#include "game/data/paper_doll_catalog.h"
#include "engine/core/logger.h"

namespace fate {

// Renders all entities with Transform + SpriteComponent
class SpriteRenderSystem : public System {
public:
    const char* name() const override { return "SpriteRenderSystem"; }

    SpriteBatch* batch = nullptr;
    Camera* camera = nullptr;

    void update(float) override {
        if (!batch || !camera) return;

        Mat4 vp = camera->getViewProjection();
        Rect visible = camera->getVisibleBounds();

        batch->begin(vp);

        // Pass 1: Regular sprites (skip entities with AppearanceComponent)
        world_->forEach<Transform, SpriteComponent>(
            [&](Entity* entity, Transform* transform, SpriteComponent* sprite) {
                if (!sprite->enabled || !sprite->texture) return;
                if (entity->hasComponent<AppearanceComponent>()) return;

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
                params.position = transform->position + sprite->renderOffset;
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

        // Pass 2: Paper doll characters
        world_->forEach<Transform, AppearanceComponent>(
            [&](Entity* entity, Transform* transform, AppearanceComponent* appearance) {
                auto& catalog = PaperDollCatalog::instance();
                int fw = catalog.isLoaded() ? catalog.frameWidth() : PAPER_DOLL_FRAME_W;
                int fh = catalog.isLoaded() ? catalog.frameHeight() : PAPER_DOLL_FRAME_H;

                float halfW = fw * 0.5f;
                float halfH = fh * 0.5f;
                Rect bounds = {
                    transform->position.x - halfW,
                    transform->position.y - halfH,
                    (float)fw, (float)fh
                };
                if (!bounds.overlaps(visible)) return;

                if (appearance->dirty) {
                    resolvePaperDollTextures(entity, appearance);
                }

                int frame = 0;
                bool flipX = false;
                Direction facing = Direction::Down;
                std::string animName;

                auto* animator = entity->getComponent<Animator>();
                auto* sprite = entity->getComponent<SpriteComponent>();
                auto* ctrl = entity->getComponent<PlayerController>();
                if (animator) {
                    frame = animator->getCurrentFrame();
                    animName = animator->currentAnimation;
                }
                if (sprite) flipX = sprite->flipX;
                if (ctrl) facing = ctrl->facing;

                // Direction determines which texture variant to use
                // Left/Right use side texture; Left also sets flipX
                if (facing == Direction::Left) flipX = true;
                else if (facing == Direction::Right) flipX = false;

                float depth = transform->depth;

                // Get local frame index for layer offsets
                int localFrame = 0;
                if (animator) localFrame = animator->getLocalFrameIndex();

                auto pickTex = [&](const AppearanceComponent::LayerTextures& lt) -> std::shared_ptr<Texture> {
                    switch (facing) {
                        case Direction::Up:    return lt.back;
                        case Direction::Down:  return lt.front;
                        case Direction::Left:
                        case Direction::Right: return lt.side;
                        default:               return lt.front;
                    }
                };

                auto drawLayer = [&](const AppearanceComponent::LayerTextures& lt,
                                     const std::string& layerName, float depthOffset) {
                    auto tex = pickTex(lt);
                    if (!tex) return;
                    float offsetY = catalog.getLayerOffsetY(animName, layerName, localFrame);
                    Vec2 pos = transform->position;
                    pos.y += offsetY;

                    int texW = tex->width(), texH = tex->height();
                    if (texW == 0 || texH == 0) return;
                    int columns = texW / fw;
                    if (columns == 0) columns = 1;
                    int col = frame % columns;
                    int row = frame / columns;

                    Rect sourceRect;
                    sourceRect.x = (float)(col * fw) / texW;
                    sourceRect.y = (float)(row * fh) / texH;
                    sourceRect.w = (float)fw / texW;
                    sourceRect.h = (float)fh / texH;

                    SpriteDrawParams params;
                    params.position = pos;
                    params.size = Vec2((float)fw, (float)fh) * transform->scale;
                    params.sourceRect = sourceRect;
                    params.color = Color::white();
                    params.rotation = transform->rotation;
                    params.depth = depth + depthOffset;
                    params.flipX = flipX;
                    params.flipY = false;
                    batch->draw(tex, params);
                };

                drawLayer(appearance->body,   "body",   0.0f);
                drawLayer(appearance->hair,   "hair",   0.01f);
                drawLayer(appearance->armor,  "armor",  0.02f);
                drawLayer(appearance->hat,    "hat",    0.03f);

                if (animator && animName.find("attack") != std::string::npos) {
                    drawLayer(appearance->weapon, "weapon", 0.04f);
                }
            }
        );

        batch->end();
    }

private:
    static constexpr int PAPER_DOLL_FRAME_W = 48;
    static constexpr int PAPER_DOLL_FRAME_H = 96;

    void resolvePaperDollTextures(Entity* /*entity*/, AppearanceComponent* a) {
        auto& catalog = PaperDollCatalog::instance();
        if (!catalog.isLoaded()) return;

        const char* genderStr = a->gender == 0 ? "Male" : "Female";
        std::string hairName = catalog.getHairstyleNameByIndex(genderStr, a->hairstyle);

        auto toLayer = [](const SpriteSet& ss) -> AppearanceComponent::LayerTextures {
            return {ss.front, ss.back, ss.side};
        };

        a->body   = toLayer(catalog.getBody(genderStr));
        a->hair   = toLayer(catalog.getHairstyle(genderStr, hairName));
        a->armor  = toLayer(catalog.getEquipment("armor", a->armorStyle));
        a->hat    = toLayer(catalog.getEquipment("hat", a->hatStyle));
        a->weapon = toLayer(catalog.getEquipment("weapon", a->weaponStyle));

        a->dirty = false;
    }
};

} // namespace fate
