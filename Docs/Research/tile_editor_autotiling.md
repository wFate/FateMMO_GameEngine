# Building a professional 2D tile editor in C++ with Dear ImGui

A polished tile editor and scene editor for a custom C++ game engine requires four interlocking systems: a tile palette that makes selecting and organizing tiles fast, a scene canvas with professional painting tools, an auto-tiling engine that eliminates tedious manual placement, and a rendering pipeline that stays responsive on maps with hundreds of thousands of tiles. This report synthesizes implementation patterns from Tiled, LDtk, Godot 4, RPG Maker, and dozens of open-source ImGui-based editors into concrete architectural guidance for a C++23 engine using SDL2, OpenGL 3.3, Dear ImGui (docking branch), and a custom ECS with SpriteBatch rendering.

The single most impactful architectural decision is to treat tiles as **component data on chunk entities**, not as individual ECS entities. The single most impactful UX decision is to implement **47-tile blob auto-tiling** early — it eliminates 90%+ of manual tile placement. And the single most impactful rendering decision is to use **GL_TEXTURE_2D_ARRAY** instead of a texture atlas, which eliminates texture bleeding entirely and simplifies shaders.

---

## The tile palette: how professional editors make tile selection feel instant

The tile palette is the primary interface artists interact with. Every professional 2D editor — Tiled, LDtk, Godot 4, RPG Maker — uses the same core pattern: display the entire spritesheet as a scrollable image with a grid overlay, and let users click-drag to select rectangular regions that become the painting stamp.

**In Dear ImGui, the recommended approach renders the full tileset as a single `ImGui::Image()` call** inside a `BeginChild()` scrollable region, then overlays grid lines and selection highlights using `ImGui::GetWindowDrawList()`. The alternative — rendering each tile as a separate `ImGui::ImageButton()` — works for small tilesets but becomes slow beyond a few hundred tiles. For the full-image approach, mouse hit-testing converts screen coordinates to tile coordinates: `tileX = (int)((mousePos.x - canvasPos.x) / (tileSize * displayScale))`. Rectangular multi-tile selection tracks drag start/end positions and draws a filled semi-transparent rectangle over the selected region using `AddRectFilled` with `IM_COL32(255, 255, 0, 64)`.

Multiple tilesets should be organized with an `ImGui::BeginTabBar()` at the top of the palette panel — this is exactly how Tiled handles it. Each tileset source stores its OpenGL texture handle, dimensions, tile size, margin, and spacing parameters. Following Tiled's Global Tile ID (GID) convention, each tileset receives a `firstGID` offset so that a single 32-bit integer can reference any tile across all loaded tilesets. The GID system is the industry standard because it makes serialization trivial and tools interoperable.

What separates good tile palettes from great ones comes down to four UX details observed across professional editors. First, LDtk's **saved tile selections** let artists bookmark frequently used tile combinations — a significant productivity feature worth implementing early. Second, Godot 4 introduced **property painting mode** where you select a property (collision, terrain type) and paint it across tiles in the atlas view rather than editing tiles one by one, which transforms the setup workflow for large tilesets. Third, every professional editor shows a **stamp preview at the cursor** on the map canvas during painting. Fourth, Tiled's right-click-to-capture-stamp lets artists grab multi-tile regions directly from the map, bypassing the palette entirely.

### Tile properties and metadata architecture

Tile properties follow the **flyweight pattern**: a shared `TileDef` struct holds all per-tile-type data (collision flags, terrain type, animation reference, custom properties), while each map cell stores only a compact `TileInstance` containing the tile ID and flip flags. This separation keeps map memory tight — a 256×256 map with 4 layers costs only **1 MB** at 4 bytes per tile.

The property system should support Tiled's established type set: **string, int, float, bool, color, file path, object reference, and class** (nested property bundles). In the ImGui inspector, each type maps to a natural widget — `ImGui::Checkbox` for bools, `ImGui::InputInt` for integers, `ImGui::ColorEdit4` for colors, `ImGui::Combo` for enum-like terrain types, and `ImGui::InputText` for strings. Collision is best stored as a bitmask (`SOLID=1, PLATFORM=2, LADDER=4, HAZARD=8`) with `ImGui::CheckboxFlags` in the UI. For an MMORPG, also consider per-tile walk speed multipliers, footstep sound categories, and light occlusion flags.

### Tile animation: global state, not per-instance

Animated tiles (water ripples, torchlight flicker, flowing lava) use frame sequences with per-frame duration in milliseconds — Tiled's model, stored as `{ tileId, durationMs }` pairs. The critical insight: **all instances of the same animated tile must be synchronized**. Maintain a single `AnimationState` per unique animation definition (typically fewer than 50), not per map cell. At render time, look up the current display tile via `displayTile = animStates[baseTileId].currentFrameTile`. If all frames share equal duration, the computation simplifies to `currentFrame = (int)(globalTimeMs / frameDuration) % frameCount` — no accumulator needed.

The animation editor UI in ImGui renders a horizontal strip of frame thumbnails with per-frame duration inputs, drag-and-drop reordering via `ImGui::BeginDragDropSource`, and a live preview showing the animated tile at actual speed. Frames should be addable by dragging tiles from the palette panel using ImGui's payload system with a `"TILE_ID"` payload type.

---

## Auto-tiling transforms editor productivity

Auto-tiling is the system that converts a paint-by-numbers workflow into a paint-by-terrain workflow. Instead of manually selecting the correct corner, edge, and transition tile for every cell, the artist paints terrain types and the system selects the correct visual tile automatically. Every modern 2D editor implements some form of this, and it should be a priority feature.

### The 47-tile blob bitmask: the production-quality standard

The **47-tile blob system** examines all 8 neighbors of each cell. Each neighbor direction is assigned a bit weight (N=2, NE=4, E=16, SE=128, S=64, SW=32, W=8, NW=1), producing an 8-bit mask with 256 possible values. The key algorithmic insight is **diagonal gating**: a diagonal neighbor bit only counts if both adjacent cardinal neighbors are also present. NE only matters if both N and E are set. This gating reduces the 256 raw combinations to exactly **47 unique visual configurations**.

Implementation requires a precomputed **256-entry lookup table** mapping every possible gated mask to a frame index (0–46). At runtime, computing the correct tile is O(1) per cell with zero branching:

```
gatedMask = computeGatedMask(neighbors)  // ~10 bitwise ops
frameIndex = LOOKUP_TABLE[gatedMask]       // single array read
```

The 47 configurations cover every possible terrain shape: solid fills, straight edges, outer corners, inner corners, T-junctions, corridors, L-bends, dead ends, and isolated cells. For prototyping, start with the simpler **16-tile cardinal-only system** (4-bit, checks only N/E/S/W), then upgrade to blob-47 for production.

### Handling multiple terrain types without combinatorial explosion

With M terrain types, naive blob tilesets require transition art for every terrain pair — an explosion of required art. Three solutions exist. The **routing pipeline** (documented by Mikhail Andreev) builds a compatibility graph where nodes are terrain types and edges are available transition tilesets, then uses BFS to find the shortest "material path" between any two terrains, chaining transitions through intermediary materials when no direct tileset exists. The **layered compositing approach** (used by Tilesetter and Tiled) stacks base terrain plus transparent border overlays, reducing per-terrain art from 47 tiles to roughly 6 border pieces plus a base fill. The **dual-grid technique** stores terrain values on grid vertices instead of cells, requiring only **5 visual tiles** instead of 47 — each cell's tile is determined by its 4 surrounding vertex values.

LDtk takes a fundamentally different approach with its **rule-based auto-layer system**. Instead of bitmask lookup, artists define pattern-matching rules — 3×3 or 5×5 grid patterns that specify conditions ("cell must contain value X", "cell must NOT contain value X", "don't care") for each position relative to the target. When the pattern matches, the associated tile is placed. Rules support flipping, random selection, Perlin noise modulation, and multi-tile stamps. This system is more flexible than bitmasks and more intuitive for artists, though slightly less performant for very large maps.

---

## Scene painting tools and map management

### Brush tools follow established patterns

The painting toolset is well-standardized across editors. The **stamp brush** paints the current tile selection at the cursor position, interpolating via Bresenham's line algorithm during fast drags to prevent gaps. The **rectangle fill** tool draws a preview rectangle during drag and fills on release, with Shift constraining to squares. The **flood fill** uses queue-based BFS (never recursive — stack overflow risk on large maps) expanding in 4 cardinal directions, matching the target tile type. For pattern fills, the replacement tile cycles through the stamp: `stamp[x % stampW][y % stampH]`. The **line tool** applies Bresenham's algorithm between click and release points. The **scatter brush** randomly selects from tiles in the current stamp at each position, weighted by per-tile probability values.

**Tile rotation and flipping** encode in the high bits of the 32-bit tile ID, following Tiled's convention: bit 31 = horizontal flip, bit 30 = vertical flip, bit 29 = diagonal flip (axis swap). Combining these three bits produces all 8 possible orientations. 90° clockwise rotation = diagonal flip + horizontal flip. 180° = horizontal + vertical flip. Keyboard shortcuts X/Y/Z for flip-H/flip-V/rotate match Tiled's conventions and should be adopted.

### Layer system and rendering order

Layers should support four types matching the union of Tiled and LDtk's models: **tile layers** (grid-based tile data), **object/entity layers** (free-positioned entities), **IntGrid layers** (integer values for collision and terrain logic), and **group layers** (containers for nesting). Each layer stores name, visibility flag, opacity, lock state, render order, pixel offset, parallax factor, and tint color.

The layer panel in ImGui renders a scrollable list with eye-icon visibility toggles, lock icons, and drag-and-drop reordering via `ImGui::BeginDragDropSource/Target`. Right-click context menus provide rename, duplicate, delete, and merge operations. During editing, **unselected layers should dim** (Godot 4's approach) — render inactive layers at reduced opacity so artists maintain spatial context while focusing on the active layer.

Rendering is back-to-front in layer order. For tile layers, determine the visible tile range from camera bounds, batch draw calls by tileset texture, and apply per-layer opacity as an alpha multiplier. The parallax factor scales the camera offset per layer, enabling simple depth effects without separate parallax systems.

### Efficient handling of large MMORPG maps

For an MMORPG with potentially enormous maps, **chunked storage is essential**. Divide each layer into 16×16 tile chunks stored in a hash map keyed by chunk coordinate. This provides O(1) tile lookup while only allocating memory for non-empty regions. Each chunk occupies **1 KB** (256 tiles × 4 bytes), fitting entirely in L1 cache for fast iteration. Empty chunks store as null pointers (8 bytes on 64-bit), so a vast mostly-empty map costs almost nothing.

Each chunk carries a `dirty` flag and cached GPU resources (VAO/VBO). When a tile changes, only the affected chunk's `dirty` flag is set. The render system rebuilds only dirty chunks' vertex buffers each frame — for typical editing where 1-2 chunks change per frame, this means rebuilding data for at most 512 tiles rather than the entire map.

For the **minimap**, render the entire map at 1 pixel per tile into an off-screen texture, updated only when tiles change. Display this texture in a fixed-size ImGui panel with a rectangle showing the current viewport bounds. Clicking the minimap jumps the camera to that position.

**Camera controls** follow a universal standard: middle-mouse drag for panning, scroll wheel for zoom-to-cursor, and WASD/arrow keys for keyboard navigation. The zoom-to-cursor algorithm is critical for good UX — compute the world position under the cursor before and after the zoom change, then offset the camera by the difference. This keeps the point under the cursor stationary during zoom, matching Google Maps behavior. The grid overlay should fade based on zoom level: compute `gridScreenSize = tileSize * zoom` and fade the grid alpha from 0 at 4px to full opacity at 16px screen-space grid spacing.

### Entity placement bridges the editor and ECS

Entities (NPCs, spawn points, interactables, triggers) live on dedicated entity layers. A **prefab browser panel** displays available entity definitions as icon buttons with thumbnails. Drag-and-drop from the browser to the viewport uses ImGui's payload system: `ImGui::SetDragDropPayload("ENTITY_PREFAB", &prefabId, sizeof(int))` on the source side, `ImGui::AcceptDragDropPayload("ENTITY_PREFAB")` on the viewport target. On drop, convert screen position to world coordinates, optionally snap to grid, and instantiate the entity.

LDtk's entity model is worth studying: entity definitions specify name, display shape, size constraints, and typed custom fields (Int, Float, Bool, String, Color, Point, Enum, Array, EntityRef). Entity instances reference their definition and store field values. Constraints like "only 1 PlayerStart per level" prevent common errors. This separation of definition from instance maps naturally to ECS archetypes.

---

## Undo/redo and serialization

### Command pattern is the industry standard

Every professional editor uses the **Command pattern** with an undo/redo stack. Each edit creates a command object storing the operation type and enough data to reverse it. For tile operations, a `SetTileCommand` records the layer, position, old tile ID, and new tile ID. Brush strokes create a `CompoundCommand` grouping all individual tile changes so they undo as a single step.

The critical design insight from the Overgrowth team: **build undo/redo from the start**. Retrofitting the command pattern into an existing editor is extremely difficult because every edit path must be routed through the command system. Consecutive same-type operations (continuous brush painting) should coalesce into a single undo step — start a new compound command on mouse-down, accumulate during drag, finalize on mouse-up.

For simpler implementations, the **snapshot approach** stores complete map state at each undo point. On modern hardware with chunked storage, this is viable for small-to-medium maps — a 64×64 map with 4 layers only costs 64 KB per snapshot. For large MMORPG maps, prefer the command pattern to avoid memory bloat.

### File format: hybrid JSON metadata plus binary tile data

For tilemap serialization, a **hybrid format** combines the debugging advantages of JSON with the performance of binary. The map file uses JSON to store metadata (map dimensions, tileset references, layer definitions, entity objects, custom properties) while tile data arrays go into separate binary files compressed with zlib or zstd.

Study Tiled's format as the reference implementation. Tiled's 32-bit GIDs encode tile ID in the lower 28 bits and flip/rotation flags in the upper 4 bits. Tile data supports three encodings: raw integer arrays (human-readable), base64-encoded binary (compact), and base64 + zlib/gzip/zstd compression (smallest). For a 256×256 layer, raw JSON is ~750 KB, base64 is ~350 KB, and base64+zlib is typically **100-200 KB**.

For format versioning, store both `version` (current format version) and `minReaderVersion` (oldest reader that can load the file). On load, reject if the major version exceeds what the loader supports. Apply sequential migration functions (v1→v2→v3) for older files. LDtk's approach of maintaining deprecated fields for several versions before removal provides a smooth transition path.

Multi-map world organization follows Tiled's `.world` files or LDtk's built-in world view: a world manifest lists map files with their pixel-space positions, enabling a zoomed-out world view showing all maps simultaneously. For the MMORPG, this enables zone-based map organization with adjacency information for seamless transitions.

---

## OpenGL rendering architecture for pixel-perfect tilemaps

### GL_TEXTURE_2D_ARRAY eliminates the bleeding problem entirely

Texture bleeding — where the GPU samples pixels from neighboring tiles in an atlas — is the number one visual issue with tile rendering. **GL_TEXTURE_2D_ARRAY**, available in OpenGL 3.0+ (well within the 3.3 target), stores each tile as a separate layer with independent edge clamping. No padding needed, no half-texel UV offsets, no bleeding possible. The shader samples with `texture(tileAtlas, vec3(localUV, float(tileIndex)))`.

Create one texture array per tile size (a 32×32 array for standard tiles, a 20×32 array for chibi sprites, etc.). All layers in an array must share dimensions, which works perfectly for uniform tile grids. Check `GL_MAX_ARRAY_TEXTURE_LAYERS` at runtime — the minimum is 256 in GL 3.0, typically **2048+** on modern GPUs, sufficient for any tileset. Allocate with `glTexImage3D`, upload individual tiles with `glTexSubImage3D`, and always set `GL_NEAREST` filtering and `GL_CLAMP_TO_EDGE` wrapping.

If you must use a traditional atlas (for compatibility or mixed-size tiles), apply three mitigations: **1-2px edge-extruded padding** between tiles (duplicate edge pixels outward, not transparent), **half-texel UV insets** (`uv += 0.5/atlasSize`), and **never generate mipmaps** on the atlas. Mipmaps average texels across tile boundaries and destroy pixel art crispness. For pixel art, the only acceptable filter configuration is `GL_NEAREST` for both min and mag filters with no mipmaps.

### Chunk-based pre-built VBOs balance performance and editability

Four tilemap rendering approaches exist, each with different tradeoffs. **Per-tile draw calls** are trivial but catastrophically slow. **Dynamic SpriteBatch** rebuilds all visible tile vertices every frame — simple but wasteful for static tiles. **Instanced rendering** uses a single quad mesh with per-instance position/tile data, efficient but less flexible. **GPU-shader tilemaps** upload tile IDs as a data texture and do all tile-to-pixel mapping in the fragment shader — the fastest approach (benchmarked at 3000+ FPS for 512×512 maps) but the hardest to integrate with a SpriteBatch pipeline.

The recommended approach for this engine: **chunk-based pre-built VBOs** with the SpriteBatch as fallback for dynamic objects. Each 16×16 chunk has its own VAO and VBO containing pre-computed vertex data (position, UV, tile index). A shared index buffer handles the quad topology across all chunks. Visible chunks are determined by a simple AABB viewport test. When a tile changes, only the single affected chunk's VBO is rebuilt — 256 tiles maximum. All chunks share the same shader and tile texture array, so rendering is **one draw call per visible chunk per layer**.

For the existing SpriteBatch, the hybrid approach works well: tile layers use pre-built chunk VBOs for static rendering, while entities, particles, and other dynamic objects go through the SpriteBatch. Both use the same projection matrix and can interleave in the render queue by layer order.

Animated tiles fit into this system two ways. The simple approach: when an animation frame changes, mark affected chunks as dirty and rebuild their VBOs. Since animation frames change infrequently (typically every 100-300ms) and affect few chunks, the rebuild cost is negligible. The advanced approach: pass a time uniform to the shader and compute animation offsets per-tile, avoiding any VBO rebuilds.

---

## ECS integration: tiles are data, not entities

The firm consensus across engine communities is that **tiles should never be individual ECS entities**. A 256×256 map with 4 layers creates 262,144 tiles — creating that many entities destroys ECS performance through component storage bloat and query overhead. Instead, use this hierarchy:

One **Tilemap entity** holds a `TilemapComponent` (map dimensions, tile size, tileset references, source file path) and a `TransformComponent` (world position). Each layer is a **child entity** with a `TileLayerComponent` (layer name, visibility, opacity, parallax factor, and the actual tile data as a chunked array) and a `TileLayerRenderComponent` (per-chunk VAO/VBO handles, dirty flags, shared texture array reference).

**Placed entities** (NPCs, spawn points, items, triggers) become full ECS entities with appropriate components. A factory pattern maps type strings from the map file to component configurations: a `"SpawnPoint"` type creates an entity with `TransformComponent` and `SpawnPointComponent`, while an `"NPC"` type adds `SpriteComponent`, `NPCComponent`, and `DialogComponent`. This preserves the ECS's strengths for gameplay entities while keeping tile data in cache-friendly flat arrays.

The editor operates on the same ECS components as the runtime, with additional editor-only components like `EditorSelectedComponent` and `EditorGizmoComponent`. Tile editing modifies `TileLayerComponent` directly and marks affected chunks dirty. A "Play mode" snapshots the current ECS state; "Stop" restores it.

---

## Making Dear ImGui feel like a real application

### Docking, fonts, and icons transform perceived quality

The docking branch's `DockSpace` API creates the application-frame feel of professional editors. Set up a full-window transparent host with `ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar`, create the dockspace inside it, then use the `DockBuilder` API to define a sensible default layout on first launch: scene hierarchy upper-left, properties lower-left, viewport center, tileset palette and asset browser bottom. The crucial pattern: check `DockBuilderGetNode(id) == nullptr` before building — this ensures the programmatic layout only applies on first run, preserving user-customized layouts from the .ini file on subsequent launches.

**Replace the default ProggyClean font** with Open Sans, Roboto, or Inter at 15-18px. Then merge an icon font (FontAwesome 6 via `IconFontCppHeaders`) with `config.MergeMode = true`. This single change — using `ICON_FA_PLAY " Play"`, `ICON_FA_FLOPPY_DISK " Save"`, `ICON_FA_EYE` for visibility toggles — transforms the editor from debug-UI aesthetic to professional tool aesthetic overnight. Font files can be embedded as C arrays using ImGui's `binary_to_compressed_c` tool to avoid filesystem dependencies.

Apply a **custom dark theme** with cohesive colors. The key style variables: `WindowRounding = 4.0f`, `FrameRounding = 2.0f`, `WindowPadding = ImVec2(8, 8)`, and a coordinated color palette for `WindowBg`, `FrameBg`, `Header*`, `Tab*`, and `Button*` colors. The `ImThemes` project and ImGui issue #707 provide dozens of ready-made themes. Use `ImGui::ShowStyleEditor()` during development for real-time theme iteration.

### Essential interaction patterns

**Context menus** via `ImGui::BeginPopupContextItem()` should appear on right-click for every interactive element — entities get Duplicate/Delete/Add Component menus, layers get Rename/Delete/Merge Down menus, empty viewport space gets Create Entity menus. Use `ImGui::Separator()` and `ImGui::BeginMenu()` for hierarchical sub-menus.

**Keyboard shortcuts** since ImGui 1.89+ use the built-in `ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)` API with proper input routing. Essential shortcuts: Ctrl+Z/Y for undo/redo, Ctrl+S for save, Ctrl+C/V for copy/paste, B for brush, G for bucket fill, E for eraser, R for rectangle, X/Y/Z for flip-H/flip-V/rotate, 1-9 for layer switching, and Space+drag for camera pan.

**Drag-and-drop** between panels — tiles from palette to viewport, entities from prefab browser to viewport, assets from browser to property slots — uses `ImGui::BeginDragDropSource` with typed payloads (`"TILE_ID"`, `"ENTITY_PREFAB"`, `"ASSET_TEXTURE"`). Show a tooltip with a thumbnail preview during the drag via `ImGui::Image()` inside the drag source block.

**Toolbars** render as borderless child windows with icon buttons. Highlight the active tool by pushing a different `ImGuiCol_Button` color before the active tool's button. Use `ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical)` between tool groups.

The **property inspector** panel displays components of the selected entity using type-appropriate widgets generated from component metadata. For tile properties, use `ImGui::CollapsingHeader` sections for Collision, Terrain, Animation, and Custom Properties. Multi-select editing (selecting multiple tiles and editing shared properties simultaneously) is a productivity multiplier observed in both Tiled and Godot 4.

---

## Architectural recommendations and reference implementations

The following open-source projects serve as the most valuable references for this engine's editor development. **LumixEngine** (github.com/nem0/LumixEngine) is the gold standard for ImGui-based game editors — study its custom toolbar widgets, FontAwesome integration, context menus, and docking layout. **Hazel Engine** (github.com/TheCherno/Hazel) provides a complete walkthrough of building an ImGui editor with docking, viewport rendering, and ECS integration, backed by a popular tutorial series. **stb_tilemap_editor.h** (part of nothings/stb) is a single-header C tile map editor implementing a complete palette panel, layer system, undo buffer, and scrolling — invaluable as a reference for tile-specific UI logic.

For tile format and auto-tiling references, **Tiled's source** (github.com/mapeditor/tiled) documents the TMX/JSON format specification, Wang set terrain system, and animation editor. **LDtk's documentation** (ldtk.io/docs) covers its innovative rule-based auto-layer system and entity model. **Boris the Brave's blog** provides the definitive classification of auto-tiling systems and the mathematical foundations of blob tilesets and Wang tiles.

### Implementation priority order

Build the systems in this order to maximize productivity at each stage: **(1)** Basic tile palette with single-tile selection and stamp painting on a single layer — the minimum viable editor. **(2)** Multiple layers with visibility toggles, the eraser tool, and undo/redo. **(3)** JSON serialization with save/load. **(4)** Multi-tile selection, rectangle fill, and flood fill tools. **(5)** 47-tile blob auto-tiling — this is where productivity explodes. **(6)** Entity placement with the prefab browser. **(7)** Chunked storage and chunk-based VBO rendering for large maps. **(8)** Tile animation, tile properties inspector, and copy/paste. **(9)** Polish: custom theme, icon fonts, keyboard shortcuts, minimap, grid overlay fading. **(10)** Binary serialization with compression, multi-map world organization, and advanced auto-tiling with multiple terrain transitions.

Each stage produces a usable editor. The auto-tiling system at stage 5 is the inflection point where the editor transitions from "functional tool" to "productivity multiplier" — prioritize it aggressively.