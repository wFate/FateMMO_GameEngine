# Advanced custom game engine editor design for a 2D MMORPG

Building a professional-grade editor for a C++23/SDL2/OpenGL 3.3/ImGui/ECS engine requires mastering six architectural pillars: runtime separation, live inspection, prefab systems, database synchronization, advanced UI patterns, and modern C++ techniques. This report synthesizes patterns from Lumix Engine, Hazel, Godot, Unreal, O3DE, EnTT, flecs, and dozens of production engines to provide a concrete implementation roadmap. Each section delivers specific architectural patterns, code examples, and library recommendations targeted at a custom 2D MMORPG editor stack.

The key insight threading through all six topics is that **serialization is the universal decoupling layer** — it separates editor from runtime, enables play-in-editor snapshots, drives prefab override propagation, bridges ECS to database, and powers undo/redo. Design every component to be serializable from day one.

---

## 1. How professional engines separate editor from game runtime

### The layered library architecture every custom engine should adopt

Professional engines universally separate editor and runtime through **library boundaries with one-way dependencies**. The editor depends on the runtime; the runtime never depends on the editor. Four production engines illustrate the spectrum of approaches:

**Lumix Engine** uses a plugin-based architecture where `src/engine/` contains the core runtime and `src/editor/` contains all editor code. Each subsystem (renderer, physics, audio) is a plugin that can provide both runtime components and editor GUI extensions via `StudioApp::GUIPlugin`. The standalone player (`src/app/main.cpp`) loads the same engine plugins without any editor UI — it simply calls `engine->deserializeProject()` and runs. This is the cleanest C++ reference implementation for a custom engine.

**Hazel Engine** (TheCherno) uses **Premake** to generate a multi-project build: `Hazel/` as a shared static library, `Hazelnut/` as the editor executable, `Sandbox/` as a test app, and `Hazel-Runtime/` for shipping. Both editor and game link against the same core library but push different application layers. Hazel's `Layer` system (`Application::PushLayer`) means the editor pushes an `EditorLayer` with panels and gizmos while the game pushes gameplay layers.

**Godot** uses conditional compilation with the **`TOOLS_ENABLED`** preprocessor define. The entire `editor/` directory is guarded by `#ifdef TOOLS_ENABLED`. Three binary types emerge from the same source tree: full editor, debug export template, and release export template. The `@tool` annotation lets scripts run in the editor, and `Engine.is_editor_hint()` distinguishes contexts at runtime.

**Unreal Engine** employs the most sophisticated module system with explicit module types (`Runtime`, `Editor`, `DeveloperTool`, `UncookedOnly`). Editor modules are declared in `.uproject` files and **never included in packaged builds**. The `WITH_EDITOR` macro gates editor-only code within runtime modules. Editor modules depend on runtime modules (one-way), and `FModuleManager` handles dynamic loading.

For a custom C++23 engine, the recommended CMake structure creates four targets:

- **`engine_common`** (static library): ECS core, component definitions, math, serialization — shared by everything
- **`engine_runtime`** (static library): Renderer, audio, physics, networking — links `engine_common`
- **`editor`** (executable): Links `engine_runtime` + Dear ImGui, defines `EDITOR_BUILD`
- **`game_player`** (executable): Links `engine_runtime` only, no ImGui, no editor overhead

This architecture ensures editor-only components (gizmos, selection highlights, debug visualization) compile only in editor builds via `#ifdef EDITOR_BUILD`, while shared component definitions live in header-only files included by both targets.

### Hot-reloading game code without restarting the editor

DLL hot-reloading in C++ follows a well-established pattern: the host executable owns all persistent state, the game logic DLL provides function pointers (`startup`, `update`, `shutdown`, `begin_reload`, `end_reload`), and a file watcher triggers reload when the DLL timestamp changes. The critical trick is **copying the DLL before loading it** so the original file remains unlocked for recompilation — this is the approach used by both Lumix Engine and the `cr.h` single-header library.

State preservation across reloads demands that all game state lives in memory owned by the host, not the DLL. Function pointers must never be stored persistently because they become invalid after reload. C++ vtables in DLL classes break on reload because vtable pointers become stale — avoid virtual dispatch on persistent objects or use C-style function pointer tables. For MSVC, PDB file locking during debugging requires either copying and patching the PDB path or using a detach/reattach workflow.

**ezEngine** takes a radical approach: the editor and engine run as **separate processes** connected via IPC. If the engine process crashes, the editor survives and relaunches a new engine process, synchronizing scene state. Hot-reload becomes trivial — kill the engine process, recompile, relaunch with the updated DLL.

For a 2D MMORPG, a **scripting layer** (Lua via LuaJIT or C# via Mono/CoreCLR) provides the best iteration speed for game logic without C++ recompilation. Lumix uses Lua scripts that reload instantly. Reserve C++ hot-reloading for systems-level code that changes infrequently.

### Play-in-editor mode through ECS state snapshots

The gold standard for play-in-editor is the **serialization round-trip**: serialize the entire ECS world to a memory buffer before entering play mode, run the game loop against a copy, and discard the copy on stop — the original edit-state buffer remains untouched. Unity uses this exact approach, which is why entering/exiting Play mode can be slow with large scenes.

For a custom ECS, the implementation is straightforward because ECS component data is typically POD. Each component type registers serialize/deserialize functions. Entity relationships use entity IDs (not pointers), which survive serialization. On play, strip editor-only components (`EditorSelectionComponent`, `GizmoComponent`) from the play world, initialize game systems, and render into a **framebuffer object (FBO)** displayed as an `ImGui::Image` in a docked viewport. The UV coordinates `ImVec2(0,1), ImVec2(1,0)` flip the Y-axis for OpenGL's coordinate system.

System scheduling separates into four phases: **`EditorOnly`** (gizmos, selection, grid — runs only in edit mode), **`GameOnly`** (AI, networking — runs in play/standalone), **`PlayModeOnly`** (physics, collision — runs in play mode within the editor), and **`Always`** (rendering, transform propagation). A `SystemScheduler` checks the current editor mode against each system's phase before executing.

### The separate-process vs. embedded-editor tradeoff

Embedded editors (Unity model) offer zero-latency state access and simpler debugging but crash alongside the game. Separate-process editors (ezEngine model) survive game crashes and simplify hot-reloading but require an IPC protocol and duplicate some state. **For a small-to-medium custom engine, the embedded approach is recommended** — it has lower initial complexity, and crash resilience can be partially addressed through careful exception handling and autosave. Consider separating processes only if stability becomes critical as the project scales.

---

## 2. Building a live memory inspector and debugger for a custom ECS

### Runtime reflection enables generic component inspection

The centerpiece of any ECS editor is the property inspector — a panel that displays and edits any component's fields without hardcoded UI per type. This requires a **reflection system** that maps component types to their field names, types, and memory offsets.

Four practical approaches exist for C++23 engines:

**EnTT's meta system** is the most elegant if using EnTT. It provides macro-free, non-intrusive reflection: `entt::meta<Health>().data<&Health::hp>("hp"_hs).prop("display_name"_hs, "HP")`. The `entt::as_ref_t` tag enables zero-copy writes — modifications through `meta_any` write directly to the component. A generic inspector iterates all storages in the registry, resolves each entity's components to `meta_type`, and dispatches field drawing to a **type → widget map** (e.g., `float → ImGui::DragFloat`, `Vec2 → ImGui::DragFloat2`).

**Macro-based registration** (RTTR library pattern) wraps component fields in a registration block: `rttr::registration::class_<Position>("Position").property("x", &Position::x)`. Runtime iteration over properties enables generic display. This works with any ECS, not just EnTT.

**Lumix Engine's visitor pattern** is the most architecturally clean for custom engines. A `reflection::IPropertyVisitor` interface has virtual methods for each property type (`visit(FloatProperty&)`, `visit(IntProperty&)`, etc.). An `ImGuiPropertyVisitor` implementation renders the appropriate widget for each type. Properties carry attributes (`MinMaxAttribute`, `EnumAttribute`, `ResourceAttribute`) that control widget behavior.

**refl-cpp** provides compile-time reflection via macros: `REFL_AUTO(type(Position), field(x), field(y))`. All processing happens at compile-time through `refl::util::for_each`, which iterates members using `constexpr` iteration. This gives zero runtime overhead but increases compilation times past ~250 reflected members.

The **C++26 reflection proposal (P2996)** will eventually provide native `^T` reflection operators and `[:m:]` splicers, enabling `template for (constexpr auto m : std::meta::nonstatic_data_members_of(^T))` — but this is not yet available in any compiler.

### Time-travel debugging through frame state recording

Three approaches exist for recording ECS state for playback, in decreasing memory cost:

**Full state snapshots** serialize the entire ECS each frame. Simple but memory-intensive (~2-10 MB/frame). Best for short recording windows (last 300 frames). Fast random access to any frame.

**Delta compression** stores only changes between frames. A `FrameDelta` structure records added, removed, and modified entities per component type, with per-field changes tracked via memory offset and size. Periodic keyframes (every 60 frames) enable reasonably fast seeking. Typical overhead drops to **1-5 KB per frame** for most ECS games — a 3-4 order of magnitude reduction.

**Command/event sourcing** records only inputs and replays them deterministically. Overwatch's replay system uses this approach, leveraging its ECS-based deterministic simulation with fixed **16ms command frames**. Replay data equals serialized input commands plus periodic world state snapshots. The kill cam repurposes the network replication model — replaying is essentially "replaying network packets to a local client." This is the most memory-efficient approach (**~2.2 bytes/frame** for input) but requires fully deterministic simulation and cannot jump to arbitrary frames without replay from the last snapshot.

For the scrubbing UI, ImGui provides a `SliderInt` for frame selection with VCR-style transport controls (play, pause, step forward/back, jump to start/end). The reconstructed state feeds a read-only display in the inspector panel.

### Thread-safe state reading via double buffering

When the game loop runs on a separate thread from the editor UI, **double buffering** is the most practical approach. At the end of each game frame, the game thread serializes editor-visible state (selected entity's components, pool statistics, watch values) into a write buffer, then atomically swaps the read/write buffer indices using `std::atomic<int>` with `memory_order_acquire`/`memory_order_release`. The editor thread reads from the immutable read buffer without any locks.

For high-frequency watch values, a **SPSC (single-producer, single-consumer) lock-free ring buffer** connects the game thread to the editor thread. This avoids serializing entire state when only specific values need monitoring. The ring buffer uses atomic head/tail indices with appropriate memory ordering — no mutexes required.

ImGui itself is not thread-safe — always call ImGui functions from a single thread and feed it the double-buffered snapshot data. Never have the game thread write to ImGui state directly.

---

## 3. Prefab inheritance, variants, and override propagation

### A three-tier hierarchy drives MMORPG character customization

The prefab system follows a **base → variant → instance** hierarchy. A base prefab defines all default component values. A variant prefab inherits from the base, storing only a "diff" of changes. An instance in the scene can further override individual properties. This maps directly to MMORPG character classes:

The `BasePlayer` prefab contains shared components (Transform, Health{100}, Movement{5.0}, NetworkSync, Collider, Inventory). `WarriorPlayer` is a variant overriding Health{maxHp:200} and Movement{baseSpeed:4.0}, adding MeleeAttack, HeavyArmor, and Rage. `MagePlayer` overrides Health{maxHp:80}, adds SpellCasting, ManaPool, and ElementalAffinity. `ArcherPlayer` overrides Health{maxHp:120}, adds RangedAttack, Quiver, and Stealth. Equipment and cosmetic changes become **instance overrides** on top of class variants — a specific warrior with enchanted armor modifies HeavyArmor{defense:45} as a per-instance patch.

### JSON Patch (RFC 6902) is the serialization standard for override diffs

O3DE (Amazon's Open 3D Engine) uses **JSON Patch** for all prefab overrides, and this is the most robust approach. Each variant or instance stores a `"patches"` array of operations:

```json
{
  "prefab_id": "warrior_player",
  "parent_prefab": "base_player",
  "patches": [
    { "op": "replace", "path": "/components/Health/maxHp", "value": 200 },
    { "op": "add", "path": "/components/MeleeAttack", "value": { "damage": 25, "range": 2.0 } },
    { "op": "remove", "path": "/components/DebugVisual" }
  ]
}
```

**JSON Pointer (RFC 6901)** provides the property path addressing scheme: `/components/Transform/position/x`. The `nlohmann/json` library has built-in `json::diff()` and `json::patch()` implementing both RFCs. For runtime/fast loading, a binary format with a `BinaryPatchOp` header (op byte, path length, value size, followed by path and value bytes) provides compact serialization.

Three override types must be supported: **property overrides** (change an existing value), **component additions** (add a component not in the parent), and **component removals** (mark a parent component as removed). O3DE's original "Slice" system suffered from unintuitive nested override chains — their redesign focused on making each prefab "own" its save context clearly.

### How Unity, Godot, and Unreal approach prefab inheritance differently

**Unity** (2018.3+) stores prefabs as YAML files. Prefab variants chain (Base → Variant A → Variant B), and nested prefabs retain links to their own assets. Override serialization uses `propertyPath` strings (e.g., `m_LocalPosition.x`). Overridden properties appear **bold with a blue left-margin indicator** in the Inspector. Unity's nested prefab redesign took 1.5 years of focused work involving 150+ enterprise customer interviews.

**Godot** unifies scenes and prefabs into a single concept. Scene inheritance creates a child scene storing only overridden properties. Changes to the base scene automatically propagate to all inherited scenes. The limitation is that scene inheritance cannot be changed after creation.

**Unreal** uses class-level inheritance (Blueprints are classes), not instance-level override tracking. There is no equivalent of "prefab variant" — instead you create new Blueprint subclasses. Class Default Objects (CDOs) hold defaults; instances inherit from CDOs with per-instance overrides.

### Editor UI patterns for override management in Dear ImGui

The property inspector must visually distinguish overridden values from inherited ones. Push a **bold font and blue tint** (`ImGui::PushFont(boldFont)` + `ImGui::PushStyleColor`) for overridden properties, and a grayed style for inherited values. Draw a 3-pixel-wide blue indicator bar on the left margin using `ImGui::GetWindowDrawList()->AddRectFilled()`. Component headers with any overrides get a blue dot indicator.

Right-click context menus on any property should offer "Revert to Prefab Value" (removes the override), "Apply to Prefab" (pushes the instance value back to the base definition), and "Copy Property Path" (copies the JSON Pointer path for debugging). Component-level context menus offer "Remove Component Override" and "Apply Component to Prefab."

### Live prefab editing propagates changes through an observer pattern

When a prefab definition changes, all instances must update. A `Signal<const PrefabChangeEvent&>` on the `PrefabDefinition` class emits on any property change. Instances register as listeners and check whether they have a local override on the changed path — if so, the **instance override takes precedence** and the instance optionally gets flagged as "conflicting" for UI display. If no local override exists, the instance recomputes the value from the updated prefab.

Performance considerations for large instance counts include **lazy evaluation** (mark instances dirty, recompose on next access), **batch propagation** (collect all changes during an edit session, emit a single event), and an **instance registry** (`std::unordered_map<PrefabID, std::vector<PrefabInstance*>>`) for O(1) lookup of all instances of a given prefab.

### Database storage stores only the delta

For an MMORPG, the resolution chain at runtime is: load base prefab from asset files → apply class variant patches → apply equipment-derived overrides → apply player-specific instance overrides. The database stores only the player's **delta from their class variant**:

```sql
CREATE TABLE player_characters (
    character_id    UUID PRIMARY KEY,
    prefab_variant  VARCHAR(64) NOT NULL,  -- "WarriorPlayer", "MagePlayer"
    overrides_json  JSONB,                 -- JSON Patch array of instance overrides
    equipment_json  JSONB                  -- Equipment-derived additional overrides
);
```

This is both space-efficient and enables the editor to understand exactly what the player has customized versus what comes from their class template.

---

## 4. Synchronizing ECS components with PostgreSQL for an MMORPG

### Memory is the source of truth, not the database

The single most critical architectural principle for MMORPG servers, confirmed by multiple expert sources (PRDeving's MMO Architecture series, IT Hare's server-side architecture guide, Amazon's New World architecture), is that **the authoritative game state lives in memory, not the database**. The database is a persistence medium for crash recovery and session continuity. With 1,000+ concurrent players generating position updates every tick, writing every change to PostgreSQL would produce ~100,000+ transactions/second — infeasible. Write-back caching reduces database transactions by **3-4 orders of magnitude**.

### Mapping components to relational tables with dirty tracking

Each persistent component type maps to its own table (`comp_position`, `comp_stats`, `comp_inventory`, `comp_equipment`), keyed by `entity_id`. Transient components (Velocity, AnimationState, NetworkBuffer, PathfindingCache) exist only in memory and are never persisted.

Dirty tracking uses a `Tracked<T>` wrapper around each persistent component. The wrapper contains the component data, a `bool dirty` flag, and a `uint32_t version` counter. Systems mutate components through a `mutate()` accessor that automatically sets the dirty flag and increments the version. A `PersistenceSystem` queries all dirty components, batches SQL writes, then clears flags. The version counter also serves double duty for **optimistic locking** in conflict resolution.

EnTT provides reactive change detection through `registry.on_update<T>()` signals and `entt::observer` collectors. Flecs has built-in change detection via `detect_changes()` on query builders, tracking changes at the archetype table level using generation counters. Both approaches can feed a persistence system.

### Batching and throttling writes to avoid overwhelming PostgreSQL

**Staggered saves** spread player persistence across ticks rather than saving everyone simultaneously. A `StaggeredSaveScheduler` processes **10-50 players per tick**, cycling through the full player list over a 30-60 second interval.

**Priority-based flushing** ensures critical data reaches the database fast:

- **IMMEDIATE**: Inventory changes, trades, purchases — flush this tick
- **HIGH**: Level ups, quest completions — flush within 5 seconds
- **NORMAL**: Position, minor stats — flush within 30-60 seconds
- **LOW**: Cosmetics, preferences — flush on logout or every 5 minutes

For PostgreSQL batch writes, the **`INSERT...UNNEST ON CONFLICT`** pattern is 50%+ faster than multi-row VALUES for large batches. Each column is passed as a PostgreSQL array parameter, bypassing the 32,767 parameter limit that constrains VALUES-based batches. Always use prepared statements — they skip parse/plan phases after first execution.

The game loop must **never block on database I/O**. A dedicated `AsyncDBExecutor` thread pool (using `std::jthread` with stop tokens) consumes work from a lock-free concurrent queue (`moodycamel::ConcurrentQueue`). The C++ PostgreSQL client options are **libpqxx** (official, BSD license, requires C++20 for v8.x), **taoPQ** (modern C++ with built-in connection pools and pipeline mode), or **postgrespp** (Boost.ASIO-based async driver).

### Conflict resolution when editor and game server collide

The recommended primary approach routes all admin/editor modifications **through the game server as RPC commands**, not as direct database writes. This ensures a single writer to ECS state — no race conditions, server validates admin commands, changes flow through normal dirty tracking and persistence.

When direct database access is necessary, **optimistic locking with version numbers** provides conflict detection:

```sql
UPDATE comp_position SET x=$2, y=$3, version=version+1
WHERE entity_id=$1 AND version=$4
RETURNING version;
```

If zero rows return, a version mismatch occurred — the client retries with fresh data. Production MMOs (WoW, FFXIV) use **in-band admin commands** via GM tools that issue server commands to modify authoritative in-memory state. Direct database modifications are reserved for maintenance windows.

### The editor-to-live-server content pipeline

The full data pipeline flows from the editor through asset baking to server deployment:

1. Content editors export **JSON asset files** (item definitions, NPC templates, skill tables, zone layouts), version-controlled in Git
2. A CI/CD asset build pipeline validates JSON, bakes to optimized binary (FlatBuffers or MessagePack), and runs automated tests
3. Binary assets deploy to the game server via rsync/S3 sync
4. The server hot-swaps **data-only assets** (balance values, NPC stats, item definitions) via file watching and atomic `shared_ptr` swaps — no restart required
5. Player instance data (inventory, stats, quest progress) loads from PostgreSQL on login
6. Database schema changes (new columns/tables) require migration scripts run during maintenance windows

For hot-reloadable data assets, design the asset system with an `AssetManager` that watches the asset directory using platform-specific APIs (`inotify` on Linux, `ReadDirectoryChangesW` on Windows, or the cross-platform `efsw` library). On file change, load and validate the new data, then atomically swap the `shared_ptr` — existing references to the old data remain valid until their last user releases them.

---

## 5. Advanced editor UI patterns for Dear ImGui

### Node-based editors for skill trees, dialogue, and AI behavior trees

Two mature libraries enable visual node editing in Dear ImGui. **imnodes** (Nelarius/imnodes) is lightweight and truly immediate-mode — a single header+source with no dependencies beyond ImGui. Its API matches ImGui's style: `ImNodes::BeginNodeEditor()` → `BeginNode(id)` → embed any ImGui widgets → `EndNodeEditor()`. Best for simple graphs like skill trees and dialogue branching.

**imgui-node-editor** (thedmd/imgui-node-editor, ~4K GitHub stars) is more feature-rich and production-proven in the Spark CE engine. It provides smooth zooming with fringe-scale rendering, canvas panning, **group nodes** (critical for organizing quest phases), flow animations on links, and JSON-based layout persistence. Best for complex systems like AI behavior trees and visual scripting.

For AI behavior trees, composite nodes (Sequence, Selector, Parallel), decorator nodes, and leaf/action nodes each get distinct colors. The runtime graph builds from the visual representation via topological sort (DAGs) or tree traversal, with each node type implementing an `Evaluate()` method and data flowing through pins via a shared blackboard.

### The ImGuizmo repository provides five widget categories in one package

**CedricGuillemet/ImGuizmo** is an essential library containing not just 3D gizmos but also:

- **ImSequencer**: Timeline sequencer with named tracks (Camera, Music, FadeIn, Animation), custom draw delegates for compact/expanded views, and frame range editing. Each cutscene event maps to a track item with start/end frames.
- **ImCurveEdit**: Delegate-based curve editor with smooth/linear/stepped interpolation, supporting multiple curves with different colors.
- **ImGradient**: Color gradient editor with add/remove color stops, drag to reposition — ideal for particle system color-over-lifetime.
- **ImZoomSlider**: Range/zoom slider that integrates with ImSequencer.

For bezier easing curves specifically, **r-lyeh's Bezier widget** (public domain, ImGui Issue #786) provides a single cubic bezier control returning `BezierValue(t, v)` for evaluation.

### Unified undo/redo through the command pattern

Every state mutation in the editor flows through a `CommandHistory` that stores `std::unique_ptr<ICommand>` objects. Each command implements `execute()` and `undo()`. A `CompositeCommand` groups atomic changes (e.g., moving multiple selected entities). **Command merging** prevents undo history pollution during continuous operations like dragging: a `SetPropertyCommand::mergeWith()` method checks if the next command modifies the same entity and property, keeping only the latest value.

Godot's `UndoRedo` class provides useful design features worth emulating: **merge modes** (disable, merge endpoints, merge all), configurable max history size, reference tracking to prevent premature object deletion during undo, and per-scene undo histories. Unreal uses `FScopedTransaction` objects where modified UObjects snapshot their state before changes.

Memory management for the undo stack should limit history size (100-500 steps), and for deleted objects, use an unlinking/relinking pattern rather than freeing memory while the object is referenced in the undo stack.

### Asset browser, console, notifications, and profiling overlay

The **asset browser** uses `ImGui::ImageButton()` in a grid layout for thumbnail display, with `ImGui::InputText()` for search filtering. Thumbnails render sprites into small FBOs (64×64) cached in an LRU texture cache. Dear ImGui's built-in drag-and-drop (`ImGui::BeginDragDropSource`/`ImGui::BeginDragDropTarget` with `ImGui::SetDragDropPayload("ASSET", &handle, sizeof(handle))`) enables dragging assets from the browser to scene viewports and property fields.

The **console** follows ImGui's demo example with a scrolling output region, input field with `ImGuiInputTextFlags_CallbackCompletion` for tab-complete, and a `std::map<std::string, std::function>` command registry. Log entries are color-coded by severity using `ImGui::PushStyleColor()`.

**imgui-notify** (patrickcjk/imgui-notify) provides header-only toast notifications with success/warning/error/info types, configurable duration, and fade animations. The enhanced fork **ImGuiNotify** (TyomaVader) adds clickable notifications with callbacks.

For **profiling**, `ImGui::PlotLines()` displays frame time graphs from a ring buffer. **ImPlot** (epezent/implot) provides GPU-accelerated real-time scrolling plots for CPU/GPU timing, memory usage, and network stats. **Tracy Profiler** (wolfpld/tracy) is the gold standard for deep C++ profiling — instrument with `ZoneScoped;` macros, track allocations with `TracyAlloc`/`TracyFree`, and profile locks with `TracyLockable`.

### Custom widgets for specialized editing

- **imgui-knobs** (altschuler/imgui-knobs): Rotary knob controls with 7 visual variants (Tick, Dot, Wiper, Stepped, etc.)
- **ImGui::ColorPicker4()** with `ImGuiColorEditFlags_PickerHueWheel` for color wheels
- Sprite preview with animation playback uses UV coordinate calculation from a spritesheet texture: compute row/column from current frame index, calculate UV bounds, display via `ImGui::Image()` with a `SliderFloat` for playback speed control

The **awesome-dear-imgui** repository and the official ImGui wiki's "Useful Extensions" page maintain curated lists of all available widgets. All recommended libraries are compatible with Dear ImGui docking branch and OpenGL 3.3.

---

## 6. Modern C++ techniques for maintainable editor code

### Type erasure frees the property editor from per-type templates

Sean Parent's type erasure pattern (an internal `Concept` abstract class with a templated `Model` subclass) allows the property editor to draw any component type through a uniform `drawImGui(label)` interface without exposing templates in the UI layer. For game engines where heap allocation per property is too expensive, a **stack-based type erasure** with a fixed buffer (`alignas(std::max_align_t) std::byte buffer_[64]`) stores the function pointer and data pointer inline.

The practical registration approach uses a `std::unordered_map<std::type_index, DrawFunc>` mapping type IDs to draw functions. EnTT's `entt::type_id<T>()` provides compile-time hashed type IDs that avoid the overhead of `typeid()`.

### C++23 deducing this eliminates CRTP boilerplate

C++23's explicit object parameter (`this auto&& self`) is a **game-changer** for editor panel hierarchies. Instead of `template<typename Derived> class EditorPanel` with `static_cast<Derived&>(*this)`, you write:

```cpp
class EditorPanel {
    void render(this auto&& self) {  // C++23 deducing this
        if (self.isVisible()) {
            if (ImGui::Begin(self.panelName(), &self.open_))
                self.onGui();
            ImGui::End();
        }
    }
};
```

No more template parameter on the base class, no `static_cast`, and natural inheritance works. Policy-based mixins (`Dockable`, `Searchable`, `Saveable`) compose panel behaviors through multiple inheritance, each using `this auto&` to access the derived type.

### Compile-time string hashing with consteval guarantees zero runtime cost

**FNV-1a** is the industry-standard hash for game engines (used by Naughty Dog, EnTT, FluxEngine). C++23's `consteval` keyword (versus `constexpr`) **guarantees** compile-time evaluation:

```cpp
consteval StringHash operator""_hs(const char* str, size_t) {
    return StringHash(str);  // FNV-1a internally
}
switch (componentHash) {
    case "Transform"_hs: /* ... */ break;
    case "Sprite"_hs:    /* ... */ break;
}
```

For debug-friendliness, store the original string pointer alongside the hash in debug builds (`#ifdef EDITOR_DEBUG`). EnTT's `entt::hashed_string` uses this exact approach.

### Concepts constrain component APIs at compile time

```cpp
template<typename T>
concept EditableComponent = requires(T t) {
    { T::componentName() } -> std::convertible_to<const char*>;
    requires std::is_default_constructible_v<T>;
};

template<typename T>
concept SerializableComponent = EditableComponent<T> && requires(T t, Archive& ar) {
    { t.serialize(ar) };
};
```

Concepts produce clear error messages when a component type is missing required interfaces, replacing opaque template error cascades with human-readable constraint failures. Combined with `constexpr if` for compile-time branching, a single `drawProperty<T>()` template handles float, int, bool, Vec2, Vec3, Color, string, enums, and custom `ImGuiDrawable` types without runtime dispatch.

### Generational index handles replace smart pointers for entity references

For ECS editors, **generational handles** outperform smart pointers. An `EntityHandle` packs a 20-bit index (supporting 1M entities) and a 12-bit generation counter (4,096 generations before wrap) into a single 32-bit value. A `HandleMap<T>` stores items in a contiguous vector with free-list recycling. On removal, the generation increments, instantly invalidating all existing handles without touching the handle holders. Benefits include no heap allocation per reference, cache-friendly contiguous storage, safe dangling detection via generation mismatch, and trivial copyability.

Use `std::unique_ptr` for exclusively-owned editor state (panels, undo history entries), `std::shared_ptr` for shared assets (textures, prefabs referenced by multiple systems), and generational handles for all entity/component references that may become invalid.

### Signal/slot with RAII connections decouples editor panels from engine state

The **TheWisp/signals** library (optimized for video games) provides RAII connection management — destroying the connection object auto-disconnects the slot. Slot removal uses index marking instead of linear search for O(1) disconnect. The **palacaze/sigslot** library offers a well-tested, thread-safe, header-only alternative.

Editor panels connect to engine signals through their constructor and auto-disconnect on destruction:

```cpp
class InspectorPanel {
    fteng::connection onSelectionChanged_;
    InspectorPanel(WorldEditor& editor) {
        onSelectionChanged_ = editor.selectionChanged_.connect(
            [this](const SelectionSet& sel) { refreshInspector(sel); });
    }
    // Destructor auto-disconnects via RAII
};
```

### Structuring the editor codebase for long-term maintainability

A central **`EditorContext`** serves as the dependency injection hub, holding the `CommandHistory`, `SelectionManager`, `AssetDatabase`, and `ECSRegistry` reference. All panels receive this context through their constructor — no global state, no singletons (except optionally for the context itself).

Panel registration follows Lumix's `StudioApp::GUIPlugin` pattern: an `IEditorPanel` interface with `name()`, `onGui()`, and `isOpen()` methods, stored in a `std::vector<std::unique_ptr<IEditorPanel>>` owned by the `EditorApp`. An optional auto-registration macro using static initialization enables panels to self-register at startup.

The `SelectionManager` maintains a `std::vector<EntityHandle>` with `select()`, `deselect()`, `selectAll()`, and `isSelected()` methods, emitting a `selectionChanged_` signal on every change. Multi-select uses an `additive` flag on `select()`. All inspector panels subscribe to this signal to refresh their display.

---

## Conclusion

The six architectural pillars explored here form a cohesive system when unified by a shared design principle: **serialization-driven decoupling**. The same serialization layer that separates editor from runtime also enables play-in-editor snapshots, drives prefab override propagation via JSON Patch, bridges ECS components to PostgreSQL through dirty-tracked `Tracked<T>` wrappers, and powers undo/redo through command state capture.

Three choices yield the highest leverage for a custom C++23 MMORPG editor. First, adopt **EnTT's meta reflection** (or build a similar system) early — it unlocks generic property inspection, serialization, and prefab override detection with one investment. Second, build the **command pattern undo/redo system before any editing features** — retrofitting undo support is exponentially harder than designing for it upfront. Third, use the **embedded editor with FBO viewports** architecture rather than a separate process — the implementation complexity of IPC outweighs its benefits at this project scale.

The technology stack that emerges — C++23 with `consteval` hashing and deducing this, Dear ImGui docking branch with ImPlot/ImGuizmo/imgui-node-editor, a custom sparse-set ECS with `Tracked<T>` persistence wrappers, PostgreSQL via libpqxx/taoPQ with async batch writes, and JSON Patch for prefab overrides — provides a foundation capable of scaling from a development tool to a production MMORPG content pipeline.