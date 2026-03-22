# Making Dear ImGui look stunning: a complete visual polish guide

A Dear ImGui editor can match the visual quality of Aseprite, Godot, or VS Code through disciplined application of custom theming, professional typography, layered dark color systems, and pixel-art-specific rendering techniques. **The difference between "programmer UI" and "professional tool" comes down to roughly 30 specific decisions** — color layering, font rendering, consistent spacing, and subtle animation — all achievable without leaving ImGui's immediate-mode paradigm. This guide covers every one of those decisions with concrete values, code, and rationale, organized from foundation (colors and fonts) through structure (layout and widgets) to finishing touches (animation and pixel-art display).

---

## 1. Building a cohesive dark theme from scratch

### The complete ImGuiCol_ color map

Dear ImGui exposes **55+ color slots** through the `ImGuiCol_` enum, stored as `ImVec4` (RGBA, 0.0–1.0) in `ImGuiStyle::Colors[]`. Understanding the full taxonomy is essential for a cohesive theme:

**Surface colors** control the background hierarchy: `WindowBg` (main window), `ChildBg` (child windows), `PopupBg` (tooltips, popups, menus), `MenuBarBg`, `ScrollbarBg`, `TableHeaderBg`, `TableRowBg`/`TableRowBgAlt`, `DockingEmptyBg`. **Frame colors** (`FrameBg`, `FrameBgHovered`, `FrameBgActive`) control input fields, checkboxes, sliders, and plots. **Interactive widget colors** span buttons (`Button`/`ButtonHovered`/`ButtonActive`), headers and selectables (`Header`/`HeaderHovered`/`HeaderActive`), tabs (`Tab`/`TabHovered`/`TabActive`/`TabUnfocused`/`TabUnfocusedActive`), sliders (`SliderGrab`/`SliderGrabActive`), scrollbar grabs (three states), resize grips (three states), and separators (three states). **Text colors** include `Text`, `TextDisabled`, and `TextSelectedBg`. **Structural colors** cover `Border`, `BorderShadow`, `TitleBg`/`TitleBgActive`/`TitleBgCollapsed`, and `CheckMark`. **Docking-specific** slots include `DockingPreview`. **Navigation** colors handle keyboard/gamepad focus: `NavHighlight`, `NavWindowingHighlight`, `NavWindowingDimBg`, `ModalWindowDimBg`.

### Recommended style values for a polished editor

Rounding creates the modern, approachable feel seen in Figma and Godot 4. These values hit the sweet spot between sharp and bubbly:

```cpp
style.WindowRounding    = 4.0f;    // Panels and windows
style.ChildRounding     = 4.0f;    // Match windows
style.FrameRounding     = 3.0f;    // Inputs, buttons, sliders
style.PopupRounding     = 4.0f;    // Tooltips, context menus
style.ScrollbarRounding = 9.0f;    // Pill-shaped scrollbar grab
style.GrabRounding      = 3.0f;    // Slider grab knob
style.TabRounding       = 4.0f;    // Docked tabs
```

Spacing controls information density. For an editor UI (interaction-heavy, not reading-heavy), tighter vertical spacing with comfortable horizontal breathing room works best:

```cpp
style.WindowPadding     = ImVec2(8, 8);     // Internal window margin
style.FramePadding      = ImVec2(6, 4);     // Click target padding
style.ItemSpacing       = ImVec2(8, 4);     // Tight vertical, readable horizontal
style.ItemInnerSpacing  = ImVec2(4, 4);     // Within compound widgets
style.IndentSpacing     = 16.0f;            // Tree node indentation
style.CellPadding       = ImVec2(4, 2);     // Table cells
style.ScrollbarSize     = 12.0f;            // Scrollbar track width
style.GrabMinSize       = 12.0f;            // Minimum slider grab size
```

**Border philosophy**: Use **1px borders on windows and popups**, **0px on frames and tabs** for a flat modern aesthetic. Only values of 0.0 or 1.0 are well-optimized in ImGui's renderer. Set `WindowBorderSize = 1.0f`, `ChildBorderSize = 1.0f`, `PopupBorderSize = 1.0f`, `FrameBorderSize = 0.0f`, `TabBorderSize = 0.0f`.

### Background layer hierarchy — the foundation of depth

The single most impactful technique for professional dark UIs is **layered backgrounds**. Never use pure black (`#000000`) — it causes halation (bright text appears to bleed) and eliminates room for shadow depth. Instead, build 4–5 elevation levels where higher means lighter:

| Layer | Purpose | Recommended hex | ImGuiCol_ |
|---|---|---|---|
| Deepest base | Behind all panels | `#121212` | `DockingEmptyBg` |
| Panel body | Main window surfaces | `#1E1E1E` | `WindowBg` |
| Sidebar/card | Elevated panels | `#252526` | `ChildBg` (or per-panel push) |
| Input fields | Interactive elements | `#2A2D2E` | `FrameBg` |
| Popovers/menus | Highest elevation | `#333333` | `PopupBg` |

This matches VS Code's exact approach: editor `#1E1E1E`, sidebar `#252526`, activity bar `#333333`. Blender uses a similar stack: `#282828` → `#303030` → `#353535` → `#3F3F3F` → `#545454`. Figma and YouTube both use `#1E1E1E` and `#181818` respectively as their base darks.

### Accent color and palette derivation

Apply the **60-30-10 rule**: 60% dark backgrounds, 30% lighter grays and text, 10% accent color. Choose one primary accent (blue is safest: `#4A9EFF` or VS Code's `#007ACC`) and derive the full interactive palette from it:

- **Accent normal**: `#4296FA` (buttons, checkmarks, active tabs, selection)
- **Accent hover**: lighten 10% → `#69ADFB`
- **Accent active/pressed**: darken 10% → `#2979E6`
- **Accent muted/disabled**: reduce saturation 50%, apply at 40% alpha
- **Accent background tint**: accent at 5–10% opacity over gray (for selected rows)

**Tinted grays** create subtle visual cohesion: shift all neutral grays toward the accent hue (e.g., 220° for blue) at 3–5% saturation. This is why VS Code's grays feel slightly cool rather than dead neutral.

**Semantic colors** must be desaturated ~20% for dark backgrounds to avoid visual harshness:

| Semantic | Hex (dark mode) | Usage |
|---|---|---|
| Error | `#CF6679` | Validation errors, delete confirmations |
| Warning | `#FFB74D` | Unsaved changes, deprecation notices |
| Success | `#81C784` | Build complete, save confirmed |
| Info | `#64B5F6` | Hints, documentation links |

### WCAG contrast for editor text

**Normal text** needs minimum **4.5:1** contrast against its background. **Large text** (≥18px or 14px bold) needs **3:1**. For primary text on `#1E1E1E`, use `#D4D4D4` (VS Code's choice) or `#E0E0E0` — both exceed 10:1. Avoid pure `#FFFFFF`, which at 15.8:1 causes unnecessary eye strain. **Disabled text** at `#666666` on `#1E1E1E` yields ~3.4:1, acceptable for decorative text but borderline for essential labels — use `#808080` (5.3:1) for anything that must remain readable.

### Community themes and runtime switching

The best starting points for theme exploration:

- **ImThemes** (github.com/Patitotective/ImThemes) — visual theme designer with real-time preview, exports C++
- **hello_imgui** (github.com/pthom/hello_imgui) — ships 17+ built-in themes including MaterialFlat, Darcula, DarculaDarker, PhotoshopStyle, SoDark variants (Blue/Yellow/Red), Cherry, BlackIsBlack
- **Issue #707** on ocornut/imgui — the canonical community theme megathread with dozens of shared themes
- **dear-imgui-styles** (github.com/GraphicsProgramming/dear-imgui-styles) — curated theme collection
- **ImGuiStyleSystem (IGSS)** — hierarchical style system with key-color derivation

**Runtime theme switching** is straightforward: assign to `ImGui::GetStyle()` directly, or use the three built-in presets (`StyleColorsDark()`, `StyleColorsLight()`, `StyleColorsClassic()`). For custom presets, serialize `ImGuiStyle` to JSON/TOML and deserialize on switch. The built-in `ImGui::ShowStyleEditor()` is invaluable for prototyping.

---

## 2. Typography that signals quality

### The right font stack

Font choice is the fastest path to a professional feel. **Inter** is the top recommendation — it was designed specifically for computer screens, is used by Figma, GitHub, Notion, and as of Blender 4.0 replaced DejaVu Sans as Blender's UI font. JetBrains IDEs also use Inter for their UI. Its high x-height (79%), stylistic sets for disambiguating `0/O` and `1/l/I`, and variable-weight support make it ideal.

For **monospace** (code, console, logs), **JetBrains Mono** leads the field — purpose-built for code reading with increased lowercase height and 138 ligatures. Cascadia Code (Microsoft's default in Windows Terminal) is a strong alternative. All recommended fonts carry SIL OFL 1.1 or Apache 2.0 licenses, fully embeddable in commercial tools.

**The recommended font stack for a pixel-art MMORPG editor:**

| Role | Font | Size (@ 1× DPI) | Weight |
|---|---|---|---|
| Default UI | Inter Regular | 14px | Regular |
| Emphasis/active tabs | Inter SemiBold | 14px | SemiBold |
| Panel titles | Inter SemiBold | 17px | SemiBold |
| Small metadata/status bar | Inter Regular | 12px | Regular |
| Code/console/logs | JetBrains Mono | 14px | Regular |
| Icons (merged) | FontAwesome 6 Solid | 14px | — |

Limit to **4–5 font sizes maximum** for clean hierarchy. Use a **1.2× modular scale**: 12 → 14 → 17 → 20.

### FreeType backend is mandatory for production

The default stb_truetype renderer produces visibly blurry text below 16px. **FreeType with `LightHinting`** transforms small text clarity dramatically — it snaps glyphs to the Y-axis (ClearType-style) while preserving inter-glyph spacing:

```cpp
// In imconfig.h:
#define IMGUI_ENABLE_FREETYPE

// In font loading code:
ImFontConfig config;
config.FontBuilderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
config.OversampleH = 1;  // FreeType handles AA; oversampling unnecessary
config.OversampleV = 1;
ImFont* font = io.Fonts->AddFontFromFileTTF("Inter-Regular.ttf", 14.0f, &config);
```

With FreeType, set oversampling to 1×1 since FreeType's own anti-aliasing is superior. For headers at 18px+, `NoHinting` preserves the font designer's curves. For pixel-perfect bitmap rendering, combine `MonoHinting | Monochrome`.

### Icon font integration pattern

**FontAwesome 6 Free Solid** (~2,000 icons) provides the best general coverage for editor UI needs: file operations, play/pause/stop, transform icons, layers, visibility toggles, settings gears. Supplement with **Kenney Game Icons** for game-specific glyphs (gamepad, D-pad, joystick). **Lucide** (1,600+ icons, ISC license) is the cleanest modern alternative if FontAwesome feels heavy.

The **IconFontCppHeaders** library (github.com/juliettef/IconFontCppHeaders) provides `#define` constants for every icon's UTF-8 codepoint, plus `ICON_MIN_*`/`ICON_MAX_16_*` range defines for the font atlas:

```cpp
#include "IconsFontAwesome6.h"

// Merge icons into the primary font
ImFontConfig iconCfg;
iconCfg.MergeMode        = true;       // KEY: merge into previous font
iconCfg.PixelSnapH       = true;       // Align to pixel grid
iconCfg.GlyphMinAdvanceX = 14.0f;      // Monospace icons
iconCfg.GlyphOffset      = ImVec2(0, 2); // Vertical alignment tweak

static const ImWchar ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
io.Fonts->AddFontFromFileTTF("fa-solid-900.ttf", 14.0f, &iconCfg, ranges);

// Usage — icons are just UTF-8 string literals:
ImGui::Button(ICON_FA_FLOPPY_DISK " Save");
ImGui::TreeNode(ICON_FA_CUBE " GameObject");
ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Open Project");
```

### DPI scaling done right

Always re-rasterize fonts at the target DPI — **never rely solely on `FontGlobalScale`**, which just scales rendered quads and produces blurry text. Query the platform scale factor (`SDL_GetDisplayDPI` for SDL2), then load fonts at `floorf(baseSize * dpiScale)`. Round to integer pixel sizes to avoid fractional-pixel blurriness. ImGui v1.92+ simplifies this with `style.FontScaleDpi`.

---

## 3. Custom widgets and the ImDrawList toolkit

### Drawing polished custom elements

`ImGui::GetWindowDrawList()` exposes the `ImDrawList` API for custom rendering within any window. Key primitives for editor polish:

**Gradient headers** use `AddRectFilledMultiColor()` for vertical or horizontal gradients across panel title bars. This single call replaces flat title backgrounds with depth-suggesting gradients. Note: `AddRectFilledMultiColor` does not support rounding — clip to a rounded region or accept rectangular gradients.

```cpp
ImVec2 pos = ImGui::GetCursorScreenPos();
float width = ImGui::GetContentRegionAvail().x;
draw_list->AddRectFilledMultiColor(
    pos, ImVec2(pos.x + width, pos.y + 30),
    IM_COL32(60, 60, 120, 255), IM_COL32(60, 60, 120, 255),  // top
    IM_COL32(30, 30, 60, 255),  IM_COL32(30, 30, 60, 255));   // bottom
```

**Drop shadows** are best achieved via a 9-slice shadow texture drawn behind panels using `GetBackgroundDrawList()`. A popular implementation exists as a GitHub Gist (kpcftsz/b044b43213564f2fb32e8685a50daf6a). For simpler cases, draw 3–4 progressively larger, more transparent `AddRectFilled` calls behind the target element.

**Styled toolbar buttons** with transparent backgrounds and hover highlights:

```cpp
ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));         // transparent
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.15f));
ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,1,1,0.25f));
ImGui::Button(ICON_FA_PLAY "##play");
ImGui::PopStyleColor(3);
```

**Custom title bars** (removing OS chrome) require platform-specific window hints (`SDL_CreateWindow` with `SDL_WINDOW_BORDERLESS`) and manual drag implementation via `SDL_HitTest` or direct `WM_NCLBUTTONDOWN` messages on Windows. The borderless-imgui-window repository (cazzwastaken/borderless-imgui-window) provides a complete template.

### State management for animations

Immediate-mode GUIs require external state tracking for animations. The standard pattern uses a map keyed by `ImGuiID`:

```cpp
static std::unordered_map<ImGuiID, float> hoverAnim;
ImGuiID id = ImGui::GetID("MyButton");
float& t = hoverAnim[id];
float target = ImGui::IsItemHovered() ? 1.0f : 0.0f;
t += (target - t) * ImGui::GetIO().DeltaTime * 10.0f; // exponential lerp
```

**Exponential decay** (`value += (target - value) * rate * dt`) is the recommended approach because it naturally handles interruptions — if the user moves the mouse away mid-animation, it simply reverses smoothly. This is the technique described by Ryan Fleury in his UI architecture writings: store `hot_t` and `active_t` per widget, animate both independently.

The **ImAnim library** (github.com/soufianekhiat/ImAnim) provides a production-ready animation system with 30+ easing functions, multiple color space interpolation (sRGB, linear, HSV, OKLAB, OKLCH), and crossfade policies for interrupted transitions. For simpler needs, these easing functions cover most cases:

```cpp
float EaseOutCubic(float t)  { return 1.0f - powf(1.0f - t, 3.0f); }
float EaseInOutQuad(float t) { return t < 0.5f ? 2*t*t : 1 - powf(-2*t+2, 2)/2; }
```

Animations require continuous rendering — disable ImGui's idle/sleep mode during active animations. Keep transitions brief (**150–200ms**) to avoid feeling sluggish in an editor workflow.

---

## 4. Layout architecture and visual hierarchy

### The 8px grid system

All spacing, sizing, margins, and padding should use multiples of **8** (with 4 as the fine-tuning unit). This creates subconscious visual order:

- **Element heights**: buttons and inputs at **32px** or **40px** (both multiples of 8)
- **Panel padding**: **8px** or **16px** internal margins
- **Item gaps**: **4px** tight, **8px** comfortable, **16px** section breaks
- **Icon sizes**: **16×16** (standard) or **24×24** (large) — always divisible by 8
- **Thumbnail grids**: **64×64** or **128×128** tiles with **4–8px** gaps

The **internal ≤ external rule** (Gestalt proximity) means padding inside a card should be less than or equal to the gap between cards, so grouped elements feel cohesive.

### Panel header pattern

Every panel in professional editors follows the same anatomy: `[Icon 16×16] [8px gap] [Title 13px SemiBold] ... [Options ⋮] [Collapse ▾]`. The header should be 32–40px tall with a background slightly lighter or darker than the panel body. A 1px bottom border or subtle shadow separates it from content. Collapse chevrons rotate 90° when collapsed.

### Property inspector layout

The property panel (like Unity's Inspector or Godot's Inspector) uses a consistent **two-column layout**: labels left-aligned at ~40% width in secondary text color (`#A0A0A0`, 12px), values filling the remaining width in primary text color. Group properties into **collapsible sections** (Transform, Appearance, Physics) with 16–24px indentation for nested properties. Row height of **24–32px** keeps things scannable. Interactive values use inline sliders, color swatches, and drag-to-scrub number inputs.

### Asset browser grid design

Render square thumbnails at consistent sizes (user-adjustable: 48, 64, 96, or 128px) with **4–8px grid gaps**. Name labels below thumbnails at 10–11px, truncated with ellipsis. Selected items get an accent-colored outline (**2px, `#4A9EFF`**); hovered items get a subtle white overlay (`rgba(255,255,255,0.05)`). Provide both grid and list view modes with a toggle. A search/filter bar at the top is essential.

---

## 5. Rendering pixel art without compromise

### GL_NEAREST is non-negotiable

Every pixel art texture must use nearest-neighbor filtering. This is set once during texture creation and applies to all ImGui::Image() calls displaying that texture:

```cpp
glGenTextures(1, &texture);
glBindTexture(GL_TEXTURE_2D, texture);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
// Do NOT call glGenerateMipmap — mipmaps cause blending artifacts
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
```

**Integer scaling** is critical: always display sprites at whole-number multiples (2×, 3×, 4×, 8×) to ensure every source pixel maps to an identical block of screen pixels. A 20×32 chibi sprite at 4× becomes 80×128 — perfect for thumbnails. At 8× it becomes 160×256, ideal for a detail view. Non-integer zoom produces uneven pixel sizes (some pixels visually larger than neighbors), which is the telltale sign of amateur pixel art tooling. If non-integer zoom is required, use a specialized pixel art shader that snaps UVs to texel centers with `fwidth()`-based smoothing at edges.

### Checkerboard transparency backgrounds

The Photoshop/Aseprite-style checkerboard communicates transparency. For **dark themes**, use two dark grays — `rgb(40,40,40)` and `rgb(50,50,50)` — rather than the traditional light gray/white which creates distracting contrast. The most efficient ImGui approach draws filled rects:

```cpp
ImVec2 pos = ImGui::GetCursorScreenPos();
int checkSize = 8;
ImU32 col1 = IM_COL32(40, 40, 40, 255), col2 = IM_COL32(50, 50, 50, 255);
for (int y = 0; y < displayH; y += checkSize)
    for (int x = 0; x < displayW; x += checkSize) {
        ImU32 c = ((x/checkSize + y/checkSize) % 2 == 0) ? col1 : col2;
        draw_list->AddRectFilled(
            ImVec2(pos.x+x, pos.y+y),
            ImVec2(pos.x+fmin(x+checkSize, displayW), pos.y+fmin(y+checkSize, displayH)), c);
    }
draw_list->AddImage(spriteTex, pos, ImVec2(pos.x+displayW, pos.y+displayH));
```

For GPU efficiency with large preview areas, use a **fragment shader** with `floor(gl_FragCoord.xy / checkSize)` and `mod(sum, 2.0)`, or create a tiny 2×2 repeating texture with `GL_REPEAT` wrapping.

### Sprite thumbnail polish

Professional sprite thumbnails layer several visual elements: checkerboard background → sprite image (GL_NEAREST) → selection outline → hover overlay → drop shadow. Selected thumbnails get a **2px accent-colored border** (`IM_COL32(0, 120, 255, 255)`). Hovered thumbnails get a semi-transparent white overlay (`IM_COL32(255, 255, 255, 30)`). Drop shadows are offset **3px down-right** with `IM_COL32(0, 0, 0, 80)` and optional 4px rounding.

### Zoom, pan, and animation preview

Implement sprite preview with mouse-wheel zoom (locked to integer factors for pixel art) and middle-click pan. The **imgui_tex_inspect** library (github.com/andyborrell/imgui_tex_inspect) provides a ready-made texture inspector with zoom, pan, and per-texel RGBA readout. For animation playback, track `currentFrame` and `frameTimer`, advancing frames based on `ImGui::GetIO().DeltaTime` against a configurable FPS. Display previous/next frames as semi-transparent onion skin overlays using `AddImage` with tinted alpha (`IM_COL32(255, 0, 0, 80)` for previous, `IM_COL32(0, 0, 255, 80)` for next).

### Grid overlay that scales with zoom

The gold-standard grid shader uses `fwidth()` for automatic anti-aliasing at any zoom:

```glsl
vec2 grid = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
float line = min(grid.x, grid.y);
float alpha = 1.0 - min(line, 1.0);
```

For a pixel art editor, render **two grid levels**: minor (per-pixel or per-tile) at low opacity (`0.1`), major (every 8th or 16th cell) at higher opacity (`0.3`). **Fade the fine grid** based on zoom level using `smoothstep(4.0, 8.0, zoom)` — below 4× zoom, hide per-pixel lines entirely since they'd become a solid gray mass. The ImDrawList CPU approach works well for moderate-size canvases and integrates cleanly with ImGui's rendering.

---

## 6. Learning from the best editor UIs

### What Aseprite gets right

Aseprite's distinctiveness comes from **total commitment to its pixel-art identity**: custom bitmap fonts (non-anti-aliased), 200% screen scaling that makes the entire UI feel chunky and pixel-art-native, minimal chrome, and a dark background (`#2B2B2B` range) punctuated by bright accent colors. The lesson: **visual coherence through a single strong aesthetic decision** outweighs any number of individual polish tweaks.

### LDtk's friendly technical aesthetic

LDtk (by the Dead Cells lead developer) uses **per-layer color coding** drawn from the Endesga32 palette — vibrant, game-friendly hues that make layer relationships immediately visible. The UI is modern with rounded elements and a dark background, but color is the primary information carrier. The takeaway: **semantic color coding** transforms dense technical interfaces into scannable, even enjoyable workspaces.

### How Godot 4 and Blender achieved their polish

Godot 4.6's new "Modern" theme (derived from community contribution) demonstrates that **rounded corners, consistent icon sizing (16×16 SVG, theme-tinted), and Inter as the UI font** are the three highest-leverage changes. Blender's 2.8+ redesign used **elevation-via-lightness** across multiple background shades (`#282828` → `#303030` → `#353535` → `#3F3F3F`), collapsible panel sections to manage density, and monochrome theme-tintable icons. Both editors prove that **a small, consistent icon system** (monochrome, single-stroke-weight, theme-adaptable) creates more visual coherence than elaborate multi-color icon sets.

### VS Code's color token architecture

VS Code organizes **300+ color tokens** in a dot-notation namespace system: `editor.background`, `sideBar.background`, `activityBar.background`, `statusBar.foreground`, `tab.activeBackground`, etc. Each component area owns its own foreground, background, and border tokens. This architectural clarity — **one namespace per UI region** — is worth replicating even in a simpler editor. Define your colors as named constants grouped by panel, not by color.

---

## 7. The extension library ecosystem

### Essential visual libraries

Dear ImGui's ecosystem includes production-ready extensions that add capabilities you shouldn't build from scratch:

**ImSpinner** (github.com/dalerank/imspinner) — header-only library with **100+ spinner/loading indicator types**: rotating arcs, bouncing dots, pulsing circles, DNA helixes, rainbow effects, bar-style loaders, and many more. Uses `ImGui::GetTime()` for continuous animation. Essential for save/load/build indicators.

**imgui-notify** (github.com/patrickcjk/imgui-notify, enhanced fork at TyomaVader/ImGuiNotify) — toast notification system with auto-fade opacity phases (fade-in → display → fade-out), configurable duration, and FontAwesome icons for Success/Warning/Error/Info types. The enhanced fork adds clickable buttons with lambda callbacks.

**ImCoolBar** (github.com/aiekick/ImCoolBar) — macOS Dock-style magnification toolbar where items smoothly enlarge on hover. Configurable normal/hovered sizes and anchor positions.

**ImPlot** (github.com/epezent/implot) — GPU-accelerated 2D plotting with line, scatter, bar, heatmap, pie, and more. Useful for performance profiling panels, terrain height visualizations, or color distribution histograms. Includes 16 built-in colormaps and auto-theme matching.

**imgui-node-editor** (github.com/thedmd/imgui-node-editor) — full-featured node graph editor with canvas pan/zoom, link management, and custom node content. Professional visual quality suitable for shader graphs or AI behavior trees.

**imgui-knobs** (github.com/altschuler/imgui-knobs) — rotary knob widgets in 7 visual styles (Tick, Dot, Wiper, WiperOnly, WiperDot, Stepped, Space). Useful for audio settings, rotation parameters, or any dial-style value.

**imgui_toggle** (github.com/cmdwft/imgui_toggle) — iOS-style animated toggle switches. **ImGuiColorTextEdit** — syntax-highlighting text editor widget. **ImFileDialog** — modern file browser with thumbnails. **Dear ImGui Bundle** (github.com/pthom/imgui_bundle) packages 20+ of these libraries pre-wired together.

### Rendering game content inside ImGui panels

Use render-to-texture: render your game world to an FBO, then display the resulting texture via `ImGui::Image()`. For custom shader effects within ImGui panels, use `draw_list->AddCallback()` to inject OpenGL state changes:

```cpp
draw_list->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
    // Change shader, blend mode, etc.
}, nullptr);
// ... draw commands with custom state ...
draw_list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
```

### Tile palette with virtual scrolling

For large tilesets, use `ImGuiListClipper` to only render visible rows of tiles. Treat each row of tiles as one clipper item with height equal to the tile display size. This keeps performance constant regardless of tileset size. Both LDtk and Tiled render the tileset as a single image with a grid overlay, computing tile selection from mouse position divided by tile size.

---

## 8. Pixel art editor-specific visual details

**Marching ants selection**: Implement with a shader using `stroke-dasharray`-equivalent math — diagonal black-and-white stripes animated by shifting UV offset based on `ImGui::GetTime()`. Two alternating colors (black + white) ensure visibility against any pixel color. Typical parameters: 2px width, 4–8px dash length, ~200ms per full cycle.

**Tool cursors**: Replace the system cursor with tool-specific cursors — crosshair for pencil, square outline for eraser, bucket icon for fill, open/closed hand for pan. For thematic consistency in a pixel art editor, use custom pixel-art-style cursor sprites at 16×16 or 32×32.

**Zoom indicator**: Display in the status bar as `4×` or `800%` (pixel editors tend to prefer integer multiplier notation). Make it clickable with preset zoom levels: 1×, 2×, 4×, 8×, 16×. Show current **pixel coordinates** (`X: 32, Y: 48`) and canvas dimensions (`64×64`) in the status bar, updating in real-time as the cursor moves.

**Layer panel**: Standard row format — `[Eye icon] [Lock icon] [Type icon] [Name] [Opacity]` at 24–32px row height. Highlight the active layer with a low-opacity accent background. Support drag reordering. Use color-coded layer type icons (following LDtk's approach) for instant visual scanning.

**Animation timeline**: Horizontal strip of frame thumbnails at the bottom, with play/pause, step forward/back, and loop toggle controls. Per-frame duration display in milliseconds below each thumbnail. Color-coded tag ranges above frames for animation states (idle, walk, attack).

---

## Conclusion

The path from default ImGui to professional editor polish follows a clear priority order. **First**, establish the color foundation: layered backgrounds (`#121212` → `#1E1E1E` → `#252526` → `#333333`), a single accent color with derived states, and proper text contrast ratios. **Second**, upgrade typography: Inter at 14px with FreeType LightHinting, merged FontAwesome icons, and 4–5 size tiers for hierarchy. **Third**, apply consistent spacing on an 8px grid with 4px fine-tuning. These three steps alone transform the feel from "programmer tool" to "production software."

The remaining polish — gradient headers, smooth hover animations, drop shadows, custom title bars, spinner libraries, checkerboard backgrounds, integer-scaled pixel art — layers on top of that solid foundation. The most overlooked insight from studying Aseprite, LDtk, Godot, and Blender is that **consistency beats complexity**: a limited palette of 5 background shades, one accent color, one font family, and one icon set applied with absolute consistency will always look more professional than a mismatched collection of individually impressive features. Every ImGuiCol_ value, every spacing constant, every font size should trace back to a single coherent system. That system, not any single technique, is what makes an editor beautiful.