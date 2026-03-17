# Reflection & Serialization System Design Spec

**Date:** 2026-03-17
**Scope:** Compile-time reflection, type-erased serialization registry, scene file format, prefab upgrade, component traits
**Phase:** Phase 1 of research gap implementation
**Depends on:** Archetype ECS (completed), CompId system (completed)

---

## Overview

This system adds field-level reflection to all engine/game components, enabling auto-generated inspectors, generic scene serialization, prefab inheritance, and future network replication — all driven from a single `FATE_REFLECT(...)` macro per component. Components are non-virtual aggregate structs (some contain non-trivially-destructible members like `std::string`, `std::vector`, `std::shared_ptr`; these are handled via the `FieldType::Custom` escape hatch and `ComponentMeta::destroy` function pointer).

**Design decisions:**
- Macro-based field registration (not nlohmann ADL-as-reflection)
- Explicit component registration in a central function (not auto-registration, to avoid MSVC linker stripping with static library)
- Scene version header included from day one; migration functions deferred until format stabilizes
- Dual-format pipeline skeleton (JSON for editing, MessagePack for runtime)

---

## Section 1: Reflection System

### Layer 1 — Field Descriptors (`engine/ecs/reflect.h`)

**FieldType enum** classifying field types for widget dispatch and serialization:
```cpp
enum class FieldType : uint8_t {
    Float, Int, UInt, Bool,
    Vec2, Vec3, Vec4, Color, Rect,
    String,         // std::string
    Enum,           // serialized as string via name lookup
    EntityHandle,   // 32-bit packed handle
    Direction,      // fate::Direction enum
    Custom          // requires manual serializer — used for complex inner types
};
```

**FieldInfo struct** — per-field metadata:
```cpp
struct FieldInfo {
    const char* name;       // field name (string literal)
    size_t offset;          // byte offset from struct start (via offsetof)
    size_t size;            // sizeof(field)
    FieldType type;         // type classification
};
```

**FATE_REFLECT macro** — generates a `Reflection<T>` template specialization with a static `fields()` method returning `std::span<const FieldInfo>`.

Usage:
```cpp
struct Transform {
    FATE_COMPONENT(Transform)
    Vec2 position;
    float rotation = 0.0f;
    float depth = 0.0f;
    Vec2 scale = Vec2::one();
};
FATE_REFLECT(Transform,
    FIELD(position, Vec2),
    FIELD(rotation, Float),
    FIELD(depth, Float),
    FIELD(scale, Vec2)
)
```

**Macro expansion:** The two-macro pattern uses the outer macro to inject the owner type:
```cpp
#define FATE_REFLECT(Type, ...) \
    template<> struct fate::Reflection<Type> { \
        static std::span<const fate::FieldInfo> fields() { \
            static const fate::FieldInfo f[] = { __VA_ARGS__ }; \
            return std::span<const fate::FieldInfo>(f); \
        } \
    };

#define FIELD(name, fieldType) \
    fate::FieldInfo{ #name, offsetof(FATE_REFLECT_TYPE, name), \
        sizeof(decltype(std::declval<FATE_REFLECT_TYPE>().name)), \
        fate::FieldType::fieldType }
```

The `FATE_REFLECT_TYPE` is injected by a wrapper that defines it before expansion. In practice, a cleaner approach uses a helper macro:
```cpp
#define FATE_REFLECT(Type, ...) \
    template<> struct fate::Reflection<Type> { \
        using _Type = Type; \
        static std::span<const fate::FieldInfo> fields() { \
            static const fate::FieldInfo f[] = { __VA_ARGS__ }; \
            return std::span<const fate::FieldInfo>(f); \
        } \
    };

#define FIELD(name, fieldType) \
    fate::FieldInfo{ #name, offsetof(_Type, name), sizeof(decltype(std::declval<_Type>().name)), fate::FieldType::fieldType }
```

Note: `offsetof` on non-standard-layout types (components with `std::string`, `std::shared_ptr` etc.) works on MSVC/GCC/Clang but emits a warning. Suppress with `#pragma warning(disable: 4200)` on MSVC.

The **`enabled` field** (injected by `FATE_COMPONENT` macro) is **always auto-included** by the FATE_REFLECT macro — it is appended to the user-provided field list automatically. This ensures enabled state is serialized without requiring boilerplate in every FATE_REFLECT call.

The default `Reflection<T>` (unspecialized) has an empty field list, so unreflected components are handled gracefully — they just can't be auto-serialized or auto-inspected.

**JSON format for compound types:**
- `Vec2` → JSON array `[x, y]` (matches existing prefab convention)
- `Vec3` → JSON array `[x, y, z]`
- `Vec4` → JSON array `[x, y, z, w]`
- `Color` → JSON array `[r, g, b, a]`
- `Rect` → JSON array `[x, y, w, h]`
- `Direction` → JSON string `"Up"`, `"Down"`, `"Left"`, `"Right"`, `"None"`
- `EntityHandle` → JSON string (entity name for cross-reference resolution)
- `String` → JSON string

Field access in the inspector and serializer uses `reinterpret_cast<T*>((uint8_t*)data + offset)`, NOT `memcpy`. This is safe for all field types including non-trivially-copyable ones like `std::string`.

### Layer 2 — Runtime Registry (`engine/ecs/component_meta.h`)

**ComponentMeta struct** — full type-erased metadata for one component type:
```cpp
struct ComponentMeta {
    const char* name;                    // "Transform"
    CompId id;                           // compile-time type ID
    size_t size;                         // sizeof(T)
    size_t alignment;                    // alignof(T)
    ComponentFlags flags;                // from component_traits<T>
    std::span<const FieldInfo> fields;   // from Reflection<T>

    // Type-erased operations
    void (*construct)(void*);            // placement-new default construct
    void (*destroy)(void*);              // call destructor (if non-trivial)
    void (*toJson)(const void*, nlohmann::json&);   // serialize
    void (*fromJson)(const nlohmann::json&, void*); // deserialize
};
```

**ComponentMetaRegistry** — Meyer's Singleton internally, populated by explicit `registerComponent<T>()` calls:
```cpp
class ComponentMetaRegistry {
public:
    static ComponentMetaRegistry& instance();

    template<typename T>
    void registerComponent();           // reads Reflection<T>, component_traits<T>, generates meta

    const ComponentMeta* findByName(const std::string& name) const;
    const ComponentMeta* findById(CompId id) const;

    void forEachMeta(std::function<void(const ComponentMeta&)> fn) const;
};
```

`registerComponent<T>()` performs:
1. Reads `T::COMPONENT_NAME` for the string name
2. Reads `componentId<T>()` for the CompId
3. Reads `Reflection<T>::fields()` for field metadata
4. Reads `component_traits<T>::flags` for flags
5. If no manual `toJson`/`fromJson` override, auto-generates them by iterating reflected fields
6. Stores the completed `ComponentMeta` in the registry

### Layer 3 — Inspector Integration

A generic `drawReflectedComponent(const ComponentMeta& meta, void* data)` function in `engine/editor/editor.cpp`:
- Iterates `meta.fields`
- Dispatches by `FieldType` to ImGui widgets:
  - `Float` → `DragFloat`
  - `Int` / `UInt` → `DragInt`
  - `Bool` → `Checkbox`
  - `Vec2` → 2x `DragFloat` with X/Y labels
  - `Vec3` → 3x `DragFloat`
  - `Color` → `ColorEdit4`
  - `String` → `InputText`
  - `Enum` → `BeginCombo` (requires enum name list, future work)
  - `EntityHandle` → display as "(idx, gen)" text
  - `Custom` → skip (use manual inspector)
- Reads/writes field data via `(uint8_t*)data + fieldInfo.offset`

Existing manual inspectors in `editor.cpp` continue to work. The editor's inspector loop checks if a `ComponentMeta` exists for the component's CompId. If a manual inspector function is registered for that type, it takes priority. Otherwise, `drawReflectedComponent` is used as the fallback. Over time, manual inspectors can be removed as the generic one covers more types.

**Undo integration:** The generic inspector captures field values before and after widget interaction using the same `(uint8_t*)data + offset` pointer. Before a widget modifies a value, the old value is copied. After modification, if the value changed, an undo command is recorded with old/new values and the field offset. This integrates with the existing `UndoManager` via a `PropertyChangeAction` that stores `{entityHandle, componentCompId, fieldOffset, oldBytes, newBytes}`.

**TypeInfo overlap:** The archetype system already has a `TypeInfo` struct in `archetype.h` with `size`, `alignment`, and `destroyFn`. The `registerComponent<T>()` call also calls `archetypes_.registerType<T>()` so both registries stay in sync. `ComponentMeta` is a superset of `TypeInfo` — it adds the reflection fields, name, and serialization function pointers. They are not deduplicated; `TypeInfo` is the archetype's internal concern, `ComponentMeta` is the external metadata layer.

---

## Section 2: Serialization System

### Serializer Registry (`engine/ecs/serializer_registry.h`)

Uses the `ComponentMetaRegistry` — no separate registry needed. The `toJson`/`fromJson` function pointers on `ComponentMeta` ARE the serializer registry.

### Auto-generated `to_json` / `from_json`

For components with all basic reflected fields, serialization is auto-generated from the `FieldInfo` array:

```
to_json: iterate fields, switch on FieldType, write field name → value
from_json: iterate fields, switch on FieldType, read j.value(name, default)
```

Using `j.value(name, default)` fills missing fields from a default-constructed component — automatic forward compatibility.

Components with `FieldType::Custom` fields or complex serialization needs register manual overrides via:
```cpp
template<typename T>
void registerComponent(
    void (*customToJson)(const void*, nlohmann::json&) = nullptr,
    void (*customFromJson)(const nlohmann::json&, void*) = nullptr
);
```

If custom functions are provided, they replace the auto-generated versions.

### Scene File Format

```json
{
    "version": 1,
    "name": "Whispering Woods",
    "metadata": {
        "sceneType": "zone",
        "minLevel": 5,
        "maxLevel": 12,
        "pvpEnabled": false
    },
    "entities": [
        {
            "name": "Goblin_01",
            "tag": "mob",
            "active": true,
            "components": {
                "Transform": { "position": [128.0, 256.0], "rotation": 0.0, "depth": 5.0, "scale": [1.0, 1.0] },
                "SpriteComponent": { "textureId": "goblin", "size": [32, 32] },
                "EnemyStatsComponent": { "level": 7, "baseHP": 150 }
            }
        }
    ]
}
```

### Key Behaviors

**Forward compatibility:** Missing fields in JSON filled from default-constructed component. Old scene files load cleanly when new fields are added.

**Unknown component preservation:** If a component type name in JSON isn't in the registry, store the raw `nlohmann::json` object as an opaque blob attached to the entity. On re-save, write it back unchanged. This prevents data loss from version mismatches.

**Two-pass deserialization:**
- Each entity in the scene file gets a stable `"id"` field — a string UUID or sequential integer unique within the scene file. Entity names are NOT unique and cannot be used as keys.
- Pass 1: Create all entities, build `scene_id → EntityHandle` mapping table
- Pass 2: Deserialize components, resolving entity references (e.g., targeting, parent-child) through the map. Entity reference fields (FieldType::EntityHandle) are serialized as the target entity's `"id"` string value, then resolved through the mapping table.

**Selective serialization:** Only entities/components whose `component_traits<T>::flags` include `ComponentFlags::Serializable` are written to scene files. Transient components (particles, debug overlays, runtime AI state) are skipped.

**Component name aliases for backward compatibility:** The `ComponentMetaRegistry` supports alias registration:
```cpp
registry.registerAlias("Sprite", "SpriteComponent");
registry.registerAlias("BoxCollider", "BoxColliderComponent");
```
This maps old prefab JSON keys (e.g., `"Sprite"`) to the canonical `COMPONENT_NAME` (e.g., `"SpriteComponent"`). Existing prefab files continue to work without migration.

**Unknown component preservation:** When a component type name in JSON isn't found in the registry, the raw `nlohmann::json` object is stored in a per-entity `std::unordered_map<std::string, nlohmann::json> unknownComponents_` map on the Entity class. On re-save, these blobs are written back unchanged. This prevents data loss when loading files from a newer engine version.

**Type-erased addComponent on World:** A new method on World enables adding components by CompId without compile-time type knowledge:
```cpp
void* World::addComponentById(EntityHandle handle, CompId id, size_t size, size_t alignment);
```
This allocates space in the archetype column (triggering migration if needed) and returns a pointer to the uninitialized memory. The caller (serialization registry) then calls `ComponentMeta::construct(ptr)` followed by `ComponentMeta::fromJson(json, ptr)` to populate it. This is the critical bridge enabling "generic spawn by string name" in the prefab system.

**Dual-format pipeline (skeleton):**
- `saveScene(path, format)` where format is `JSON` or `MessagePack`
- JSON: `nlohmann::json` → file
- MessagePack: same `nlohmann::json` object → `nlohmann::json::to_msgpack()` → file
- Both use identical in-memory representation; format is just the output encoding
- Default to JSON in development, MessagePack in release builds

---

## Section 3: Component Traits (`engine/ecs/component_traits.h`)

```cpp
enum class ComponentFlags : uint32_t {
    None         = 0,
    Serializable = 1 << 0,  // saved to scene files
    Networked    = 1 << 1,  // replicated to clients (future)
    EditorOnly   = 1 << 2,  // stripped in runtime builds (future)
    Persistent   = 1 << 3,  // saved to zone snapshots
};

// Bitwise operators for combining flags
constexpr ComponentFlags operator|(ComponentFlags a, ComponentFlags b);
constexpr ComponentFlags operator&(ComponentFlags a, ComponentFlags b);
constexpr bool hasFlag(ComponentFlags flags, ComponentFlags test);

// Default: all components are serializable
template<typename T>
struct component_traits {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};
```

Override per type. **Game-specific overrides go in `game/register_components.h`** (not in the engine's `component_traits.h`) to avoid engine→game circular dependencies:

```cpp
// In game/register_components.h:
template<> struct component_traits<MobAIComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None; // server-side runtime state
};
template<> struct component_traits<Transform> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable | ComponentFlags::Persistent;
};
template<> struct component_traits<CharacterStatsComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable | ComponentFlags::Networked | ComponentFlags::Persistent;
};
```

**Component classification table:**

| Component | Flags | Serialization |
|---|---|---|
| Transform | Serializable, Persistent | Auto (all basic fields) |
| SpriteComponent | Serializable | Custom (`shared_ptr<Texture>` → serialize textureId string) |
| BoxCollider | Serializable | Auto |
| Animator | Serializable | Custom (`unordered_map` frames) |
| PlayerController | None | Runtime only |
| MobAIComponent | None | Runtime only (reconstructed from spawn data) |
| EnemyStatsComponent | Serializable | Custom (inner `EnemyStats` class) |
| CharacterStatsComponent | Serializable, Networked, Persistent | Custom (inner `CharacterStats` class) |
| InventoryComponent | Serializable, Persistent | Custom (inner `Inventory` class) |
| SkillManagerComponent | Serializable, Persistent | Custom (inner `SkillManager` class) |
| SpawnZoneComponent | Serializable | Custom (vectors, spawn rules) |
| DamageableComponent | Serializable | Auto (marker, minimal data) |
| TargetingComponent | None | Runtime only |
| CombatControllerComponent | Serializable | Auto |
| All social components | None | Server-side, not in scene files |
| NameplateComponent | None | Generated from stats at runtime |
| MobNameplateComponent | None | Generated from stats at runtime |
| GhostFlag | None | Runtime only |

**Note on CompId stability:** CompIds (uint32_t counter) are NOT stable across builds — their values depend on `registerComponent<T>()` call order. The scene format uses string names for this reason. Never serialize CompIds to disk or network.

---

## Section 4: Prefab System Upgrade

### Inheritance

Prefab JSON files support an `"inherits": "BasePrefabName"` field. On load:
1. Load the base prefab recursively (supports chains: `A inherits B inherits C`)
2. Deep-merge the child's component overrides onto the base's components
3. Bake the merged result into a flat template

Circular inheritance detected and rejected with an error log.

### Registry Integration

`PrefabLibrary::spawn(World&, prefabName)` now:
1. Looks up the baked prefab template by name
2. Creates an entity
3. For each component in the template's JSON:
   - Looks up the `ComponentMeta` by string name in `ComponentMetaRegistry`
   - Calls `meta.construct(ptr)` to default-construct in archetype storage
   - Calls `meta.fromJson(json, ptr)` to deserialize from the template
4. Returns the entity handle

This replaces the hardcoded `if (type == "Transform") { ... }` chain in the current `prefab.cpp`.

### Override Detection (Future)

Not implemented now. When needed: compare each field of an instance against the base prefab's default values using `Reflection<T>::fields()` + `memcmp` per field. Only differing fields are saved in the instance's override JSON. The reflection system makes this field-by-field comparison possible.

---

## Files

### New Files
| File | Responsibility |
|---|---|
| `engine/ecs/reflect.h` | FATE_REFLECT macro, FieldInfo, FieldType, Reflection<T> specializations |
| `engine/ecs/component_meta.h` | ComponentMeta struct, ComponentMetaRegistry |
| `engine/ecs/component_traits.h` | ComponentFlags enum, component_traits<T> defaults and overrides |
| `game/register_components.h` | registerAllComponents() — explicit registration of all 27+ components |
| `tests/test_reflection.cpp` | Reflection field iteration, auto-serialization, scene save/load tests |

### Modified Files
| File | Changes |
|---|---|
| `engine/ecs/prefab.h` / `prefab.cpp` | Use ComponentMetaRegistry for generic spawn, add inheritance support |
| `engine/ecs/entity.h` | Add `unknownComponents_` map for preserving unregistered component JSON |
| `engine/ecs/world.h` / `world.cpp` | Add `addComponentById(handle, CompId, size, alignment)` type-erased method |
| `engine/editor/editor.cpp` | Add generic `drawReflectedComponent()` fallback inspector |
| `engine/scene/scene.h` / `scene.cpp` | Updated scene save/load using serializer registry, version header, two-pass deserialization |
| `game/game_app.cpp` | Call `registerAllComponents()` in `onInit()` |
| `game/components/*.h` | Add `FATE_REFLECT(...)` macro to each component |
| `game/components/game_components.h` | Add `FATE_REFLECT(...)` to all wrapper components |

---

## What This Does NOT Include

- Migration functions for scene versioning (deferred — version header present but no v1→v2 transforms)
- Prefab override detection / delta serialization (future — reflection makes it possible)
- Network binary serializer (future — shares reflection metadata but uses bitstream format)
- Auto-registration of components (explicit registration chosen to avoid linker stripping)
- Enum reflection / name lookup (future — magic_enum or manual registration)
- Full MessagePack pipeline (skeleton only — nlohmann's `to_msgpack` is trivial to wire up)
