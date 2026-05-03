// engine/components/register_engine_components.h
//
// Registers Transform / SpriteComponent / TileLayerComponent with the
// ComponentMetaRegistry so they can be (de)serialized via PrefabLibrary —
// required for editor save/load and Play/Stop snapshot/restore.
//
// The full-game build (game/register_components.h) calls these registrations
// itself as part of its larger setup; the demo (no game/) calls this header's
// registerEngineComponents() from demo_app::onInit so the same prefab path
// works for painted tiles, sprite entities, and tile layer toggles.
//
// Header-only inline functions — both the demo and any future engine-only
// consumer can include and call this without pulling in any game/ symbols.

#pragma once

#include "engine/ecs/component_meta.h"
#include "engine/components/transform.h"
#include "engine/components/sprite_component.h"
#include "engine/components/tile_layer_component.h"
#include "engine/module/behavior_component.h"

#include <nlohmann/json.hpp>

namespace fate {

inline void registerEngineComponents() {
    auto& reg = ComponentMetaRegistry::instance();

    reg.registerComponent<Transform>();

    // SpriteComponent has a custom serializer because the texture is a
    // shared_ptr<Texture> that can't be JSON-encoded directly — texturePath
    // round-trips and the texture is reloaded on deserialize.
    reg.registerComponent<SpriteComponent>(
        // toJson
        [](const void* data, nlohmann::json& j) {
            const auto* s = static_cast<const SpriteComponent*>(data);
            j["texturePath"] = s->texturePath;
            j["sourceRect"]  = { s->sourceRect.x, s->sourceRect.y,
                                 s->sourceRect.w, s->sourceRect.h };
            j["size"]        = { s->size.x, s->size.y };
            j["tint"]        = { s->tint.r, s->tint.g, s->tint.b, s->tint.a };
            j["flipX"]       = s->flipX;
            j["flipY"]       = s->flipY;
            j["frameWidth"]  = s->frameWidth;
            j["frameHeight"] = s->frameHeight;
            j["currentFrame"]= s->currentFrame;
            j["totalFrames"] = s->totalFrames;
            j["columns"]     = s->columns;
        },
        // fromJson
        [](const nlohmann::json& j, void* data) {
            auto* s = static_cast<SpriteComponent*>(data);
            // Support both old ("texture") and new ("texturePath") field names
            if (j.contains("texturePath"))
                s->texturePath = j["texturePath"].get<std::string>();
            else if (j.contains("texture"))
                s->texturePath = j["texture"].get<std::string>();
            if (j.contains("sourceRect")) {
                auto& r = j["sourceRect"];
                s->sourceRect = { r[0].get<float>(), r[1].get<float>(),
                                  r[2].get<float>(), r[3].get<float>() };
            }
            if (j.contains("size")) {
                auto& v = j["size"];
                s->size = { v[0].get<float>(), v[1].get<float>() };
            }
            if (j.contains("tint")) {
                auto& c = j["tint"];
                s->tint = Color(c[0].get<float>(), c[1].get<float>(),
                                c[2].get<float>(), c[3].get<float>());
            }
            if (j.contains("flipX"))       s->flipX       = j["flipX"].get<bool>();
            if (j.contains("flipY"))       s->flipY       = j["flipY"].get<bool>();
            if (j.contains("frameWidth"))  s->frameWidth  = j["frameWidth"].get<int>();
            if (j.contains("frameHeight")) s->frameHeight = j["frameHeight"].get<int>();
            if (j.contains("currentFrame"))s->currentFrame= j["currentFrame"].get<int>();
            if (j.contains("totalFrames")) s->totalFrames = j["totalFrames"].get<int>();
            if (j.contains("columns"))     s->columns     = j["columns"].get<int>();
            // Reload texture from path after deserialization
            if (!s->texturePath.empty()) {
                s->texture = TextureCache::instance().load(s->texturePath);
            }
        }
    );

    reg.registerComponent<TileLayerComponent>();

    // Hot-reloadable behavior shell — body lives in engine/module so the demo
    // (no game/) and the full game share the same registration.
    registerBehaviorComponent();

    // Backward-compat alias so old scene JSON written before the rename
    // still loads.
    reg.registerAlias("Sprite", "SpriteComponent");
}

} // namespace fate
