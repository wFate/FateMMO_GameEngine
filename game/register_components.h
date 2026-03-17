#pragma once

// register_components.h — explicit component registration with trait overrides
// Called once at startup from GameApp::onInit()

#include "engine/ecs/component_meta.h"
#include "engine/ecs/component_traits.h"

#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/box_collider.h"
#include "game/components/player_controller.h"
#include "game/components/animator.h"
#include "game/components/polygon_collider.h"
#include "game/components/zone_component.h"
#include "game/components/game_components.h"
#include "game/systems/spawn_system.h"  // SpawnZoneComponent

#include <nlohmann/json.hpp>

// ============================================================================
// Component trait specializations (override default Serializable)
// ============================================================================

namespace fate {

// --- Transform: saved to disk ---
template<> struct component_traits<Transform> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Persistent;
};

// --- SpriteComponent: serialized (has custom toJson/fromJson for texture) ---
template<> struct component_traits<SpriteComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

// --- BoxCollider: serialized ---
template<> struct component_traits<BoxCollider> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

// --- PlayerController: runtime only (isLocalPlayer is set at spawn) ---
template<> struct component_traits<PlayerController> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};

// --- MobAIComponent: runtime only ---
template<> struct component_traits<MobAIComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};

// --- TargetingComponent: runtime only ---
template<> struct component_traits<TargetingComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};

// --- NameplateComponent: runtime only ---
template<> struct component_traits<NameplateComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};

// --- MobNameplateComponent: runtime only ---
template<> struct component_traits<MobNameplateComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};

// --- Social components: runtime only ---
template<> struct component_traits<ChatComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};
template<> struct component_traits<GuildComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};
template<> struct component_traits<PartyComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};
template<> struct component_traits<FriendsComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};
template<> struct component_traits<MarketComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};
template<> struct component_traits<TradeComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};

// --- CharacterStatsComponent: saved to DB, replicated ---
template<> struct component_traits<CharacterStatsComponent> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Networked | ComponentFlags::Persistent;
};

// --- InventoryComponent: saved to DB ---
template<> struct component_traits<InventoryComponent> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Persistent;
};

// --- EnemyStatsComponent: serialized for spawning ---
template<> struct component_traits<EnemyStatsComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

// --- DamageableComponent: serialized (marker) ---
template<> struct component_traits<DamageableComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

// --- CombatControllerComponent: serialized ---
template<> struct component_traits<CombatControllerComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

// All other components keep the default (Serializable).

// ============================================================================
// registerAllComponents() — call once at startup
// ============================================================================

inline void registerAllComponents() {
    auto& reg = ComponentMetaRegistry::instance();

    // ----- Core engine components -----
    reg.registerComponent<Transform>();

    // SpriteComponent: custom serializer handles texturePath (skips shared_ptr<Texture>)
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
            if (j.contains("texturePath")) s->texturePath = j["texturePath"].get<std::string>();
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
            // Note: texture (shared_ptr) is NOT restored here — the asset
            // loader must re-bind it from texturePath after deserialization.
        }
    );

    reg.registerComponent<BoxCollider>();
    reg.registerComponent<PlayerController>();
    reg.registerComponent<Animator>();
    reg.registerComponent<PolygonCollider>();

    // ----- Zone / portal components -----
    reg.registerComponent<ZoneComponent>();
    reg.registerComponent<PortalComponent>();
    reg.registerComponent<SpawnZoneComponent>();

    // ----- Player components -----
    reg.registerComponent<CharacterStatsComponent>();
    reg.registerComponent<CombatControllerComponent>();
    reg.registerComponent<DamageableComponent>();
    reg.registerComponent<InventoryComponent>();
    reg.registerComponent<SkillManagerComponent>();
    reg.registerComponent<StatusEffectComponent>();
    reg.registerComponent<CrowdControlComponent>();
    reg.registerComponent<TargetingComponent>();
    reg.registerComponent<ChatComponent>();
    reg.registerComponent<GuildComponent>();
    reg.registerComponent<PartyComponent>();
    reg.registerComponent<FriendsComponent>();
    reg.registerComponent<MarketComponent>();
    reg.registerComponent<TradeComponent>();
    reg.registerComponent<NameplateComponent>();

    // ----- Mob / Enemy components -----
    reg.registerComponent<EnemyStatsComponent>();
    reg.registerComponent<MobAIComponent>();
    reg.registerComponent<MobNameplateComponent>();

    // ----- NPC components -----
    reg.registerComponent<NPCComponent>();
    reg.registerComponent<QuestGiverComponent>();
    reg.registerComponent<QuestMarkerComponent>();
    reg.registerComponent<ShopComponent>();
    reg.registerComponent<SkillTrainerComponent>();
    reg.registerComponent<BankerComponent>();
    reg.registerComponent<GuildNPCComponent>();
    reg.registerComponent<TeleporterComponent>();
    reg.registerComponent<StoryNPCComponent>();

    // ----- Player quest / bank -----
    reg.registerComponent<QuestComponent>();
    reg.registerComponent<BankStorageComponent>();

    // ----- Backward-compat aliases -----
    reg.registerAlias("Sprite", "SpriteComponent");
}

} // namespace fate
