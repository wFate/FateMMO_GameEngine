# Building a Unity-like game engine editor in C++

**A polished, dockable editor with scene/game viewports, gizmos, hierarchy, inspector, and undo is entirely achievable for a solo developer using SDL2, OpenGL 3.3, and Dear ImGui's docking branch.** The architectural patterns are well-established: Unity itself is a C++ core with a managed scripting layer, and its editor windows — Scene View, Game View, Inspector, Hierarchy — all render to offscreen framebuffers displayed as textures in an immediate-mode GUI. Open-source engines like Hazel, Lumix, Overload, and Spartan have proven that ImGui can produce professional-quality editors. This report distills the architecture, rendering patterns, and implementation strategies needed to replicate that experience.

---

## How Unity was actually built

Unity Technologies began as **Over the Edge Entertainment**, founded in Copenhagen in 2004 by David Helgason, Joachim Ante, and Nicholas Francis. The origin traces to May 2002, when Francis posted on an OpenGL forum seeking collaborators for an open-source shader compiler. Ante, then a Berlin high school student, responded. They merged their independent game engine codebases, bootstrapped with **€25,000 from Ante's father**, and initially tried to build a game. Their first title, *GooBall* (2005), flopped — but the engine they'd built had clear value. They pivoted to selling the tool.

**Unity 1.0** launched at Apple's WWDC in June 2005 as a Mac-only engine priced at $499. The editor remained Mac-exclusive until **Unity 2.5 (March 2009)**, when Ante's team rebuilt most of the editor for Windows — and overnight, the majority of Unity's users shifted to that platform. The engine hit critical mass with Unity 3.0 (2010, Android support + Asset Store), went physically-based with Unity 5.0 (2015), introduced the Scriptable Render Pipeline in 2018, and began its data-oriented transformation with DOTS at GDC 2018. Unity went public on the NYSE in September 2020.

The engine's **two-layer architecture** is the key design decision that defines everything else. The native C++ core handles rendering, physics, audio, memory management, serialization, and platform abstraction — shipped as `UnityPlayer.dll` (~40MB+). On top sits a managed C# layer providing all user-facing APIs (`UnityEngine.*`, `UnityEditor.*`), built originally on the **Mono runtime** for cross-platform C# execution. The binding mechanism uses `[MethodImpl(MethodImplOptions.InternalCall)]` attributes on C# methods that bridge directly into C++ native functions. Files named `*.bindings.cs` in Unity's open-source C# reference (UnityCsReference on GitHub) contain thousands of these declarations. **IL2CPP**, introduced with Unity 5 for iOS 64-bit, later became an alternative: an AOT compiler (written in C#) that translates .NET IL to C++ source code, which then compiles to native machine code via the platform's C++ compiler.

Unity chose C# over C++ for scripting because managed memory eliminates entire classes of bugs, C#'s reflection system powers the Inspector and serialization, and the Mono runtime enabled cross-platform execution before .NET itself was portable. Early versions also supported UnityScript (JavaScript-like) and Boo, but both were deprecated — C# is now the sole scripting language.

---

## The editor architecture and its separation of concerns

Unity's editor is a **hybrid C++/C# application**. Core rendering and window management live in C++; all editor windows, tools, and panels are implemented in C# within the `UnityEditor` assembly. Historically, the editor UI was built using **IMGUI** (Unity's custom immediate-mode GUI), with a transition to **UI Toolkit** (retained-mode, UXML/USS) underway since 2019.

Every major editor panel — Scene View, Game View, Inspector, Hierarchy, Project browser — derives from the C# class `EditorWindow`. Custom editors inherit from `Editor` and override `OnInspectorGUI()`. Property drawers control how specific types render in the Inspector. This extensibility model is what made Unity's editor ecosystem so rich.

**Play mode separation** is one of Unity's most important architectural features. When entering Play Mode (with domain reload enabled), Unity serializes all managed state to the C++ side, destroys all managed memory, reloads all assemblies fresh, then deserializes data back. This guarantees a clean slate identical to a standalone build. The process is expensive — which is why Unity added optional domain reload skipping in 2019.3 for faster iteration, at the cost of requiring developers to manually reset static state.

The **serialization system** is arguably Unity's most foundational subsystem. Nearly everything builds on it: the Inspector displays serialized data, prefabs are serialized GameObjects, `Instantiate()` works by serialize-clone-deserialize, undo/redo captures serialized snapshots, hot reloading preserves state across assembly reloads, and scene files are serialized hierarchies. Unity uses a custom C++ serialization backend (not .NET's) with binary and YAML backends. The `SerializedObject`/`SerializedProperty` API wraps this for the editor — calling `ApplyModifiedProperties()` automatically records undo operations.

Unity's **undo system** stores delta changes computed by diffing serialized object state. `Undo.RecordObject(target, "description")` snapshots an object's serialized state before modification; after the change, Unity computes the diff. Operations auto-group between events (mouse-down creates a new group). This tight coupling between serialization and undo is a key architectural insight: **reflection, serialization, the inspector, and undo/redo are all facets of the same underlying property system**.

---

## Rendering dual viewports with framebuffer objects

The core pattern for editor viewports is straightforward: **render the scene to an offscreen FBO, then display the resulting texture in an ImGui panel**. Unity's Scene View creates a hidden editor camera (completely separate from game cameras, using `HideFlags`) and renders to a dedicated `RenderTexture`. The Game View does the same with runtime cameras. Both render independently; the editor composites their textures into the appropriate panels.

For an OpenGL 3.3 engine, the implementation follows this sequence. First, create a framebuffer with a color texture attachment and a depth-stencil renderbuffer:

```cpp
GLuint fbo, colorTexture, rbo;
glGenFramebuffers(1, &fbo);
glBindFramebuffer(GL_FRAMEBUFFER, fbo);
// Color attachment
glGenTextures(1, &colorTexture);
glBindTexture(GL_TEXTURE_2D, colorTexture);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
             GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                        GL_TEXTURE_2D, colorTexture, 0);
// Depth-stencil attachment
glGenRenderbuffers(1, &rbo);
glBindRenderbuffer(GL_RENDERBUFFER, rbo);
glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                           GL_RENDERBUFFER, rbo);
```

Each frame, bind the FBO, render the scene with the viewport's camera matrices, render editor overlays (grid, gizmos, outlines), then unbind. Display the result in ImGui with **flipped UV coordinates** (OpenGL textures are bottom-up):

```cpp
ImGui::Image((ImTextureID)(intptr_t)colorTexture,
             viewportSize, ImVec2(0, 1), ImVec2(1, 0));
```

When the ImGui panel resizes, reallocate both the color texture and depth renderbuffer via `glTexImage2D` and `glRenderbufferStorage` with the new dimensions. Update the camera's aspect ratio to match.

**Multiple viewports** share the same scene data (meshes, materials, GPU buffers, shader programs) but maintain per-viewport state: FBO, camera, dimensions, and render settings. The rendering loop iterates viewports, binding each FBO and setting per-viewport uniforms (view/projection matrices). Scene View viewports add overlay passes for the grid, selection outlines, and gizmos; Game View viewports skip these. Only visible/active viewports should be rendered — Unity throttles Scene View re-rendering to mouse-hover events.

**Input forwarding** to the viewport requires checking `ImGui::IsWindowHovered()` and computing mouse position relative to the viewport's content region origin. Pass these relative coordinates to the editor camera's input handler.

---

## Editor overlays: grids, gizmos, and selection outlines

The **compositing order** for editor overlays within the Scene View FBO is: clear → render scene geometry → render grid (alpha-blended, writes depth) → render selection outlines → render debug lines → render gizmos (depth test OFF, always on top).

The best technique for an **infinite grid** uses a full-screen clip-space quad with a fragment shader that performs ray-plane intersection against Y=0. The shader uses `fwidth()` for screen-space anti-aliased grid lines, writes `gl_FragDepth` for correct depth interaction with scene geometry, and fades lines at distance to prevent aliasing. Axis lines are highlighted in red (X) and blue (Z). This approach requires no world-scale geometry — just four vertices forming a screen quad, plus the inverse view-projection matrix as a uniform.

For **gizmos** (translate/rotate/scale handles), the most practical approach for an ImGui-based editor is **ImGuizmo** by Cédric Guillemet — a single-header library that integrates directly with ImGui's draw system. After calling `ImGuizmo::SetRect()` with the viewport window's position and size, `ImGuizmo::Manipulate()` takes view/projection matrices and the selected object's transform matrix, renders the appropriate gizmo, and returns the modified matrix. Keyboard shortcuts switch between operations (T for translate, E for rotate, R for scale). `ImGuizmo::IsOver()` prevents click-through to the scene, and `ImGuizmo::IsUsing()` indicates active manipulation for undo recording.

Gizmos must maintain **constant screen size** regardless of distance from the camera. If rendering custom mesh-based gizmos instead of using ImGuizmo, scale them by `distance_to_camera / reference_distance` before rendering, and disable depth testing so they always appear on top.

**Selection outlines** have two practical approaches. The simpler stencil-based method renders the selected object writing 1 to the stencil buffer, then renders a slightly scaled-up version of the object (with a solid outline color) only where stencil ≠ 1. The more sophisticated approach renders selected objects into a separate selection ID buffer, then runs a fullscreen edge-detection pass comparing each pixel's ID with its neighbors — wherever IDs differ, an outline edge appears. This latter technique, used by The Machinery engine, handles complex shapes better and supports dimming outlines behind occluding geometry.

---

## The editor camera: orbit, pan, zoom, and fly-through

Editor cameras use a **spherical coordinate orbit model** around a focal point (pivot). Three parameters define the camera: `azimuth` (horizontal angle), `polar` (vertical angle, clamped to avoid gimbal lock), and `radius` (distance from pivot = zoom level). The camera position is computed as:

```
x = pivot.x + radius * cos(polar) * sin(azimuth)
y = pivot.y + radius * sin(polar)
z = pivot.z + radius * cos(polar) * cos(azimuth)
```

**Input mapping** follows industry conventions: middle mouse drag orbits (delta_x → azimuth, delta_y → polar angle), Shift+middle mouse pans (translating the pivot along the camera's local right and up vectors), and scroll wheel zooms (proportionally scaling radius). Pan speed should scale with radius so distant scenes pan at appropriate speeds. "Focus selected" (F key) sets the pivot to the selected object's center and adjusts radius to frame its bounding box.

A secondary **fly-through mode** activates on right-click hold: WASD moves along camera-local axes, mouse look rotates orientation. Unity's Scene View supports smooth transitions between perspective and orthographic projection by interpolating an `m_Ortho` parameter.

---

## Building the editor shell with ImGui docking

Dear ImGui's docking branch provides the infrastructure for a professional tabbed, dockable layout. The setup requires enabling `ImGuiConfigFlags_DockingEnable` (and optionally `ImGuiConfigFlags_ViewportsEnable` for OS-level multi-window support). After `ImGui::NewFrame()`, call `ImGui::DockSpaceOverViewport()` to create a fullscreen dockspace.

**Programmatic default layout** uses the `DockBuilder` API from `imgui_internal.h`. This runs once (checking if the dockspace node already exists), splits the dockspace into regions, and docks windows by name:

```cpp
ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.20f, &dock_left, &dock_main);
ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.25f, &dock_right, &dock_main);
ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.30f, &dock_bottom, &dock_main);
ImGui::DockBuilderDockWindow("Scene Viewport", dock_main);
ImGui::DockBuilderDockWindow("Hierarchy", dock_left);
ImGui::DockBuilderDockWindow("Inspector", dock_right);
ImGui::DockBuilderDockWindow("Console", dock_bottom);
```

ImGui auto-persists layout to `imgui.ini`. Each panel is a class with an `OnImGuiRender()` method; panels share a `Scene*` context and selection state. Use `ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0))` for viewport panels to eliminate padding around the scene texture.

**Making ImGui look professional** requires three things: a custom dark theme (setting ~30 color values in `ImGuiStyle::Colors`), icon fonts (FontAwesome merged into the main font atlas using `ImFontConfig::MergeMode = true` with the IconFontCppHeaders library), and good typography (Roboto or Inter at 16px with proper pixel snapping). The ImGui demo's `ShowStyleEditor()` enables interactive prototyping; tools like ImThemes export theme code directly. Engines like Hazel, Spartan, and Lumix demonstrate that a dark, muted color palette with **2px window rounding**, **4px frame rounding**, and subtle borders achieves a Unity/Unreal-adjacent aesthetic.

**Cross-panel drag-and-drop** uses ImGui's `BeginDragDropSource`/`BeginDragDropTarget` API. Define payload type strings as contracts between panels — `"ENTITY_NODE"` for hierarchy reparenting, `"CONTENT_BROWSER_ITEM"` for asset drag from the content browser to the scene viewport. The payload carries an entity ID or asset path. For viewport drop targets, call `BeginDragDropTarget()` immediately after `ImGui::Image()`.

---

## Scene hierarchy, inspector, and content browser panels

The **hierarchy panel** uses `ImGui::TreeNodeEx` with flags for arrow-based expansion, span-full-width selection, and frame padding. The Hazel engine pattern is the de facto standard: recursive `DrawEntityNode()` renders each entity as a tree node, handles selection on click (checking `IsItemClicked() && !IsItemToggledOpen()`), context menus via `BeginPopupContextItem()`, and drag-and-drop reparenting using `"ENTITY_NODE"` payloads. Leaf nodes use `ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen` to suppress the expand arrow.

The **inspector panel** iterates components on the selected entity, rendering each inside a collapsible `TreeNodeEx` with framed headers. A template function `DrawComponent<T>(name, entity, uiFunction)` handles the boilerplate: checking component existence, rendering the header with a settings gear button, and calling a per-component UI lambda. Property widgets map to types: `ImGui::DragFloat` for floats, a custom `DrawVec3Control` with colored X/Y/Z buttons for vectors, `ImGui::ColorEdit4` for colors, `ImGui::Checkbox` for bools, `ImGui::BeginCombo` for enums. The Hazel-style Vec3 control uses `ImGui::PushMultiItemsWidths(3, ...)` with red/green/blue styled buttons that reset to default on click — **this single widget pattern is what makes the inspector feel polished**.

The **content browser** uses a grid layout with `ImGui::Columns()` (or the newer Tables API), rendering `ImGui::ImageButton` for each file with a folder or file-type icon. Double-clicking directories navigates; a back button returns to the parent. Files become drag sources with `"CONTENT_BROWSER_ITEM"` payloads. The `ImGuiFileDialog` library by aiekick provides a full-featured alternative with thumbnails, filters, and bookmarks.

---

## Reflection and property systems that tie everything together

The deepest architectural insight from studying Unity is that **reflection, serialization, the inspector, and undo/redo are all one system**. Reflection provides metadata (field names, types, memory offsets). The inspector iterates reflected fields to generate widgets. When a widget changes a value, the system captures the old value (via reflection offset), applies the new value, and creates an undo command. Serialization uses the same reflection metadata to read/write fields to disk.

For a solo C++ developer, three practical approaches exist:

**Macro-based registration** (~200 lines of infrastructure) is the simplest starting point. Preshing's minimal reflection system uses `REFLECT()` and `REFLECT_STRUCT_MEMBER()` macros that expand to `offsetof()` and `decltype` to build `TypeDescriptor` structs containing member names, offsets, and type pointers. This powers a recursive `DrawProperty()` function that dispatches reflected types to ImGui widgets.

**EnTT's meta system** is the best choice if you're already using EnTT for ECS. It's completely non-intrusive (no macros, no modification to reflected types), uses pure C++17 templates, and supports runtime registration/unregistration. Registration looks like: `entt::meta<MyType>().data<&MyType::x>("x"_hs).data<&MyType::y>("y"_hs)`. The hashed-string identifiers enable O(1) lookups.

**RTTR (Run Time Type Reflection)** is the best standalone library — MIT-licensed, mature, well-documented. Registration happens in `.cpp` files (keeping headers clean and compile times low). It supports properties, methods, constructors, enumerations, and inheritance traversal. For enum-specific reflection, **magic_enum** provides zero-boilerplate compile-time enum-to-string conversion — invaluable for ImGui combo boxes and serialization.

---

## Undo/redo via the command pattern

The standard approach is the **command pattern** from *Game Programming Patterns*: each undoable operation is an object with `execute()` and `undo()` methods. A `CommandHistory` maintains a vector of commands with a current-position index. Executing a command truncates any redo history, pushes the command, and increments the index. Undo decrements and calls `undo()`; redo increments and calls `execute()`.

The command pattern beats the memento pattern (full-state snapshots) for editor work because commands store only **deltas** — the old and new values of changed properties. A `PropertyChangeCommand` uses reflection offsets to capture the old value before modification and the new value after, then applies either via `memcpy` at the property's memory offset. This is exactly how Unity's undo works internally: `RecordObject` snapshots serialized state, then diffs after modification.

**Undo grouping** handles the critical UX problem of continuous operations (dragging a slider fires hundreds of value changes, but undo should revert in one step). Three techniques solve this: compound commands that bundle multiple operations, `mergeWith()` methods that coalesce consecutive same-property changes (keeping the original old value but updating to the latest new value), and RAII scope guards that automatically group all commands within a block. Godot's explicit `create_action`/`commit_action` API with `MERGE_ENDS` mode is particularly clean and worth emulating.

**Object lifetime** is the key pitfall: deleted objects referenced by undo history cause dangling pointers. The solution is to use stable entity IDs (not raw pointers) in commands, and defer actual object destruction until commands leave the undo stack.

---

## What open-source engines teach us about editor structure

**Godot** takes the most radical approach: its editor is a Godot application running on itself — built entirely with Godot's own UI system (Control nodes). `EditorNode` orchestrates the layout; `EditorData` manages state; `EditorPlugin` enables extensions. Scenes serialize to `.tscn` (text) or `.scn` (binary). The editor and runtime share the same binary, differentiated by the `-e` flag. This self-hosting strategy is elegant but impractical for a new engine — you need a working UI system first.

**Hazel** (TheCherno, 12.9k+ GitHub stars) is the gold standard tutorial for ImGui-based editors. It uses EnTT for ECS, yaml-cpp for serialization, ImGui docking for layout, ImGuizmo for gizmos, and renders to framebuffers displayed as `ImGui::Image`. Its `ImGuiLayer` sits on an application layer stack with event blocking to control input dispatch between editor UI and the 3D viewport. The YouTube series (100+ episodes) walks through every decision.

**Overload** demonstrates a notable pattern: rather than using ImGui directly, its `OvUI` library wraps ImGui into an **event-based widget system**, separating UI logic from the immediate-mode paradigm. This abstraction adds overhead but makes the codebase more maintainable as complexity grows.

**Lumix Engine** pushes ImGui the furthest, with deep custom extensions (`ImGuiEx` namespace) for toolbars, curve editors, node editors with pins and links, and canvas widgets with zoom/pan. It had its own docking implementation before ImGui's docking branch existed. **Spartan Engine** combines an ImGui editor with a GPU-driven Vulkan renderer featuring path-traced GI — its TAA code was adopted by both Godot and S.T.A.L.K.E.R. Anomaly.

---

## Recommended architecture for a solo developer

| System | Recommendation |
|--------|---------------|
| **Window/Input** | SDL2 with OpenGL 3.3 core profile |
| **Editor UI** | Dear ImGui docking branch + custom dark theme + FontAwesome icons |
| **Viewports** | Per-viewport FBO class (color texture + depth RBO), displayed via `ImGui::Image` |
| **ECS** | EnTT (`entt::registry`) — battle-tested, header-only, pairs naturally with ImGui |
| **Gizmos** | ImGuizmo for transform handles; custom infinite grid shader |
| **Reflection** | `entt::meta` for property metadata + `magic_enum` for enums |
| **Serialization** | nlohmann/json or yaml-cpp, driven by reflection metadata |
| **Undo/Redo** | Command pattern with `PropertyChangeCommand` using reflection offsets |
| **Scene format** | YAML (human-readable, diffable, similar to Unity's approach) |
| **Build system** | CMake or Premake5 |

The implementation order that avoids blocking dependencies: SDL2+OpenGL window → ImGui integration with docking → FBO viewport rendering → editor camera (orbit/pan/zoom) → ECS with basic components → scene hierarchy panel → inspector with manual property widgets → reflection system → reflection-driven inspector → serialization → undo/redo → content browser → gizmos → selection outlines → grid shader.

## Conclusion

The path from "OpenGL triangle in an ImGui window" to "polished Unity-like editor" is long but well-charted. The critical architectural insight is that **reflection is the keystone** — it connects the inspector, serialization, and undo/redo into a unified system rather than three separate implementations. Start with the viewport FBO pattern and editor camera, since seeing your scene in a dockable panel is the first moment the project feels real. Add EnTT for lightweight ECS, then build the hierarchy and inspector panels. Introduce reflection when manual property boilerplate becomes unbearable — that's the inflection point where the editor starts scaling. The open-source engines (Hazel for learning, Lumix for advanced ImGui techniques, Godot for production architecture) provide reference implementations for every subsystem. The technology stack of SDL2 + OpenGL 3.3 + ImGui docking + EnTT + ImGuizmo is proven by dozens of engines and handles everything up to a professional-quality editor experience.