# Generic Component Serialization System

**Date:** 2026-03-16
**Status:** Ready for implementation
**Goal:** Make every component automatically serializable so the scene save/load system never silently drops data.

## The Problem

The editor's `saveScene()`/`loadScene()` in `engine/editor/editor.cpp` hardcodes which components it knows how to serialize. Right now it handles: Transform, SpriteComponent, BoxCollider, PolygonCollider, PlayerController, ZoneComponent, PortalComponent, SpawnZoneComponent. That's 8 out of 27+ component types.

Every other component (Animator, CharacterStats, CombatController, EnemyStats, MobAI, Inventory, SkillManager, StatusEffects, CrowdControl, Targeting, Chat, Guild, Party, Friends, Market, Trade, Nameplate, MobNameplate, Damageable) is silently lost on save/reload. This means:

- Animator data (animation definitions, current state) is lost
- Player stats are lost
- Any new component added in the future must have serialization manually added to the editor or it gets dropped

This is the #1 architectural debt in the engine right now.

## The Solution: Self-Describing Components

Each component type defines its own `serialize()` and `deserialize()` methods. A component factory registry maps type name strings to constructor functions. The editor becomes generic — it doesn't need to know about specific component types.

### Architecture Overview

```
Component (base class)
  + virtual void serialize(json& out) const = 0;
  + virtual void deserialize(const json& in) = 0;
  + typeName() already exists via FATE_COMPONENT macro

ComponentRegistry (singleton)
  + register<T>(name) — maps "Transform" -> []{ return make_unique<Transform>(); }
  + create(name) -> unique_ptr<Component>
  + Auto-registered via REGISTER_COMPONENT macro

Editor::saveScene()
  entity->forEachComponent([&](Component* comp) {
      json cj;
      comp->serialize(cj);
      componentsJson[comp->typeName()] = cj;
  });

Editor::loadScene()
  for (auto& [name, data] : componentsJson) {
      auto comp = ComponentRegistry::instance().create(name);
      comp->deserialize(data);
      entity->addComponentRaw(name, std::move(comp));
  }
```

## Detailed Design

### Step 1: Extend Component Base Class

**File:** `engine/ecs/component.h`

Add pure virtual serialization methods to the `Component` base class:

```cpp
#include <nlohmann/json_fwd.hpp>

struct Component {
    virtual ~Component() = default;
    virtual const char* typeName() const = 0;
    virtual ComponentTypeId typeId() const = 0;
    bool enabled = true;

    // Serialization — every component must implement these
    virtual void serialize(nlohmann::json& out) const = 0;
    virtual void deserialize(const nlohmann::json& in) = 0;

    // Optional: components can declare themselves as transient (not saved)
    // e.g., runtime-only state like MobAI tick accumulators
    virtual bool isSerializable() const { return true; }
};
```

**Why pure virtual:** Forces every component to implement serialization. If a developer adds a new component and forgets, they get a compile error — not a silent data loss at runtime.

**The `isSerializable()` escape hatch:** Some components hold purely runtime state (tracked mob lists, cooldown timers, AI accumulators). These should NOT be saved. Components override this to return `false` to opt out. The editor skips them during save.

### Step 2: Component Factory Registry

**File:** `engine/ecs/component_registry.h` (new)

```cpp
#pragma once
#include "engine/ecs/component.h"
#include <unordered_map>
#include <functional>
#include <memory>
#include <string>

namespace fate {

class ComponentRegistry {
public:
    static ComponentRegistry& instance() {
        static ComponentRegistry s;
        return s;
    }

    using FactoryFn = std::function<std::unique_ptr<Component>()>;

    template<typename T>
    void registerComponent(const std::string& name) {
        factories_[name] = []() -> std::unique_ptr<Component> {
            return std::make_unique<T>();
        };
    }

    std::unique_ptr<Component> create(const std::string& name) const {
        auto it = factories_.find(name);
        if (it == factories_.end()) return nullptr;
        return it->second();
    }

    bool isRegistered(const std::string& name) const {
        return factories_.count(name) > 0;
    }

    const auto& allRegistered() const { return factories_; }

private:
    ComponentRegistry() = default;
    std::unordered_map<std::string, FactoryFn> factories_;
};

} // namespace fate
```

### Step 3: Auto-Registration Macro

Update the `FATE_COMPONENT` macro to also register the component in the factory:

```cpp
// In component.h or a dedicated macros header:

#define FATE_COMPONENT(ClassName) \
    const char* typeName() const override { return #ClassName; } \
    ComponentTypeId typeId() const override { return fate::getComponentTypeId<ClassName>(); } \
    static bool _registered_; // declaration

// Separate macro for the .cpp file (or header-only components):
#define REGISTER_COMPONENT(ClassName) \
    bool ClassName::_registered_ = []() { \
        fate::ComponentRegistry::instance().registerComponent<ClassName>(#ClassName); \
        return true; \
    }();
```

**Alternative (simpler, header-only):** Use a static initializer inside the component struct:

```cpp
#define FATE_COMPONENT(ClassName) \
    const char* typeName() const override { return #ClassName; } \
    ComponentTypeId typeId() const override { return fate::getComponentTypeId<ClassName>(); } \
    struct _AutoRegister { \
        _AutoRegister() { \
            fate::ComponentRegistry::instance().registerComponent<ClassName>(#ClassName); \
        } \
    }; \
    static inline _AutoRegister _autoReg_{};
```

This auto-registers every component type the first time the translation unit is loaded. No manual registration step needed.

**Important consideration:** Static initialization order. The `ComponentRegistry::instance()` must be constructed before any component's auto-register runs. Since it's a Meyer's singleton (function-local static), this is guaranteed — the first call to `instance()` constructs it.

### Step 4: Add `addComponentRaw()` to Entity

**File:** `engine/ecs/entity.h`

The entity currently only supports `addComponent<T>()` which requires knowing the type at compile time. For deserialization, we need to add a component by type name string:

```cpp
// Add a pre-constructed component by its type ID
void addComponentRaw(std::unique_ptr<Component> comp) {
    if (!comp) return;
    components_[comp->typeId()] = std::move(comp);
}
```

### Step 5: Add `forEachComponent()` to Entity

**File:** `engine/ecs/entity.h`

The entity needs a way to iterate all components generically:

```cpp
template<typename Fn>
void forEachComponent(Fn&& fn) {
    for (auto& [id, comp] : components_) {
        fn(comp.get());
    }
}

template<typename Fn>
void forEachComponent(Fn&& fn) const {
    for (auto& [id, comp] : components_) {
        fn(comp.get());
    }
}
```

**Note:** `forEachComponent` may already exist in the codebase — check before adding.

### Step 6: Implement `serialize()`/`deserialize()` on Every Component

Each component implements its own serialization. Examples:

**Transform:**
```cpp
void serialize(nlohmann::json& out) const override {
    out["position"] = {position.x, position.y};
    out["scale"] = {scale.x, scale.y};
    out["rotation"] = rotation;
    out["depth"] = depth;
}

void deserialize(const nlohmann::json& in) override {
    if (in.contains("position")) {
        auto p = in["position"];
        position = {p[0].get<float>(), p[1].get<float>()};
    }
    if (in.contains("scale")) {
        auto s = in["scale"];
        scale = {s[0].get<float>(), s[1].get<float>()};
    }
    rotation = in.value("rotation", 0.0f);
    depth = in.value("depth", 0.0f);
}
```

**SpriteComponent:**
```cpp
void serialize(nlohmann::json& out) const override {
    out["texture"] = texturePath;
    out["size"] = {size.x, size.y};
    out["sourceRect"] = {sourceRect.x, sourceRect.y, sourceRect.w, sourceRect.h};
    out["tint"] = {tint.r, tint.g, tint.b, tint.a};
    out["flipX"] = flipX;
    out["flipY"] = flipY;
}

void deserialize(const nlohmann::json& in) override {
    texturePath = in.value("texture", "");
    if (!texturePath.empty()) {
        texture = TextureCache::instance().load(texturePath);
    }
    // ... etc
}
```

**Runtime-only component (e.g., MobAIComponent):**
```cpp
bool isSerializable() const override { return false; } // Skip during save
void serialize(nlohmann::json&) const override {} // No-op
void deserialize(const nlohmann::json&) override {} // No-op
```

### Step 7: Rewrite Editor saveScene/loadScene to Be Generic

**saveScene:**
```cpp
world->forEachEntity([&](Entity* entity) {
    // Skip transient entities
    std::string tag = entity->tag();
    if (tag == "mob" || tag == "boss") return;

    nlohmann::json ej;
    ej["name"] = entity->name();
    ej["tag"] = tag;
    ej["active"] = entity->isActive();

    nlohmann::json comps;
    entity->forEachComponent([&](Component* comp) {
        if (comp->isSerializable()) {
            nlohmann::json cj;
            comp->serialize(cj);
            comps[comp->typeName()] = cj;
        }
    });

    ej["components"] = comps;
    entitiesJson.push_back(ej);
});
```

**loadScene:**
```cpp
for (auto& [typeName, data] : comps.items()) {
    auto comp = ComponentRegistry::instance().create(typeName);
    if (comp) {
        comp->deserialize(data);
        entity->addComponentRaw(std::move(comp));
    } else {
        LOG_WARN("Editor", "Unknown component type: %s (skipped)", typeName.c_str());
    }
}
```

**This is the entire serialization system.** The editor no longer needs to know about specific component types. Adding a new component with `FATE_COMPONENT` and implementing `serialize()`/`deserialize()` is all that's needed — the editor automatically saves and loads it.

### Step 8: Migrate Prefab System

The prefab system (`engine/ecs/prefab.h`) likely has the same hardcoded serialization problem. The `PrefabLibrary::entityToJson()` and `PrefabLibrary::jsonToEntity()` methods should be updated to use the same generic approach. This is a secondary migration — do it after the editor save/load works.

## Component Serialization Categories

Not all components should be saved in a scene file. Here's how to categorize them:

| Category | isSerializable() | Examples | Reason |
|----------|-------------------|----------|--------|
| **Scene components** | `true` | Transform, Sprite, BoxCollider, PolygonCollider, PlayerController, Zone, Portal, SpawnZone, Animator | Define the scene layout and entity configuration |
| **Runtime state** | `false` | MobAI (tick accumulators), CombatController (cooldowns), Targeting (selected target), TrackedMobs | Transient state reconstructed at runtime |
| **Player session data** | `false` | CharacterStats, Inventory, Skills, StatusEffects, CrowdControl | Managed by server/database, not scene files |
| **Social systems** | `false` | Chat, Guild, Party, Friends, Market, Trade | Network-managed, not persisted locally |
| **Display components** | `true` | Nameplate, MobNameplate | Visual configuration that should persist |
| **Marker components** | `true` | Damageable | No data to serialize, but presence matters |

**Marker components** like `DamageableComponent` have no fields but their existence on an entity matters. Their serialize/deserialize are empty, but they still return `isSerializable() = true` so the type name gets saved.

## Implementation Order

1. **Component base class** — add `serialize()`, `deserialize()`, `isSerializable()` as pure virtual / virtual
2. **ComponentRegistry** — new file, factory pattern
3. **FATE_COMPONENT macro** — update to auto-register
4. **Entity** — add `addComponentRaw()`, verify `forEachComponent()` exists
5. **Engine components** — implement serialize/deserialize on Transform, SpriteComponent, BoxCollider, PolygonCollider, Animator
6. **Game components** — implement on PlayerController, ZoneComponent, PortalComponent, SpawnZoneComponent, NameplateComponent, MobNameplateComponent, DamageableComponent
7. **Runtime-only components** — mark as `isSerializable() = false`: CharacterStats, CombatController, MobAI, Inventory, Skills, StatusEffects, CrowdControl, Targeting, Chat, Guild, Party, Friends, Market, Trade
8. **Editor saveScene/loadScene** — replace hardcoded serialization with generic loop
9. **Prefab system** — migrate to same generic approach
10. **Test** — save scene, reload, verify all components survive

## Files Affected

| File | Change |
|------|--------|
| `engine/ecs/component.h` | Add serialize/deserialize/isSerializable to base class |
| `engine/ecs/component_registry.h` | NEW — factory registry singleton |
| `engine/ecs/entity.h` | Add `addComponentRaw()`, verify `forEachComponent()` |
| `game/components/transform.h` | Implement serialize/deserialize |
| `game/components/sprite_component.h` | Implement serialize/deserialize |
| `game/components/box_collider.h` | Implement serialize/deserialize |
| `game/components/polygon_collider.h` | Implement serialize/deserialize |
| `game/components/animator.h` | Implement serialize/deserialize |
| `game/components/player_controller.h` | Implement serialize/deserialize |
| `game/components/zone_component.h` | Implement serialize/deserialize (Zone + Portal) |
| `game/components/game_components.h` | Mark runtime components as non-serializable, implement serialize on nameplates |
| `game/systems/spawn_system.h` | Implement serialize/deserialize on SpawnZoneComponent |
| `engine/editor/editor.cpp` | Replace ~200 lines of hardcoded serialization with ~20 lines of generic code |
| `engine/ecs/prefab.h` | Migrate to generic serialization (secondary) |

## Why This Matters for a 2D MMORPG

1. **Content pipeline velocity** — Level designers can add new component types (quest triggers, NPC dialogue, shop inventories, environmental effects) and they automatically persist in scenes without touching the editor code.

2. **Server authority** — The `isSerializable()` flag cleanly separates scene data (level layout, spawn zones) from session data (player stats, inventory) which will live on the server. Scene files only contain what the server needs to reconstruct the world.

3. **Prefab system** — Once components self-serialize, prefabs become trivial. A "Goblin" prefab is just a JSON file with all its components. The entity factory can load any prefab without hardcoded component knowledge.

4. **Hot-reload potential** — With generic serialization, you can serialize an entity's state, modify component code, recompile, deserialize — enabling rapid iteration without restarting the editor.

5. **Network replication** — The same serialize/deserialize pattern extends to network replication. Components that need to sync across clients can use the same JSON format (or a binary variant) for network messages.

## Integration Notes

- **nlohmann/json** is already in the project and used everywhere. The `serialize()`/`deserialize()` methods use it directly.
- **RTTI is already enabled** — `std::type_index` and `typeid()` are used throughout the ECS. No additional compiler flags needed.
- **The FATE_COMPONENT macro is used by every component** — modifying it propagates to all components automatically.
- **Backward compatibility** — Old scene files (with hardcoded component names like "Transform", "Sprite") will still load because the registry maps the same strings. The `typeName()` output matches the existing JSON keys.
