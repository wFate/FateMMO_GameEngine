# Editor UI Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform the FateMMO editor from default ImGui styling to Unity/Aseprite-level visual polish through fonts, colors, spacing, and widget refinement.

**Architecture:** All changes are in the ImGui init/render layer. FreeType is added as a build dependency for sharp font rendering. Three font sizes create visual hierarchy. A refined color system with layered backgrounds creates depth. Widget-level polish (checkerboards, styled headers, tree guides) completes the professional feel.

**Tech Stack:** Dear ImGui (v1.91.9b-docking), FreeType (via vcpkg), Inter font family, SDL2, OpenGL 3.3

**Spec:** `Docs/superpowers/specs/2026-03-22-editor-ui-polish-design.md`

**Build command:**
```bash
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\cppwinrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\cppwinrt\\winrt"
export PATH="/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64:$PATH"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
```

**CRITICAL:** Always `touch` every `.cpp` file you edit before building. CMake misses changes silently.

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `CMakeLists.txt` | Modify | FreeType dependency, imgui_freetype.cpp source, link + define |
| `engine/editor/editor.h` | Modify | Add font pointer members + pass to sub-editors |
| `engine/editor/animation_editor.h` | Modify | Add font pointer setters |
| `engine/editor/asset_browser.h` | Modify | Add font pointer setters |
| `engine/editor/editor.cpp` | Modify | Font loading, full color/spacing/rounding overhaul, inspector/hierarchy/toolbar polish |
| `engine/editor/animation_editor.cpp` | Modify | Checkerboard backgrounds, frame strip styling, preview polish |
| `engine/editor/asset_browser.cpp` | Modify | Checkerboard thumbnails, hover overlay, desaturated colors, breadcrumb styling |
| `engine/editor/node_editor.cpp` | Modify | Harmonize imnodes colors with new palette |
| `assets/fonts/Inter-Regular.ttf` | Add | Body font (14px, 12px) |
| `assets/fonts/Inter-SemiBold.ttf` | Add | Heading font (16px) |
| `assets/fonts/OFL.txt` | Add | Inter font license |

---

### Task 1: Install FreeType & Add to Build

**Files:**
- Modify: `CMakeLists.txt:206-210` (find_package section), `CMakeLists.txt:250-262` (imgui_lib sources), `CMakeLists.txt:270` (imgui_lib link), `CMakeLists.txt:478` (imgui_lib defines)

- [ ] **Step 1: Install FreeType via vcpkg**

```bash
/c/vcpkg/vcpkg install freetype:x64-windows
```

Expected: FreeType installed to `C:/vcpkg/installed/x64-windows/`

- [ ] **Step 2: Add find_package for Freetype**

In `CMakeLists.txt`, after line 209 (`find_package(PostgreSQL REQUIRED)`), add:

```cmake
    find_package(Freetype REQUIRED)
```

- [ ] **Step 3: Add imgui_freetype.cpp to imgui_lib sources**

In `CMakeLists.txt`, after line 261 (`${imnodes_SOURCE_DIR}/imnodes.cpp`), add:

```cmake
    ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp
```

- [ ] **Step 4: Link Freetype to imgui_lib**

In `CMakeLists.txt`, change line 270 from:
```cmake
target_link_libraries(imgui_lib PUBLIC SDL2::SDL2-static ${OPENGL_LIB})
```
to:
```cmake
target_link_libraries(imgui_lib PUBLIC SDL2::SDL2-static ${OPENGL_LIB} Freetype::Freetype)
```

Also add FreeType include dir to the existing `target_include_directories(imgui_lib ...)` block (line 263-269) — add one line after the `backends` entry:
```cmake
    ${imgui_SOURCE_DIR}/misc/freetype
```
This is a safety measure so `imgui_freetype.cpp` can find its header via include path.

**Note:** After Steps 2-4 add lines to CMakeLists.txt, subsequent line numbers in this file will shift by ~3-4 lines. Use content matching (grep) rather than relying on exact line numbers for Step 5.

- [ ] **Step 5: Add IMGUI_ENABLE_FREETYPE define to imgui_lib**

In `CMakeLists.txt`, change line 478 from:
```cmake
    target_compile_definitions(imgui_lib PRIVATE _CRT_SECURE_NO_WARNINGS IMGUI_DEFINE_MATH_OPERATORS)
```
to:
```cmake
    target_compile_definitions(imgui_lib PRIVATE _CRT_SECURE_NO_WARNINGS IMGUI_DEFINE_MATH_OPERATORS IMGUI_ENABLE_FREETYPE)
```

- [ ] **Step 6: Reconfigure and build to verify FreeType links**

```bash
"$CMAKE" -S . -B out/build/x64-Debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

Expected: Build succeeds. Look for `imgui_freetype.cpp.obj` in output to confirm it compiled.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: integrate FreeType for ImGui font rendering"
```

---

### Task 2: Bundle Inter Font Files

**Files:**
- Add: `assets/fonts/Inter-Regular.ttf`
- Add: `assets/fonts/Inter-SemiBold.ttf`
- Add: `assets/fonts/OFL.txt`

- [ ] **Step 1: Download Inter font**

Download from https://github.com/rsms/inter/releases — get the latest release zip. Extract:
- `Inter-Regular.ttf` (from the `extras/ttf/` folder or the static TTF files)
- `Inter-SemiBold.ttf`
- `OFL.txt` (license file from the root of the zip)

Place them in `assets/fonts/`.

**Alternative if download is not possible:** Use `WebFetch` or `curl` to download from the GitHub releases page, or ask the user to manually place the files.

- [ ] **Step 2: Verify files exist**

```bash
ls -la assets/fonts/
```

Expected: `Inter-Regular.ttf`, `Inter-SemiBold.ttf`, `OFL.txt` all present.

- [ ] **Step 3: Commit**

```bash
git add assets/fonts/Inter-Regular.ttf assets/fonts/Inter-SemiBold.ttf assets/fonts/OFL.txt
git commit -m "assets: add Inter font family for editor UI"
```

---

### Task 3: Font Loading in Editor Init

**Files:**
- Modify: `engine/editor/editor.h:171-180` (private members section)
- Modify: `engine/editor/editor.cpp:65-98` (init function)

- [ ] **Step 1: Add font pointer members to Editor class**

In `engine/editor/editor.h`, after line 178 (`bool wantsMouse_ = false;`), add:

```cpp
    // Font stack (loaded in init, used via PushFont/PopFont)
    ImFont* fontBody_ = nullptr;
    ImFont* fontHeading_ = nullptr;
    ImFont* fontSmall_ = nullptr;
```

- [ ] **Step 2: Add font loading in Editor::init**

In `engine/editor/editor.cpp`, in the `init()` function, replace the single line `ImGui::StyleColorsDark();` at line 77 with the font loading block below. The `ImGuiStyle& style = ImGui::GetStyle();` line at line 78 and the entire style/color block that follows (lines 78-141) will be replaced separately in Task 4. This task only touches line 77:

```cpp
    // Load Inter font family with FreeType hinting
    ImFontConfig fontCfg;
    fontCfg.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
    fontCfg.OversampleH = 1;
    fontCfg.OversampleV = 1;

    // Body font (14px) — default for all UI
    fontBody_ = io.Fonts->AddFontFromFileTTF("assets/fonts/Inter-Regular.ttf", 14.0f, &fontCfg);

    // Heading font (16px) — CollapsingHeaders, entity names
    fontCfg.MergeMode = false;
    fontHeading_ = io.Fonts->AddFontFromFileTTF("assets/fonts/Inter-SemiBold.ttf", 16.0f, &fontCfg);

    // Small font (12px) — metadata, FPS, breadcrumbs
    fontSmall_ = io.Fonts->AddFontFromFileTTF("assets/fonts/Inter-Regular.ttf", 12.0f, &fontCfg);

    // Fallback: if fonts fail to load, ImGui uses its built-in font
    if (!fontBody_) {
        LOG_WARN("Editor", "Failed to load Inter fonts — using ImGui default");
        fontBody_ = io.Fonts->AddFontDefault();
        fontHeading_ = fontBody_;
        fontSmall_ = fontBody_;
    }

    io.Fonts->Build();

    ImGui::StyleColorsDark();
```

- [ ] **Step 3: Add font pointer setters to AnimationEditor and AssetBrowser**

These classes are separate from `Editor` and need font access for styling. Add setter methods.

In `engine/editor/animation_editor.h`, add after the `void setOpen(bool o)` line:
```cpp
    void setFonts(ImFont* heading, ImFont* small) { fontHeading_ = heading; fontSmall_ = small; }
```

And add private members at the end of the private section:
```cpp
    ImFont* fontHeading_ = nullptr;
    ImFont* fontSmall_ = nullptr;
```

In `engine/editor/asset_browser.h`, add in the public section after the `onOpenAnimation` callback:
```cpp
    void setFonts(ImFont* heading, ImFont* small) { fontHeading_ = heading; fontSmall_ = small; }
```

And add private members:
```cpp
    ImFont* fontHeading_ = nullptr;
    ImFont* fontSmall_ = nullptr;
```

- [ ] **Step 4: Wire font pointers from Editor to sub-editors**

In `engine/editor/editor.cpp`, in `Editor::init()`, after the `io.Fonts->Build();` call and after font loading, add:
```cpp
    animationEditor_.setFonts(fontHeading_, fontSmall_);
    assetBrowser_.setFonts(fontHeading_, fontSmall_);
```

- [ ] **Step 5: Touch and build**

```bash
touch engine/editor/editor.cpp engine/editor/editor.h engine/editor/animation_editor.h engine/editor/asset_browser.h
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

Expected: Build succeeds. At runtime, editor should display with Inter font.

- [ ] **Step 6: Commit**

```bash
git add engine/editor/editor.h engine/editor/editor.cpp engine/editor/animation_editor.h engine/editor/asset_browser.h
git commit -m "feat(editor): load Inter font stack with FreeType rendering"
```

---

### Task 4: Color System & Spacing Overhaul

**Files:**
- Modify: `engine/editor/editor.cpp:78-141` (style and color setup in init)

- [ ] **Step 1: Replace the entire style/color block in Editor::init**

In `engine/editor/editor.cpp`, replace the block from `ImGuiStyle& style = ImGui::GetStyle();` through all `c[ImGuiCol_...] = ...` lines (lines 78-141) with:

```cpp
    ImGuiStyle& style = ImGui::GetStyle();

    // Spacing — 8px grid, tight vertical, comfortable horizontal
    style.WindowPadding     = ImVec2(8, 8);
    style.FramePadding      = ImVec2(6, 4);
    style.CellPadding       = ImVec2(4, 3);
    style.ItemSpacing       = ImVec2(8, 4);
    style.ItemInnerSpacing  = ImVec2(4, 4);
    style.IndentSpacing     = 16.0f;
    style.ScrollbarSize     = 11.0f;
    style.GrabMinSize       = 8.0f;

    // Rounding — subtle modern softness
    style.WindowRounding    = 3.0f;
    style.ChildRounding     = 3.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 3.0f;

    // Borders — minimal, modern
    style.WindowBorderSize     = 1.0f;
    style.ChildBorderSize      = 0.0f;
    style.PopupBorderSize      = 1.0f;
    style.FrameBorderSize      = 0.0f;
    style.TabBorderSize        = 0.0f;
    style.DockingSeparatorSize = 2.0f;

    // Color scheme — layered dark backgrounds with blue accent
    ImVec4* c = style.Colors;

    // Background hierarchy (darkest → lightest)
    c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.078f, 0.078f, 0.086f, 1.00f); // #141416
    c[ImGuiCol_WindowBg]             = ImVec4(0.118f, 0.118f, 0.133f, 1.00f); // #1E1E22
    c[ImGuiCol_ChildBg]              = ImVec4(0.118f, 0.118f, 0.133f, 1.00f); // #1E1E22
    c[ImGuiCol_PopupBg]              = ImVec4(0.188f, 0.188f, 0.212f, 0.96f); // #303036 F5
    c[ImGuiCol_FrameBg]              = ImVec4(0.165f, 0.165f, 0.180f, 1.00f); // #2A2A2E
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.200f, 0.200f, 0.220f, 1.00f); // #333338
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.235f, 0.235f, 0.259f, 1.00f); // #3C3C42

    // Title bar & menu
    c[ImGuiCol_TitleBg]              = ImVec4(0.078f, 0.078f, 0.094f, 1.00f); // #141418
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.118f, 0.118f, 0.133f, 1.00f); // #1E1E22
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.078f, 0.078f, 0.094f, 0.50f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.102f, 0.102f, 0.118f, 1.00f); // #1A1A1E

    // Tabs
    c[ImGuiCol_Tab]                  = ImVec4(0.094f, 0.094f, 0.110f, 1.00f); // #18181C
    c[ImGuiCol_TabHovered]           = ImVec4(0.200f, 0.220f, 0.259f, 1.00f); // #333842
    c[ImGuiCol_TabSelected]          = ImVec4(0.118f, 0.118f, 0.133f, 1.00f); // #1E1E22
    c[ImGuiCol_TabSelectedOverline]  = ImVec4(0.290f, 0.541f, 0.859f, 1.00f); // #4A8ADB
    c[ImGuiCol_TabDimmed]            = ImVec4(0.078f, 0.078f, 0.102f, 1.00f); // #14141A
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.102f, 0.102f, 0.125f, 1.00f); // #1A1A20
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.290f, 0.541f, 0.859f, 0.50f);

    // Headers (CollapsingHeader, Selectable)
    c[ImGuiCol_Header]               = ImVec4(0.165f, 0.176f, 0.196f, 1.00f); // #2A2D32
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.200f, 0.220f, 0.259f, 1.00f); // #333842
    c[ImGuiCol_HeaderActive]         = ImVec4(0.227f, 0.251f, 0.314f, 1.00f); // #3A4050

    // Buttons
    c[ImGuiCol_Button]               = ImVec4(0.145f, 0.145f, 0.188f, 1.00f); // #252530
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.200f, 0.200f, 0.251f, 1.00f); // #333340
    c[ImGuiCol_ButtonActive]         = ImVec4(0.243f, 0.243f, 0.298f, 1.00f); // #3E3E4C

    // Text
    c[ImGuiCol_Text]                 = ImVec4(0.831f, 0.831f, 0.847f, 1.00f); // #D4D4D8
    c[ImGuiCol_TextDisabled]         = ImVec4(0.502f, 0.502f, 0.533f, 1.00f); // #808088
    c[ImGuiCol_TextSelectedBg]       = ImVec4(0.290f, 0.541f, 0.859f, 0.40f);

    // Borders & separators
    c[ImGuiCol_Border]               = ImVec4(0.165f, 0.165f, 0.188f, 1.00f); // #2A2A30
    c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.165f, 0.165f, 0.188f, 1.00f); // #2A2A30
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.290f, 0.541f, 0.859f, 0.60f);
    c[ImGuiCol_SeparatorActive]      = ImVec4(0.290f, 0.541f, 0.859f, 1.00f);

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.078f, 0.078f, 0.094f, 1.00f); // #141418
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.200f, 0.200f, 0.220f, 1.00f); // #333338
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.251f, 0.251f, 0.282f, 1.00f); // #404048
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.314f, 0.314f, 0.345f, 1.00f); // #505058

    // Accent-colored interactive elements
    c[ImGuiCol_CheckMark]            = ImVec4(0.290f, 0.541f, 0.859f, 1.00f); // #4A8ADB
    c[ImGuiCol_SliderGrab]           = ImVec4(0.290f, 0.541f, 0.859f, 0.80f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.369f, 0.604f, 0.910f, 1.00f); // #5E9AE8

    // Resize grip
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.200f, 0.200f, 0.220f, 0.40f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.290f, 0.541f, 0.859f, 0.60f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0.290f, 0.541f, 0.859f, 0.90f);

    // Docking & nav
    c[ImGuiCol_DockingPreview]       = ImVec4(0.290f, 0.541f, 0.859f, 0.40f);
    c[ImGuiCol_NavHighlight]         = ImVec4(0.290f, 0.541f, 0.859f, 1.00f);
```

- [ ] **Step 2: Touch and build**

```bash
touch engine/editor/editor.cpp
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

Expected: Build succeeds. At runtime, editor shows refined color palette with layered backgrounds.

- [ ] **Step 3: Run tests**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests && out/build/x64-Debug/fate_tests
```

Expected: All 844 tests pass (visual-only changes should not affect logic tests).

- [ ] **Step 4: Commit**

```bash
git add engine/editor/editor.cpp
git commit -m "feat(editor): refined color system and spacing for professional look"
```

---

### Task 5: Toolbar & Viewport Polish

**Files:**
- Modify: `engine/editor/editor.cpp:486-620` (drawSceneViewport toolbar section)

- [ ] **Step 1: Update toolbar background color**

In `engine/editor/editor.cpp`, in `drawSceneViewport()`, change the `PushStyleColor` for `ChildBg` (line 489) from:
```cpp
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.12f, 1.00f));
```
to:
```cpp
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.145f, 0.145f, 0.157f, 1.00f));
```

- [ ] **Step 2: Update tool button styling**

In the `toolBtn` lambda (around line 499), replace:
```cpp
            auto toolBtn = [&](const char* label, EditorTool tool) {
                bool active = (currentTool_ == tool);
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.80f, 1.00f));
                if (ImGui::Button(label, ImVec2(0, btnH))) currentTool_ = tool;
                if (active) ImGui::PopStyleColor();
                ImGui::SameLine();
            };
```
with:
```cpp
            auto toolBtn = [&](const char* label, EditorTool tool) {
                bool active = (currentTool_ == tool);
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.290f, 0.541f, 0.859f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.369f, 0.604f, 0.910f, 1.00f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.502f, 0.502f, 0.533f, 1.0f));
                }
                if (ImGui::Button(label, ImVec2(0, btnH))) currentTool_ = tool;
                if (active) ImGui::PopStyleColor(2);
                else ImGui::PopStyleColor(3);
                ImGui::SameLine();
            };
```

- [ ] **Step 3: Update toggle button styling**

Replace the `toggleBtn` lambda (around line 519) similarly:
```cpp
            auto toggleBtn = [&](const char* label, bool* val) {
                bool wasActive = *val;
                if (wasActive) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.290f, 0.541f, 0.859f, 0.50f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.290f, 0.541f, 0.859f, 0.70f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.502f, 0.502f, 0.533f, 1.0f));
                }
                if (ImGui::Button(label, ImVec2(0, btnH))) *val = !(*val);
                if (wasActive) ImGui::PopStyleColor(2);
                else ImGui::PopStyleColor(3);
                ImGui::SameLine();
            };
```

- [ ] **Step 4: Change Play/Stop button labels**

Change `"|>"` to `"Play"` and `"||"` to `"Stop"` in the play/stop section (around lines 538 and 550). Keep the green/red background colors as they are.

- [ ] **Step 5: Push fontSmall for FPS stats**

Replace the entire FPS stats block (around line 594-606) with this version that pushes `fontSmall_` before `CalcTextSize` so the width measurement and text rendering both use the small font:

```cpp
            {
                ImGuiIO& io = ImGui::GetIO();
                char stats[64];
                snprintf(stats, sizeof(stats), "%.0f FPS | %zu ent",
                         io.Framerate, dockWorld_ ? dockWorld_->entityCount() : 0u);
                if (fontSmall_) ImGui::PushFont(fontSmall_);
                float textW = ImGui::CalcTextSize(stats).x;
                float regionW = ImGui::GetContentRegionAvail().x;
                if (regionW > textW + 4.0f) {
                    ImGui::SameLine(ImGui::GetCursorPosX() + regionW - textW);
                    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.50f, 1.0f), "%s", stats);
                }
                if (fontSmall_) ImGui::PopFont();
            }
```

- [ ] **Step 6: Touch and build**

```bash
touch engine/editor/editor.cpp
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

- [ ] **Step 7: Commit**

```bash
git add engine/editor/editor.cpp
git commit -m "feat(editor): polish viewport toolbar styling and button states"
```

---

### Task 6: Inspector Polish

**Files:**
- Modify: `engine/editor/editor.cpp:2374-3391` (drawInspector function)

- [ ] **Step 1: Push fontHeading for entity name**

In `drawInspector()`, around line 2386, wrap the entity name InputText:
```cpp
        // -- Entity name (prominent input at top) --
        char nameBuf[128];
        strncpy(nameBuf, selectedEntity_->name().c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (fontHeading_) ImGui::PushFont(fontHeading_);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##EntityName", nameBuf, sizeof(nameBuf))) {
            selectedEntity_->setName(nameBuf);
        }
        if (fontHeading_) ImGui::PopFont();
```

- [ ] **Step 2: Push fontHeading for each CollapsingHeader**

For every `CollapsingHeader` call in the inspector, wrap with `fontHeading_`. There are ~25 CollapsingHeader calls. The correct pattern is:

```cpp
        if (fontHeading_) ImGui::PushFont(fontHeading_);
        bool transformOpen = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
        if (fontHeading_) ImGui::PopFont();
        if (transformOpen) {
            // ... contents ...
        }
```

**IMPORTANT — Headers with right-click remove menus:** Most component headers (Box Collider, Polygon Collider, Player Controller, Animator, Zone, Portal, etc.) are followed by a `BeginPopupContextItem` call for right-click removal. The `PopFont()` must come BETWEEN the `CollapsingHeader` and the `BeginPopupContextItem`:

```cpp
        if (fontHeading_) ImGui::PushFont(fontHeading_);
        bool boxOpen = ImGui::CollapsingHeader("Box Collider", ImGuiTreeNodeFlags_DefaultOpen);
        if (fontHeading_) ImGui::PopFont();
        if (ImGui::BeginPopupContextItem("##rmBoxCollider")) {
            // ... remove menu ...
            ImGui::EndPopup();
        }
        if (boxOpen && selectedEntity_->hasComponent<BoxCollider>()) {
            // ... contents ...
        }
```

**Social/marker components** (Party, Guild, Chat, Friends, Trade, Market at lines 3092-3133) currently call `CollapsingHeader` without storing the result. Change them to use the same stored-result pattern:

```cpp
        if (selectedEntity_->hasComponent<PartyComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            ImGui::CollapsingHeader("Party Manager");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmParty")) {
                // ... existing remove menu ...
                ImGui::EndPopup();
            }
        }
```

Apply to ALL CollapsingHeader calls:
- Transform (~line 2421) — no context menu
- Sprite (~line 2452) — no context menu
- Box Collider (~line 2525) — has context menu
- Polygon Collider (~line 2563) — has context menu
- Player Controller (~line 2625) — has context menu
- Animator (~line 2657) — has context menu
- Zone (~line 2687) — has context menu
- Portal (~line 2747) — has context menu
- Character Stats (~line 2805) — has context menu
- Enemy Stats (~line 2893) — has context menu
- Mob AI (~line 2928) — has context menu
- Combat Controller (~line 2957) — has context menu
- Inventory (~line 2977) — has context menu
- Skill Manager (~line 2990) — has context menu
- Status Effects (~line 3002) — has context menu
- Crowd Control (~line 3016) — has context menu
- Nameplate (~line 3031) — has context menu
- Mob Nameplate (~line 3048) — has context menu
- Targeting (~line 3068) — has context menu
- Damageable (~line 3082) — has context menu
- Party/Guild/Chat/Friends/Trade/Market (~lines 3092-3133) — have context menus
- Spawn Zone (~line 3136) — has context menu
- Faction (~line 3196) — has context menu
- Pet (~line 3212) — has context menu

- [ ] **Step 3: Add spacing between component sections**

After each component section (after the closing `}` of each component block), add `ImGui::Spacing();`. This visually separates the sections.

- [ ] **Step 4: Add 1px separator after open component sections**

After the closing `}` of each component section's content block (not the header, the content), add a subtle separator line. Use a DrawList line rather than `ImGui::Separator()` for a thinner, more controlled look. Add this helper pattern after each component block:

```cpp
        // After each component section that was open:
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(p.x, p.y), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y),
                IM_COL32(255, 255, 255, 15));
        }
        ImGui::Spacing();
```

Apply this after Transform, Sprite, BoxCollider, PolygonCollider, PlayerController, Animator, Zone, Portal, CharacterStats, EnemyStats, MobAI, CombatController, and SpawnZone sections (the ones that have visible content when open). Skip marker-only components.

- [ ] **Step 5: Style the Add Component button**

Replace the current Add Component button (around line 3279):
```cpp
        ImGui::Separator();
        if (ImGui::Button("+ Add Component")) ImGui::OpenPopup("AddComponent");
```
with:
```cpp
        ImGui::Spacing();
        ImGui::Spacing();
        // Full-width outlined "Add Component" button
        {
            float availW = ImGui::GetContentRegionAvail().x;
            ImVec2 btnPos = ImGui::GetCursorScreenPos();
            ImVec2 btnSize(availW, ImGui::GetFrameHeight());

            // Draw subtle outline
            ImGui::GetWindowDrawList()->AddRect(
                btnPos,
                ImVec2(btnPos.x + btnSize.x, btnPos.y + btnSize.y),
                IM_COL32(128, 128, 128, 80), 3.0f);

            // Transparent button overlaid
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.06f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.10f));
            if (ImGui::Button("+ Add Component", btnSize)) ImGui::OpenPopup("AddComponent");
            ImGui::PopStyleColor(3);
        }
```

- [ ] **Step 6: Touch and build**

```bash
touch engine/editor/editor.cpp
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

- [ ] **Step 7: Commit**

```bash
git add engine/editor/editor.cpp
git commit -m "feat(editor): inspector polish — heading fonts, spacing, styled add-component button"
```

---

### Task 7: Hierarchy Polish

**Files:**
- Modify: `engine/editor/editor.cpp:2205-2311` (drawHierarchy function)

- [ ] **Step 1: Add tree indentation guides for expanded groups**

In `drawHierarchy()`, inside the `for (auto& group : groups)` loop, when a group is expanded (the `if (open)` block around line 2295), draw a vertical connector line:

```cpp
                if (open) {
                    // Draw tree indentation guide
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float indentX = ImGui::GetCursorScreenPos().x - ImGui::GetStyle().IndentSpacing * 0.5f + 4.0f;
                    float startY = ImGui::GetCursorScreenPos().y;

                    for (auto* entity : group.entities) {
                        // ... existing entity rendering code ...
                    }

                    float endY = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;
                    if (endY > startY) {
                        dl->AddLine(
                            ImVec2(indentX, startY),
                            ImVec2(indentX, endY),
                            IM_COL32(255, 255, 255, 25), 1.0f);
                    }

                    ImGui::TreePop();
                }
```

The key is to capture `startY` before the child loop and `endY` after it, then draw the line.

- [ ] **Step 2: Touch and build**

```bash
touch engine/editor/editor.cpp
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

- [ ] **Step 3: Commit**

```bash
git add engine/editor/editor.cpp
git commit -m "feat(editor): hierarchy tree indentation guides"
```

---

### Task 8: Animation Editor Visual Polish

**Files:**
- Modify: `engine/editor/animation_editor.cpp:301-427` (drawFrameStrip), `engine/editor/animation_editor.cpp:579-654` (drawPreview), `engine/editor/animation_editor.cpp:248-296` (drawStateList)

- [ ] **Step 1: Add checkerboard helper at the top of the file**

In `engine/editor/animation_editor.cpp`, after the namespace includes (after line 14), add:

```cpp
namespace {
// Draw a checkerboard transparency background (dark theme variant)
static void drawCheckerboard(ImDrawList* dl, ImVec2 pos, float w, float h, int checkSize = 8) {
    ImU32 c1 = IM_COL32(40, 40, 40, 255);
    ImU32 c2 = IM_COL32(50, 50, 50, 255);
    for (int y = 0; y < (int)h; y += checkSize) {
        for (int x = 0; x < (int)w; x += checkSize) {
            ImU32 c = ((x / checkSize + y / checkSize) % 2 == 0) ? c1 : c2;
            float x1 = pos.x + (float)x, y1 = pos.y + (float)y;
            float x2 = x1 + fminf((float)checkSize, w - (float)x);
            float y2 = y1 + fminf((float)checkSize, h - (float)y);
            dl->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), c);
        }
    }
}
} // anonymous namespace
```

- [ ] **Step 2: Add checkerboard behind frame strip thumbnails**

In `drawFrameStrip()`, inside the frame loop, before the `ImageButton` call (around line 321), add checkerboard drawing:

```cpp
        // Draw checkerboard behind frame
        ImVec2 framePos = ImGui::GetCursorScreenPos();
        drawCheckerboard(ImGui::GetWindowDrawList(), framePos, 48.0f, 48.0f);
```

- [ ] **Step 3: Style selected frame with accent border instead of button color**

Replace the selected-frame color push (around line 313-316):
```cpp
        bool isSelected = (i == selectedFrameIdx_);
        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.6f, 0.9f, 1.0f));
        }
```
with:
```cpp
        bool isSelected = (i == selectedFrameIdx_);
```

And replace the pop after the button (around line 335-337):
```cpp
        if (isSelected) {
            ImGui::PopStyleColor(2);
        }
```
with:
```cpp
        // Draw accent border on selected frame
        if (isSelected) {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(min, max, IM_COL32(74, 138, 219, 255), 2.0f, 0, 2.0f);
        }
```

- [ ] **Step 4: Style the "+" drop target with subtle outline**

Replace the "+" button styling (around line 382-386):
```cpp
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    if (ImGui::Button("+##drop", ImVec2(48, 48))) {
        // Clicking does nothing — this is just a drop target
    }
    ImGui::PopStyleColor();
```
with:
```cpp
    // Draw outlined drop target
    {
        ImVec2 dropPos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRect(
            dropPos, ImVec2(dropPos.x + 48, dropPos.y + 48),
            IM_COL32(128, 128, 128, 60), 3.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.06f));
        if (ImGui::Button("+##drop", ImVec2(48, 48))) {}
        ImGui::PopStyleColor(2);
    }
```

- [ ] **Step 5: Add checkerboard behind preview image**

In `drawPreview()`, before the `ImGui::Image()` call (around line 625), add:

```cpp
    if (previewFrame_ >= 0 && previewFrame_ < (int)frames.size()) {
        unsigned int texId = loadFrameTexture(frames[previewFrame_]);
        ImVec2 previewPos = ImGui::GetCursorScreenPos();
        drawCheckerboard(ImGui::GetWindowDrawList(), previewPos, 128.0f, 128.0f);
        if (texId != 0) {
            ImGui::Image((ImTextureID)(intptr_t)texId, ImVec2(128, 128));
```

Remove the duplicate `Dummy` fallback since the checkerboard already fills the space.

- [ ] **Step 6: Add frame index label below each frame**

In `drawFrameStrip()`, after the frame button and the accent border drawing (before `ImGui::SameLine()`), add a small label:

```cpp
        // Frame index label
        if (fontSmall_) ImGui::PushFont(fontSmall_);
        ImGui::Text("%d", i);
        if (fontSmall_) ImGui::PopFont();
```

Note: This will be on the line below the frame button. The `SameLine()` at the end of each frame should come after this label — adjust positioning so frames flow horizontally. Actually, since the label breaks the horizontal flow, wrap the frame button + label in a `BeginGroup()/EndGroup()` to keep them as a unit:

```cpp
        ImGui::BeginGroup();
        // ... checkerboard + ImageButton code ...
        // ... accent border code ...
        if (fontSmall_) ImGui::PushFont(fontSmall_);
        float labelW = ImGui::CalcTextSize("00").x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (48.0f - labelW) * 0.5f);
        ImGui::Text("%d", i);
        if (fontSmall_) ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SameLine();
```

Remove the existing standalone `ImGui::SameLine();` at the end of the loop to avoid double-spacing.

- [ ] **Step 7: Push fontSmall for preview frame counter**

In `drawPreview()`, wrap the frame counter text (around line 631) with fontSmall:

```cpp
    if (fontSmall_) ImGui::PushFont(fontSmall_);
    ImGui::Text("Frame %d / %d", previewFrame_ + 1, frameCount);
    if (fontSmall_) ImGui::PopFont();
```

- [ ] **Step 8: Style state list selected state with accent bar**

In `drawStateList()`, in the Selectable call (around line 253), push the accent color for the selected state:

```cpp
        bool selected = (i == selectedStateIdx_);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.290f, 0.541f, 0.859f, 0.30f));
        }
        if (ImGui::Selectable(template_.states[i].name.c_str(), selected)) {
            selectedStateIdx_ = i;
            selectedFrameIdx_ = -1;
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
```

- [ ] **Step 9: Touch and build**

```bash
touch engine/editor/animation_editor.cpp
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

- [ ] **Step 10: Commit**

```bash
git add engine/editor/animation_editor.cpp
git commit -m "feat(editor): animation editor visual polish — checkerboards, accent borders, frame labels"
```

---

### Task 9: Asset Browser Polish

**Files:**
- Modify: `engine/editor/asset_browser.cpp:16-48` (colorForType), `engine/editor/asset_browser.cpp:199-241` (drawBreadcrumb), `engine/editor/asset_browser.cpp:256-429` (drawGrid)

- [ ] **Step 1: Add checkerboard helper**

In `engine/editor/asset_browser.cpp`, inside the anonymous namespace at the top (after line 48), add the same `drawCheckerboard` function from Task 8.

- [ ] **Step 2: Update colorForType with desaturated values**

Replace the `colorForType` switch body (lines 21-33) with:

```cpp
    switch (type) {
        case 0:  return {0.35f, 0.65f, 0.35f, 1.0f};   // Sprite - green
        case 1:  return {0.35f, 0.65f, 0.80f, 1.0f};   // Script - cyan
        case 2:  return {0.35f, 0.80f, 0.50f, 1.0f};   // Scene - mint
        case 3:  return {0.80f, 0.58f, 0.35f, 1.0f};   // Shader - orange
        case 4:  return {0.72f, 0.35f, 0.72f, 1.0f};   // Audio - purple
        case 5:  return {0.72f, 0.72f, 0.42f, 1.0f};   // Font - yellow
        case 6:  return {0.50f, 0.50f, 0.72f, 1.0f};   // Tile - blue
        case 7:  return {0.80f, 0.50f, 0.50f, 1.0f};   // Prefab - red
        case 8:  return {0.42f, 0.80f, 0.72f, 1.0f};   // Animation - teal
        default: return {0.50f, 0.50f, 0.50f, 1.0f};   // Other - gray
    }
```

- [ ] **Step 3: Add checkerboard behind sprite thumbnails**

In `drawGrid()`, before the `ImageButton` for sprite thumbnails (around line 317), add:

```cpp
            // Draw checkerboard behind sprite thumbnail
            ImVec2 thumbPos = ImGui::GetCursorScreenPos();
            drawCheckerboard(ImGui::GetWindowDrawList(), thumbPos, itemW - 8, itemH - 8);
```

- [ ] **Step 4: Add hover overlay on thumbnails**

After the `ImageButton` / `Button` call for each entry (but before the EndGroup), add hover detection:

```cpp
        // Hover overlay
        if (ImGui::IsItemHovered()) {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(255, 255, 255, 15));
        }
```

- [ ] **Step 5: Add accent border on selected thumbnails**

For sprites, after the existing selected-color push/pop, also draw an accent border on the selected item:

```cpp
        // Selection accent border
        if (draggedAssetPath_ == entry.relativePath) {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(min, max, IM_COL32(74, 138, 219, 255), 2.0f, 0, 2.0f);
        }
```

- [ ] **Step 6: Style breadcrumb with fontSmall**

In `drawBreadcrumb()` (around line 199), wrap the entire function body with fontSmall:

```cpp
void AssetBrowser::drawBreadcrumb() {
    if (fontSmall_) ImGui::PushFont(fontSmall_);

    // Root button
    if (ImGui::SmallButton("assets")) {
        navigateTo("");
    }
    // ... rest of existing breadcrumb code ...

    if (fontSmall_) ImGui::PopFont();
}
```

Also change the `ImGui::TextDisabled("/")` separator (around line 228) to use the secondary text color explicitly:

```cpp
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.502f, 0.502f, 0.533f, 1.0f), ">");
            ImGui::SameLine();
```

This replaces the "/" with ">" arrows and uses the secondary text color.

- [ ] **Step 7: Touch and build**

```bash
touch engine/editor/asset_browser.cpp
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

- [ ] **Step 8: Commit**

```bash
git add engine/editor/asset_browser.cpp
git commit -m "feat(editor): asset browser polish — checkerboards, desaturated colors, hover/selection, breadcrumb"
```

---

### Task 10: Node Editor Color Harmonization

**Files:**
- Modify: `engine/editor/node_editor.cpp:13-18` (init function)

- [ ] **Step 1: Harmonize imnodes colors with new palette**

In `DialogueNodeEditor::init()`, after `ImNodes::StyleColorsDark()` (line 17), add style overrides to match the new palette:

```cpp
    ImNodes::StyleColorsDark();

    // Harmonize with editor palette
    ImNodesStyle& nodeStyle = ImNodes::GetStyle();
    nodeStyle.Colors[ImNodesCol_NodeBackground]       = IM_COL32(30, 30, 34, 255);   // #1E1E22
    nodeStyle.Colors[ImNodesCol_NodeBackgroundHovered] = IM_COL32(38, 38, 44, 255);
    nodeStyle.Colors[ImNodesCol_NodeBackgroundSelected]= IM_COL32(42, 45, 50, 255);  // #2A2D32
    nodeStyle.Colors[ImNodesCol_TitleBar]              = IM_COL32(42, 45, 50, 255);   // #2A2D32
    nodeStyle.Colors[ImNodesCol_TitleBarHovered]       = IM_COL32(51, 56, 66, 255);   // #333842
    nodeStyle.Colors[ImNodesCol_TitleBarSelected]      = IM_COL32(74, 138, 219, 255); // #4A8ADB
    nodeStyle.Colors[ImNodesCol_Link]                  = IM_COL32(74, 138, 219, 200);
    nodeStyle.Colors[ImNodesCol_LinkHovered]           = IM_COL32(94, 154, 232, 255);
    nodeStyle.Colors[ImNodesCol_LinkSelected]          = IM_COL32(74, 138, 219, 255);
    nodeStyle.Colors[ImNodesCol_GridBackground]        = IM_COL32(20, 20, 22, 255);   // #141416
    nodeStyle.Colors[ImNodesCol_GridLine]              = IM_COL32(42, 42, 48, 100);
    nodeStyle.Colors[ImNodesCol_Pin]                   = IM_COL32(74, 138, 219, 255);
    nodeStyle.Colors[ImNodesCol_PinHovered]            = IM_COL32(94, 154, 232, 255);
    nodeStyle.Flags |= ImNodesStyleFlags_GridLines;
```

- [ ] **Step 2: Touch and build**

```bash
touch engine/editor/node_editor.cpp
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

- [ ] **Step 3: Commit**

```bash
git add engine/editor/node_editor.cpp
git commit -m "feat(editor): harmonize dialogue node editor colors with new palette"
```

---

### Task 11: Final Build Verification & Test Run

- [ ] **Step 1: Touch all modified files and rebuild**

```bash
touch engine/editor/editor.cpp engine/editor/editor.h engine/editor/animation_editor.cpp engine/editor/asset_browser.cpp engine/editor/node_editor.cpp
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

Expected: Clean build, no warnings.

- [ ] **Step 2: Run full test suite**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests && out/build/x64-Debug/fate_tests
```

Expected: All 844 tests pass.

- [ ] **Step 3: Visual smoke test**

Launch the editor and verify:
- [ ] Inter font renders clearly at all three sizes (body, heading, small)
- [ ] Panel backgrounds show distinct layered shading
- [ ] CollapsingHeaders have visible lift from panel background with semibold text
- [ ] Toolbar buttons show accent color when active, transparent when inactive
- [ ] Tab overlines show accent blue on active tabs
- [ ] Asset browser thumbnails have checkerboard backgrounds
- [ ] Animation editor frames have checkerboard backgrounds
- [ ] Entity name in inspector shows in heading font
- [ ] "+ Add Component" has subtle outline style
- [ ] Dialogue node editor colors match the new palette
- [ ] Hierarchy groups show vertical indentation guides when expanded
