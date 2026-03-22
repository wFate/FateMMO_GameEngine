# Editor UI Polish Design

**Date:** 2026-03-22
**Goal:** Transform the FateMMO editor from "functional programmer UI" to "professional game engine" — targeting Unity/Aseprite-level visual quality through fonts, colors, spacing, and widget refinement.

**Constraints:**
- Text-based buttons (no icon fonts)
- No status bar
- Inter + FreeType font stack
- All changes within ImGui's immediate-mode paradigm
- Must not break existing panel functionality or 844 tests

---

## 1. Font Stack & FreeType

### Build Changes

**FreeType installation** (classic-mode vcpkg — no `vcpkg.json` in this project):
```
vcpkg install freetype:x64-windows
```

**CMakeLists.txt changes:**
1. Add `find_package(Freetype REQUIRED)` after the existing `find_package` calls (~line 210)
2. Add `${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp` to the `imgui_lib` STATIC sources (line 261)
3. Add `target_link_libraries(imgui_lib PUBLIC ... Freetype::Freetype)` (line 270)
4. Add `target_compile_definitions(imgui_lib PRIVATE IMGUI_ENABLE_FREETYPE)` (line 478, alongside existing defines)

Note: The define goes on `imgui_lib` (where `imgui_draw.cpp` is compiled), NOT on `FateEngine`. Putting it on `EDITOR_BUILD` would cause linker errors since ImGui's draw code would still be compiled without FreeType support.

**Font files:** Download Inter from https://github.com/rsms/inter/releases (OFL-1.1 license). Bundle:
- `assets/fonts/Inter-Regular.ttf`
- `assets/fonts/Inter-SemiBold.ttf`
- `assets/fonts/OFL.txt` (license file)

### Font Atlas Configuration (in `Editor::init`)

| Slot | Font | Size | Usage |
|------|------|------|-------|
| `fontBody` | Inter Regular | 14px | Default UI — labels, buttons, inputs |
| `fontHeading` | Inter SemiBold | 16px | CollapsingHeader titles, entity name in inspector |
| `fontSmall` | Inter Regular | 12px | FPS counter, metadata, tooltips, breadcrumb |

### FreeType Config
```cpp
ImFontConfig config;
config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
config.OversampleH = 1;
config.OversampleV = 1;
```

### Usage Pattern
- `ImGui::PushFont(fontHeading)` before CollapsingHeaders and the entity name input
- `ImGui::PushFont(fontSmall)` for FPS stats, entity counts, breadcrumb segments
- Default font handles everything else

### Member Variables
Add to `Editor` class:
```cpp
ImFont* fontBody_ = nullptr;
ImFont* fontHeading_ = nullptr;
ImFont* fontSmall_ = nullptr;
```

---

## 2. Color System

### Background Hierarchy

| Layer | Purpose | ImGuiCol | Hex |
|-------|---------|----------|-----|
| Deepest | Behind all panels | `DockingEmptyBg` | `#141416` |
| Panel body | Hierarchy, inspector, project | `WindowBg` | `#1E1E22` |
| Child panel | Same as body for seamless nesting | `ChildBg` | `#1E1E22` |
| Sidebar/toolbar | Viewport toolbar, section backgrounds | Pushed per-widget | `#252528` |
| Input fields | Text inputs, sliders, combos | `FrameBg` | `#2A2A2E` |
| Input hovered | | `FrameBgHovered` | `#333338` |
| Input active | | `FrameBgActive` | `#3C3C42` |
| Popups/menus | Context menus, dropdowns, tooltips | `PopupBg` | `#303036F5` (96% alpha for slight translucency) |

### Accent Color (blue)

| State | Hex | Usage |
|-------|-----|-------|
| Normal | `#4A8ADB` | Buttons, checkmarks, active tabs, selection |
| Hover | `#5E9AE8` | Button hover, header hover |
| Active | `#3670C0` | Button pressed, header active |
| Muted (30% alpha) | `#4A8ADB4D` | Selected rows, active toolbar buttons |

### CollapsingHeader Colors
- `Header`: `#2A2D32` — visible lift from panel background
- `HeaderHovered`: `#333842`
- `HeaderActive`: `#3A4050`

### Tab Colors
- `Tab` (inactive): `#18181C`
- `TabHovered`: `#333842`
- `TabSelected` (active): `#1E1E22` (matches panel body — "connects" to content)
- `TabSelectedOverline`: `#4A8ADB` (accent-colored top bar on active tab)
- `TabDimmed`: `#14141A`
- `TabDimmedSelected`: `#1A1A20`
- `TabDimmedSelectedOverline`: `#4A8ADB80` (muted overline on unfocused active tab)

### Title Bar & Menu Colors
- `TitleBg`: `#141418`
- `TitleBgActive`: `#1E1E22`
- `TitleBgCollapsed`: `#14141880`
- `MenuBarBg`: `#1A1A1E`

### Text Colors
- `Text`: `#D4D4D8` — primary (not pure white)
- `TextDisabled`: `#808088` — secondary/metadata
- `TextSelectedBg`: `#4A8ADB66`

### Other Interactive Colors
- `Button`: `#252530`
- `ButtonHovered`: `#333340`
- `ButtonActive`: `#3E3E4C`
- `Separator`: `#2A2A30`
- `Border`: `#2A2A30`
- `CheckMark`: `#4A8ADB`
- `SliderGrab`: `#4A8ADB`
- `SliderGrabActive`: `#5E9AE8`
- `ScrollbarBg`: `#141418`
- `ScrollbarGrab`: `#333338`
- `ScrollbarGrabHovered`: `#404048`
- `ScrollbarGrabActive`: `#505058`
- `ResizeGrip`: `#333338`
- `ResizeGripHovered`: `#4A8ADB99`
- `ResizeGripActive`: `#4A8ADBE6`
- `NavHighlight`: `#4A8ADB`

---

## 3. Spacing & Layout

### Style Values
```cpp
style.WindowPadding     = ImVec2(8, 8);
style.FramePadding      = ImVec2(6, 4);
style.ItemSpacing       = ImVec2(8, 4);
style.ItemInnerSpacing  = ImVec2(4, 4);
style.IndentSpacing     = 16.0f;
style.CellPadding       = ImVec2(4, 3);
style.ScrollbarSize     = 11.0f;
style.GrabMinSize       = 8.0f;
```

### Rounding
```cpp
style.WindowRounding    = 3.0f;
style.ChildRounding     = 3.0f;
style.FrameRounding     = 3.0f;
style.PopupRounding     = 4.0f;
style.ScrollbarRounding = 6.0f;
style.GrabRounding      = 3.0f;
style.TabRounding       = 3.0f;
```

### Borders
```cpp
style.WindowBorderSize  = 1.0f;
style.ChildBorderSize   = 0.0f;   // was 1.0, remove inner borders (animation editor state list uses BeginChild with border=true — this intentionally removes that border for a cleaner look; the state list is visually separated by its fixed width and the adjacent content instead)
style.PopupBorderSize   = 1.0f;
style.FrameBorderSize   = 0.0f;
style.TabBorderSize     = 0.0f;   // was 1.0, cleaner tabs
style.DockingSeparatorSize = 2.0f;
```

---

## 4. Hierarchy & Inspector Polish

### Hierarchy

**Tree indentation guides:** Draw subtle vertical connector lines (`IM_COL32(255, 255, 255, 25)`) using `GetWindowDrawList()->AddLine()`. Scope: only within expanded groups (the hierarchy is currently flat — single entities are leaves, groups are one level deep). For each expanded group, draw a vertical line from the group header down to the last child, positioned at `windowPos.x + groupIndentX + 8px`. No horizontal L-connectors needed — the vertical line alone provides sufficient visual structure.

**Selected item highlight:** The existing `ImGuiTreeNodeFlags_Selected` flag will pick up the updated `Header`/`HeaderHovered`/`HeaderActive` colors from Section 2, which provides a full-width accent bar. No custom draw-list rendering needed.

### Inspector

**Entity name:** Push `fontHeading` for the entity name `InputText` at the top of the inspector.

**Component headers:** Push `fontHeading` before each `CollapsingHeader`. The color changes from Section 2 handle the visual lift. Add `ImGui::Spacing()` between component sections.

**Add Component button:** Full-width with a subtle rounded outline. Use `GetWindowDrawList()->AddRect()` with `IM_COL32(128, 128, 128, 80)` and `rounding=3.0f` to draw a solid-but-faint border around the button area, then overlay a centered `ImGui::Button("+ Add Component")` with transparent background (`ImGuiCol_Button` pushed to `ImVec4(0,0,0,0)`).

**1px separator line:** After each open CollapsingHeader section, draw a subtle 1px line at the bottom of the section content.

---

## 5. Toolbar & Viewport

### Viewport Toolbar

**Button states:**
- Active tool: accent color at full opacity, rounded rect background
- Inactive: transparent background, text in secondary color (`#808088`)
- Hovered: `#FFFFFF` at 8% opacity fill

**Play/Stop labels:** Change `"|>"` to `"Play"` and `"||"` to `"Stop"`. Keep green/red background coloring.

**Toolbar background:** Push `ImGuiCol_ChildBg` to `ImVec4(0.145f, 0.145f, 0.157f, 1.0f)` (`#252528`) for the toolbar's `BeginChild` call. This replaces the current `#1A1A1F` push at editor.cpp line 489, making the toolbar visually distinct from the scene content.

**FPS stats:** Push `fontSmall` for the right-aligned metrics text.

---

## 6. Animation Editor & Asset Browser

### Animation Editor

**Frame strip:**
- Checkerboard transparency background behind each 48x48 frame thumbnail
  - Colors: `IM_COL32(40, 40, 40, 255)` and `IM_COL32(50, 50, 50, 255)`, 8px check size
  - Draw via `AddRectFilled` calls before the frame image
- Selected frame: 2px accent border (`#4A8ADB`) instead of button color change
- Frame index label below each frame in `fontSmall`
- "+" drop target: subtle rounded outline (`AddRect` with `IM_COL32(128, 128, 128, 60)`) instead of flat dark button

**Preview panel:**
- Checkerboard behind preview image (same pattern)
- Integer zoom (4x), `GL_NEAREST` (already configured)
- Transport controls styled as a compact row, active state highlighted
- Frame counter in `fontSmall`

**State list:**
- Push `fontHeading` for "States" section label
- Selected state gets accent highlight bar

### Asset Browser

**Thumbnails:**
- Checkerboard behind sprite thumbnails (same pattern as animation editor)
- Hovered: white overlay `IM_COL32(255, 255, 255, 15)`
- Selected: 2px accent border

**Type placeholders (desaturated colors):**
Replace `colorForType()` values with these muted versions:
```
Sprite:    {0.35f, 0.65f, 0.35f, 1.0f}   // was {0.4, 0.8, 0.4}
Script:    {0.35f, 0.65f, 0.80f, 1.0f}   // was {0.4, 0.8, 1.0}
Scene:     {0.35f, 0.80f, 0.50f, 1.0f}   // was {0.4, 1.0, 0.6}
Shader:    {0.80f, 0.58f, 0.35f, 1.0f}   // was {1.0, 0.7, 0.4}
Audio:     {0.72f, 0.35f, 0.72f, 1.0f}   // was {0.9, 0.4, 0.9}
Font:      {0.72f, 0.72f, 0.42f, 1.0f}   // was {0.9, 0.9, 0.5}
Tile:      {0.50f, 0.50f, 0.72f, 1.0f}   // was {0.6, 0.6, 0.9}
Prefab:    {0.80f, 0.50f, 0.50f, 1.0f}   // was {1.0, 0.6, 0.6}
Animation: {0.42f, 0.80f, 0.72f, 1.0f}   // was {0.5, 1.0, 0.9}
Other:     {0.50f, 0.50f, 0.50f, 1.0f}   // was {0.6, 0.6, 0.6}
```

**Breadcrumb:**
- Push `fontSmall` for breadcrumb segments
- Separator arrows in secondary text color (`#808088`)

---

## Helper: Checkerboard Drawing Function

Shared utility used by animation editor and asset browser:
```cpp
static void drawCheckerboard(ImDrawList* dl, ImVec2 pos, float w, float h, int checkSize = 8) {
    ImU32 c1 = IM_COL32(40, 40, 40, 255);
    ImU32 c2 = IM_COL32(50, 50, 50, 255);
    for (int y = 0; y < (int)h; y += checkSize) {
        for (int x = 0; x < (int)w; x += checkSize) {
            ImU32 c = ((x/checkSize + y/checkSize) % 2 == 0) ? c1 : c2;
            float x1 = pos.x + x, y1 = pos.y + y;
            float x2 = x1 + fminf((float)checkSize, w - x);
            float y2 = y1 + fminf((float)checkSize, h - y);
            dl->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), c);
        }
    }
}
```

---

## Files Modified

| File | Changes |
|------|---------|
| `CMakeLists.txt` | `find_package(Freetype)`, add `imgui_freetype.cpp` to `imgui_lib`, link Freetype, add `IMGUI_ENABLE_FREETYPE` define to `imgui_lib` |
| `engine/editor/editor.h` | Add `fontBody_`, `fontHeading_`, `fontSmall_` members |
| `engine/editor/editor.cpp` | Font loading, color system, spacing, inspector/hierarchy/toolbar polish |
| `engine/editor/animation_editor.cpp` | Checkerboard backgrounds, frame strip styling, preview polish |
| `engine/editor/asset_browser.cpp` | Checkerboard thumbnails, hover overlay, desaturated type colors, breadcrumb styling |
| `engine/editor/node_editor.cpp` | Harmonize imnodes style colors with new palette (update `ImNodes::StyleColorsDark()` follow-up pushes) |

Note: No `vcpkg.json` — project uses classic-mode vcpkg. FreeType installed via `vcpkg install freetype:x64-windows`.

### DPI Scaling
Out of scope for this session. Font sizes are fixed pixel values. `io.FontGlobalScale` remains `1.0f`. DPI-aware font rasterization can be added later if needed for high-DPI displays.

## Files Added

| File | Purpose |
|------|---------|
| `assets/fonts/Inter-Regular.ttf` | UI body font |
| `assets/fonts/Inter-SemiBold.ttf` | UI heading font |
