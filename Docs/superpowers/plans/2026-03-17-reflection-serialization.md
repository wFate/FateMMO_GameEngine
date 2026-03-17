# Reflection & Serialization Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add field-level reflection and generic serialization to all engine/game components, replacing hardcoded JSON parsing with registry-driven auto-serialization.

**Architecture:** A `FATE_REFLECT(Type, FIELD(...), ...)` macro generates per-component field metadata (name, offset, type). A `ComponentMetaRegistry` maps string names to type-erased construct/destroy/serialize functions. The prefab system and scene save/load use the registry instead of hardcoded type checks. A generic inspector fallback renders components via reflection when no manual UI exists.

**Tech Stack:** C++23, MSVC, nlohmann/json, Dear ImGui, doctest

**Spec:** `Docs/superpowers/specs/2026-03-17-reflection-serialization-design.md`

**Build command:** `"/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build out/build --config Debug 2>&1 | tail -50`

**Test command:** `out/build/Debug/fate_tests.exe`

**CRITICAL: NEVER create .bat files. They hang indefinitely on this machine.**

---

## File Structure

### New Files
| File | Responsibility |
|---|---|
| `engine/ecs/reflect.h` | FieldType enum, FieldInfo struct, FATE_REFLECT macro, Reflection<T> template |
| `engine/ecs/component_meta.h` | ComponentMeta struct, ComponentMetaRegistry (Meyer's Singleton) |
| `engine/ecs/component_traits.h` | ComponentFlags enum, component_traits<T> default + bitwise operators |
| `game/register_components.h` | registerAllComponents() — explicit registration of all components with reflection |
| `tests/test_reflection.cpp` | Reflection, serialization, scene save/load tests |

### Modified Files
| File | Changes |
|---|---|
| `engine/ecs/entity.h` | Add `unknownComponents_` map for preserving unregistered JSON |
| `engine/ecs/world.h` / `world.cpp` | Add `addComponentById()` type-erased method |
| `engine/ecs/prefab.cpp` | Rewrite entityToJson/jsonToEntity to use ComponentMetaRegistry |
| `engine/scene/scene.cpp` | Update loadFromFile to use registry-based deserialization, add version header |
| `game/game_app.cpp` | Call registerAllComponents() in onInit() |
| `game/components/transform.h` | Add FATE_REFLECT |
| `game/components/sprite_component.h` | Add FATE_REFLECT |
| `game/components/box_collider.h` | Add FATE_REFLECT |
| `game/components/animator.h` | Add FATE_REFLECT |
| `game/components/player_controller.h` | Add FATE_REFLECT |
| `game/components/zone_component.h` | Add FATE_REFLECT |
| `game/components/game_components.h` | Add FATE_REFLECT to all wrapper components |

---

## Task 1: Reflection Core — FieldType, FieldInfo, FATE_REFLECT Macro

**Files:**
- Create: `engine/ecs/reflect.h`
- Create: `tests/test_reflection.cpp`

- [ ] **Step 1: Create `engine/ecs/reflect.h`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace fate {

enum class FieldType : uint8_t {
    Float, Int, UInt, Bool,
    Vec2, Vec3, Vec4, Color, Rect,
    String,
    Enum,
    EntityHandle,
    Direction,
    Custom
};

struct FieldInfo {
    const char* name;
    size_t offset;
    size_t size;
    FieldType type;
};

// Default: no reflection (empty field list)
template<typename T>
struct Reflection {
    static std::span<const FieldInfo> fields() { return {}; }
};

} // namespace fate

// Macro to define reflection for a component type.
// Usage: FATE_REFLECT(Transform, FATE_FIELD(position, Vec2), FATE_FIELD(rotation, Float))
// Must appear OUTSIDE any namespace, AFTER the struct definition.

#define FATE_FIELD(fieldName, fieldType) \
    fate::FieldInfo{ #fieldName, offsetof(_ReflType, fieldName), sizeof(std::declval<_ReflType>().fieldName), fate::FieldType::fieldType }

#define FATE_REFLECT(Type, ...) \
    template<> struct fate::Reflection<Type> { \
        using _ReflType = Type; \
        static std::span<const fate::FieldInfo> fields() { \
            static const fate::FieldInfo _fields[] = { \
                __VA_ARGS__ \
            }; \
            return std::span<const fate::FieldInfo>(_fields); \
        } \
    };
```

Note: `sizeof(std::declval<_ReflType>().fieldName)` may not work in all contexts. If MSVC rejects it, use `sizeof(Type::fieldName)` directly. The `_ReflType` alias is defined inside the template specialization scope.

Also note: `offsetof` on non-standard-layout types emits MSVC warning C4200. Add `#pragma warning(push)` / `#pragma warning(disable: 4200)` around the macro if needed.

- [ ] **Step 2: Create `tests/test_reflection.cpp` with basic tests**

```cpp
#include <doctest/doctest.h>
#include "engine/ecs/reflect.h"
#include "engine/ecs/component_registry.h"
#include "engine/core/types.h"

namespace {

struct TestPos {
    FATE_COMPONENT(TestPos)
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

} // namespace

FATE_REFLECT(TestPos,
    FATE_FIELD(x, Float),
    FATE_FIELD(y, Float),
    FATE_FIELD(z, Float)
)

TEST_CASE("Reflection field count") {
    auto fields = fate::Reflection<TestPos>::fields();
    CHECK(fields.size() == 3);
}

TEST_CASE("Reflection field names and types") {
    auto fields = fate::Reflection<TestPos>::fields();
    CHECK(std::string(fields[0].name) == "x");
    CHECK(fields[0].type == fate::FieldType::Float);
    CHECK(std::string(fields[1].name) == "y");
    CHECK(std::string(fields[2].name) == "z");
}

TEST_CASE("Reflection field offset access") {
    TestPos pos;
    pos.x = 42.0f;
    pos.y = 99.0f;
    auto fields = fate::Reflection<TestPos>::fields();

    // Read via offset
    float* xPtr = reinterpret_cast<float*>(
        reinterpret_cast<uint8_t*>(&pos) + fields[0].offset);
    CHECK(*xPtr == 42.0f);

    float* yPtr = reinterpret_cast<float*>(
        reinterpret_cast<uint8_t*>(&pos) + fields[1].offset);
    CHECK(*yPtr == 99.0f);

    // Write via offset
    *xPtr = 7.0f;
    CHECK(pos.x == 7.0f);
}

TEST_CASE("Unreflected component has empty fields") {
    struct Unreflected { FATE_COMPONENT(Unreflected) int val; };
    auto fields = fate::Reflection<Unreflected>::fields();
    CHECK(fields.empty());
}
```

- [ ] **Step 3: Build and run tests**

- [ ] **Step 4: Commit**

```bash
git add engine/ecs/reflect.h tests/test_reflection.cpp
git commit -m "feat(ecs): reflection core — FieldType, FieldInfo, FATE_REFLECT macro"
```

---

## Task 2: Component Traits — Flags System

**Files:**
- Create: `engine/ecs/component_traits.h`

- [ ] **Step 1: Create `engine/ecs/component_traits.h`**

```cpp
#pragma once
#include <cstdint>
#include <type_traits>

namespace fate {

enum class ComponentFlags : uint32_t {
    None         = 0,
    Serializable = 1 << 0,
    Networked    = 1 << 1,
    EditorOnly   = 1 << 2,
    Persistent   = 1 << 3,
};

constexpr ComponentFlags operator|(ComponentFlags a, ComponentFlags b) {
    return static_cast<ComponentFlags>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr ComponentFlags operator&(ComponentFlags a, ComponentFlags b) {
    return static_cast<ComponentFlags>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
constexpr bool hasFlag(ComponentFlags flags, ComponentFlags test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

// Default: all components are Serializable
template<typename T>
struct component_traits {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

} // namespace fate
```

- [ ] **Step 2: Build, verify, commit**

```bash
git add engine/ecs/component_traits.h
git commit -m "feat(ecs): component traits with Serializable/Networked/Persistent flags"
```

---

## Task 3: ComponentMeta Registry

**Files:**
- Create: `engine/ecs/component_meta.h`

- [ ] **Step 1: Create `engine/ecs/component_meta.h`**

The ComponentMetaRegistry maps string names to type-erased component metadata. It uses Meyer's Singleton internally but is populated via explicit `registerComponent<T>()` calls.

```cpp
#pragma once
#include "engine/ecs/reflect.h"
#include "engine/ecs/component_registry.h"
#include "engine/ecs/component_traits.h"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <functional>
#include <span>

namespace fate {

struct ComponentMeta {
    const char* name = nullptr;
    CompId id = 0;
    size_t size = 0;
    size_t alignment = 0;
    ComponentFlags flags = ComponentFlags::None;
    std::span<const FieldInfo> fields;

    // Type-erased operations
    std::function<void(void*)> construct;     // placement-new default construct
    std::function<void(void*)> destroy;       // destructor (if non-trivial)
    std::function<void(const void*, nlohmann::json&)> toJson;
    std::function<void(const nlohmann::json&, void*)> fromJson;
};

class ComponentMetaRegistry {
public:
    static ComponentMetaRegistry& instance() {
        static ComponentMetaRegistry reg;
        return reg;
    }

    template<typename T>
    void registerComponent(
        std::function<void(const void*, nlohmann::json&)> customToJson = nullptr,
        std::function<void(const nlohmann::json&, void*)> customFromJson = nullptr);

    void registerAlias(const std::string& alias, const std::string& canonical) {
        aliases_[alias] = canonical;
    }

    const ComponentMeta* findByName(const std::string& name) const {
        // Check aliases first
        auto aliasIt = aliases_.find(name);
        const std::string& lookup = (aliasIt != aliases_.end()) ? aliasIt->second : name;
        auto it = byName_.find(lookup);
        return (it != byName_.end()) ? &it->second : nullptr;
    }

    const ComponentMeta* findById(CompId id) const {
        auto it = byId_.find(id);
        return (it != byId_.end()) ? it->second : nullptr;
    }

    void forEachMeta(const std::function<void(const ComponentMeta&)>& fn) const {
        for (auto& [name, meta] : byName_) fn(meta);
    }

private:
    ComponentMetaRegistry() = default;
    std::unordered_map<std::string, ComponentMeta> byName_;
    std::unordered_map<CompId, const ComponentMeta*> byId_;
    std::unordered_map<std::string, std::string> aliases_;
};

// Auto-generate toJson/fromJson from reflected fields
void autoToJson(const void* data, nlohmann::json& j, std::span<const FieldInfo> fields);
void autoFromJson(const nlohmann::json& j, void* data, std::span<const FieldInfo> fields);

// Template implementation of registerComponent
template<typename T>
void ComponentMetaRegistry::registerComponent(
    std::function<void(const void*, nlohmann::json&)> customToJson,
    std::function<void(const nlohmann::json&, void*)> customFromJson)
{
    ComponentMeta meta;
    meta.name = T::COMPONENT_NAME;
    meta.id = componentId<T>();
    meta.size = sizeof(T);
    meta.alignment = alignof(T);
    meta.flags = component_traits<T>::flags;
    meta.fields = Reflection<T>::fields();

    meta.construct = [](void* ptr) { new (ptr) T(); };

    if constexpr (!std::is_trivially_destructible_v<T>) {
        meta.destroy = [](void* ptr) { static_cast<T*>(ptr)->~T(); };
    }

    if (customToJson) {
        meta.toJson = customToJson;
    } else if (!meta.fields.empty()) {
        meta.toJson = [fields = meta.fields](const void* data, nlohmann::json& j) {
            autoToJson(data, j, fields);
        };
    }

    if (customFromJson) {
        meta.fromJson = customFromJson;
    } else if (!meta.fields.empty()) {
        meta.fromJson = [fields = meta.fields](const nlohmann::json& j, void* data) {
            autoFromJson(j, data, fields);
        };
    }

    std::string nameStr(meta.name);
    byName_[nameStr] = meta;
    byId_[meta.id] = &byName_[nameStr];
}

} // namespace fate
```

- [ ] **Step 2: Create `engine/ecs/component_meta.cpp`**

Implement `autoToJson` and `autoFromJson` — the generic field-by-field serialization using FieldType dispatch. These read/write fields via `(uint8_t*)data + field.offset` with `reinterpret_cast` to the appropriate type.

Key dispatch table:
- `Float` → `j[name] = *reinterpret_cast<const float*>(ptr)`
- `Int` → `j[name] = *reinterpret_cast<const int*>(ptr)`
- `Bool` → `j[name] = *reinterpret_cast<const bool*>(ptr)`
- `Vec2` → `j[name] = { v.x, v.y }` (array format matching existing prefab convention)
- `Color` → `j[name] = { c.r, c.g, c.b, c.a }`
- `Rect` → `j[name] = { r.x, r.y, r.w, r.h }`
- `String` → `j[name] = *reinterpret_cast<const std::string*>(ptr)`
- `Custom` → skip (requires manual serializer)

For `autoFromJson`, use `j.value(name, default)` pattern to fill missing fields.

- [ ] **Step 3: Add tests for auto-serialization**

In `tests/test_reflection.cpp`, add:
- Test: reflect a component, auto-serialize to JSON, verify JSON structure
- Test: auto-deserialize from JSON, verify field values
- Test: missing field in JSON gets default value (forward compat)
- Test: alias lookup works

- [ ] **Step 4: Build, run tests, commit**

```bash
git add engine/ecs/component_meta.h engine/ecs/component_meta.cpp tests/test_reflection.cpp
git commit -m "feat(ecs): ComponentMetaRegistry with auto-generated JSON serialization"
```

---

## Task 4: Type-Erased addComponentById on World

**Files:**
- Modify: `engine/ecs/world.h`
- Modify: `engine/ecs/world.cpp`

- [ ] **Step 1: Add `addComponentById` method to World**

In `world.h`, add to the World class:
```cpp
// Type-erased component addition — used by serialization registry.
// Returns pointer to zero-initialized component memory in the archetype.
// Caller must construct and populate the component via the returned pointer.
void* addComponentById(EntityHandle handle, CompId id, size_t size, size_t alignment);
```

In `world.cpp`, implement:
1. Look up the entity from the handle
2. Register the type with ArchetypeStorage if not already registered (using size/alignment)
3. Build the new type set (current archetype types + new CompId)
4. Call `archetypes_.migrateEntity()` to move to new archetype
5. Return pointer to the new component's memory in the destination archetype column
6. Update entity's archetypeId_ and row_

- [ ] **Step 2: Add `unknownComponents_` to Entity**

In `entity.h`, add:
```cpp
#include <nlohmann/json_fwd.hpp>
// ... in class Entity:
std::unordered_map<std::string, nlohmann::json> unknownComponents_;
```

- [ ] **Step 3: Build, run tests, commit**

```bash
git add engine/ecs/world.h engine/ecs/world.cpp engine/ecs/entity.h
git commit -m "feat(ecs): type-erased addComponentById and unknown component preservation"
```

---

## Task 5: Add FATE_REFLECT to All Game Components

**Files:**
- Modify: `game/components/transform.h`
- Modify: `game/components/sprite_component.h`
- Modify: `game/components/box_collider.h`
- Modify: `game/components/animator.h`
- Modify: `game/components/player_controller.h`
- Modify: `game/components/zone_component.h`
- Modify: `game/components/game_components.h`

- [ ] **Step 1: Read each component file to identify its fields and types**

- [ ] **Step 2: Add `#include "engine/ecs/reflect.h"` and `FATE_REFLECT(...)` to each**

For each component, add a `FATE_REFLECT` block after the struct definition. Example for Transform:
```cpp
FATE_REFLECT(Transform,
    FATE_FIELD(position, Vec2),
    FATE_FIELD(rotation, Float),
    FATE_FIELD(depth, Float),
    FATE_FIELD(scale, Vec2)
)
```

For components with complex inner types (CharacterStats, Inventory, etc.), use `FATE_FIELD(fieldName, Custom)` — these need manual serializers registered in Task 6.

For components with only the `enabled` field and no data (DamageableComponent, marker components), either skip FATE_REFLECT or reflect only `enabled`:
```cpp
FATE_REFLECT(DamageableComponent) // no fields besides enabled
```

Note: The FATE_REFLECT macro must appear OUTSIDE any namespace, after the struct definition and its closing namespace brace.

- [ ] **Step 3: Build, fix any macro expansion issues**

Common issues:
- `offsetof` warnings on non-standard-layout types → suppress with pragma
- FATE_REFLECT inside a namespace → move after namespace closing brace
- Fields with no matching FieldType → use Custom

- [ ] **Step 4: Commit**

```bash
git add game/components/ game/systems/spawn_system.h
git commit -m "feat(game): add FATE_REFLECT to all game components"
```

---

## Task 6: Register All Components

**Files:**
- Create: `game/register_components.h`
- Modify: `game/game_app.cpp`

- [ ] **Step 1: Create `game/register_components.h`**

```cpp
#pragma once
#include "engine/ecs/component_meta.h"
#include "engine/ecs/component_traits.h"

// Include all game component headers
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/box_collider.h"
#include "game/components/animator.h"
#include "game/components/player_controller.h"
#include "game/components/zone_component.h"
#include "game/components/game_components.h"

namespace fate {

// Game-specific component_traits overrides
template<> struct component_traits<Transform> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable | ComponentFlags::Persistent;
};
template<> struct component_traits<SpriteComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};
template<> struct component_traits<BoxCollider> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};
template<> struct component_traits<PlayerController> {
    static constexpr ComponentFlags flags = ComponentFlags::None; // runtime only
};
template<> struct component_traits<MobAIComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None; // runtime only
};
template<> struct component_traits<TargetingComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None; // runtime only
};
template<> struct component_traits<NameplateComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None; // generated
};
template<> struct component_traits<MobNameplateComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None; // generated
};
// Social components — server-only, not in scene files
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
template<> struct component_traits<CharacterStatsComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable | ComponentFlags::Networked | ComponentFlags::Persistent;
};
template<> struct component_traits<InventoryComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable | ComponentFlags::Persistent;
};
template<> struct component_traits<EnemyStatsComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

inline void registerAllComponents() {
    auto& reg = ComponentMetaRegistry::instance();

    // Basic components — auto-serialized from reflection
    reg.registerComponent<Transform>();
    reg.registerComponent<BoxCollider>();
    reg.registerComponent<DamageableComponent>();
    reg.registerComponent<CombatControllerComponent>();

    // Components with custom serialization (shared_ptr, complex inner types)
    reg.registerComponent<SpriteComponent>(
        // Custom toJson
        [](const void* data, nlohmann::json& j) {
            auto* s = static_cast<const SpriteComponent*>(data);
            j["textureId"] = s->textureId;
            j["size"] = { s->size.x, s->size.y };
            j["sourceRect"] = { s->sourceRect.x, s->sourceRect.y, s->sourceRect.w, s->sourceRect.h };
            j["tint"] = { s->tint.r, s->tint.g, s->tint.b, s->tint.a };
            j["flipX"] = s->flipX;
            j["flipY"] = s->flipY;
        },
        // Custom fromJson
        [](const nlohmann::json& j, void* data) {
            auto* s = static_cast<SpriteComponent*>(data);
            s->textureId = j.value("textureId", std::string(""));
            if (j.contains("size")) { s->size = { j["size"][0], j["size"][1] }; }
            if (j.contains("sourceRect")) {
                auto& r = j["sourceRect"];
                s->sourceRect = { r[0], r[1], r[2], r[3] };
            }
            if (j.contains("tint")) {
                auto& c = j["tint"];
                s->tint = { c[0], c[1], c[2], c[3] };
            }
            s->flipX = j.value("flipX", false);
            s->flipY = j.value("flipY", false);
        }
    );

    // Register remaining components that have reflection
    // Components without custom serializers use auto-generated from FATE_REFLECT
    reg.registerComponent<Animator>();
    reg.registerComponent<PlayerController>();
    reg.registerComponent<ZoneComponent>();
    reg.registerComponent<PortalComponent>();
    reg.registerComponent<SpawnZoneComponent>();
    reg.registerComponent<MobAIComponent>();
    reg.registerComponent<EnemyStatsComponent>();
    reg.registerComponent<CharacterStatsComponent>();
    reg.registerComponent<InventoryComponent>();
    reg.registerComponent<SkillManagerComponent>();
    reg.registerComponent<StatusEffectComponent>();
    reg.registerComponent<CrowdControlComponent>();
    reg.registerComponent<TargetingComponent>();
    reg.registerComponent<NameplateComponent>();
    reg.registerComponent<MobNameplateComponent>();
    reg.registerComponent<ChatComponent>();
    reg.registerComponent<GuildComponent>();
    reg.registerComponent<PartyComponent>();
    reg.registerComponent<FriendsComponent>();
    reg.registerComponent<MarketComponent>();
    reg.registerComponent<TradeComponent>();

    // Backward-compat aliases for existing prefab files
    reg.registerAlias("Sprite", "SpriteComponent");
    reg.registerAlias("Transform", "Transform"); // canonical = same
}

} // namespace fate
```

- [ ] **Step 2: Call from GameApp::onInit()**

In `game/game_app.cpp`, add near the top of `onInit()`:
```cpp
#include "game/register_components.h"
// ...
void GameApp::onInit() {
    fate::registerAllComponents();
    // ... rest of init
}
```

- [ ] **Step 3: Build, run tests, commit**

```bash
git add game/register_components.h game/game_app.cpp
git commit -m "feat(game): explicit component registration with traits and serializers"
```

---

## Task 7: Upgrade Prefab System

**Files:**
- Modify: `engine/ecs/prefab.cpp`

- [ ] **Step 1: Read current prefab.cpp fully**

- [ ] **Step 2: Rewrite `entityToJson` to use ComponentMetaRegistry**

Replace the hardcoded `if (auto* t = entity->getComponent<Transform>())` chain with:
```cpp
nlohmann::json PrefabLibrary::entityToJson(Entity* entity) {
    nlohmann::json data;
    data["name"] = entity->name();
    data["tag"] = entity->tag();
    data["active"] = entity->isActive();
    nlohmann::json comps;

    entity->forEachComponent([&](void* ptr, CompId id) {
        auto* meta = ComponentMetaRegistry::instance().findById(id);
        if (!meta || !meta->toJson) return;
        if (!hasFlag(meta->flags, ComponentFlags::Serializable)) return;

        nlohmann::json compJson;
        meta->toJson(ptr, compJson);
        comps[meta->name] = compJson;
    });

    // Preserve unknown components
    for (auto& [name, blob] : entity->unknownComponents_) {
        comps[name] = blob;
    }

    data["components"] = comps;
    return data;
}
```

- [ ] **Step 3: Rewrite `jsonToEntity` to use ComponentMetaRegistry**

```cpp
Entity* PrefabLibrary::jsonToEntity(const nlohmann::json& data, World& world) {
    std::string name = data.value("name", "Entity");
    Entity* entity = world.createEntity(name);
    entity->setTag(data.value("tag", ""));
    entity->setActive(data.value("active", true));

    if (data.contains("components")) {
        auto& comps = data["components"];
        for (auto& [typeName, compJson] : comps.items()) {
            auto* meta = ComponentMetaRegistry::instance().findByName(typeName);
            if (!meta) {
                // Unknown component — preserve raw JSON
                entity->unknownComponents_[typeName] = compJson;
                continue;
            }
            if (!meta->fromJson) continue;

            // Add component via type-erased path
            void* ptr = world.addComponentById(entity->handle(), meta->id, meta->size, meta->alignment);
            if (ptr && meta->construct) meta->construct(ptr);
            if (ptr && meta->fromJson) meta->fromJson(compJson, ptr);
        }
    }

    return entity;
}
```

- [ ] **Step 4: Add tests for registry-based prefab save/load round-trip**

- [ ] **Step 5: Build, run tests, commit**

```bash
git add engine/ecs/prefab.cpp tests/test_reflection.cpp
git commit -m "feat(prefab): registry-based generic serialization replacing hardcoded type checks"
```

---

## Task 8: Scene Save/Load Upgrade

**Files:**
- Modify: `engine/scene/scene.cpp`

- [ ] **Step 1: Read current scene.cpp loadFromFile**

- [ ] **Step 2: Update `loadFromFile` to use registry-based deserialization**

Add version header parsing. Use `PrefabLibrary::jsonToEntity` for entity deserialization (it now uses the registry). Add two-pass deserialization if entity cross-references exist.

- [ ] **Step 3: Update scene save to use registry-based serialization**

Use `PrefabLibrary::entityToJson` for each entity. Add version header to output.

- [ ] **Step 4: Build, run, commit**

```bash
git add engine/scene/scene.cpp engine/scene/scene.h
git commit -m "feat(scene): registry-based save/load with version header"
```

---

## Task 9: Generic Inspector Fallback

**Files:**
- Modify: `engine/editor/editor.cpp`

- [ ] **Step 1: Read editor.cpp to understand current inspector rendering**

Find the inspector section where components are rendered with ImGui widgets.

- [ ] **Step 2: Add `drawReflectedComponent` function**

```cpp
void drawReflectedComponent(const ComponentMeta& meta, void* data) {
    for (const auto& field : meta.fields) {
        uint8_t* ptr = static_cast<uint8_t*>(data) + field.offset;
        switch (field.type) {
            case FieldType::Float:
                ImGui::DragFloat(field.name, reinterpret_cast<float*>(ptr), 0.1f);
                break;
            case FieldType::Int:
                ImGui::DragInt(field.name, reinterpret_cast<int*>(ptr));
                break;
            case FieldType::Bool:
                ImGui::Checkbox(field.name, reinterpret_cast<bool*>(ptr));
                break;
            case FieldType::Vec2: {
                auto* v = reinterpret_cast<Vec2*>(ptr);
                ImGui::DragFloat2(field.name, &v->x, 0.5f);
                break;
            }
            case FieldType::Color: {
                auto* c = reinterpret_cast<Color*>(ptr);
                ImGui::ColorEdit4(field.name, &c->r);
                break;
            }
            case FieldType::String: {
                auto* s = reinterpret_cast<std::string*>(ptr);
                char buf[256];
                strncpy(buf, s->c_str(), sizeof(buf));
                if (ImGui::InputText(field.name, buf, sizeof(buf))) {
                    *s = buf;
                }
                break;
            }
            default:
                ImGui::TextDisabled("%s: [%s]", field.name, "custom");
                break;
        }
    }
}
```

- [ ] **Step 3: Wire into inspector — fallback for components without manual UI**

After the existing manual inspector code, add a fallback pass:
```cpp
// Generic fallback for any reflected components not handled above
entity->forEachComponent([&](void* data, CompId id) {
    auto* meta = ComponentMetaRegistry::instance().findById(id);
    if (!meta || meta->fields.empty()) return;
    // Skip if already rendered by manual inspector above
    if (/* already handled */) return;

    if (ImGui::CollapsingHeader(meta->name)) {
        drawReflectedComponent(*meta, data);
    }
});
```

The exact integration depends on how the current inspector is structured — read the code first.

- [ ] **Step 4: Build, run game, verify inspector shows reflected fields**

- [ ] **Step 5: Commit**

```bash
git add engine/editor/editor.cpp
git commit -m "feat(editor): generic reflection-driven inspector fallback"
```

---

## Task 10: Final Integration & Verification

- [ ] **Step 1: Full clean build**

- [ ] **Step 2: Run all tests**

- [ ] **Step 3: Run game and verify**

- Launch game
- Open editor (F3)
- Create entity, add Transform, verify inspector shows reflected fields
- Save scene, load scene, verify round-trip preserves component data
- Spawn prefab, verify components deserialized correctly
- Verify existing gameplay works (mob AI, combat, spawning)

- [ ] **Step 4: Commit any remaining fixes**

```bash
git add -A
git commit -m "feat(engine): complete reflection and serialization system"
```
