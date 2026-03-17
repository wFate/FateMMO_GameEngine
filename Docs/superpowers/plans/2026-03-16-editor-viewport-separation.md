# Editor Viewport Separation Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restructure the editor from floating ImGui overlays to a Unity-style dockable layout with FBO-backed scene viewport.

**Architecture:** Game renders to an FBO when editor is open, displayed via `ImGui::Image` in a dockable viewport panel. All editor panels (hierarchy, inspector, asset browser, etc.) live in an `ImGui::DockSpace` outside the viewport. When editor is closed, game renders directly to screen with zero FBO overhead.

**Tech Stack:** C++17, SDL2, OpenGL 3.3 Core (custom loader), Dear ImGui (docking branch), SpriteBatch renderer

**Spec:** `Docs/superpowers/specs/2026-03-16-editor-viewport-separation-design.md`

**Build command:**
```bash
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build build --config Debug
```

**CMake note:** `CMakeLists.txt` uses `GLOB_RECURSE engine/*.cpp` — new files under `engine/` are automatically picked up. No CMakeLists.txt edit needed.

---

## Chunk 1: Framebuffer Class + Editor Header Changes

### Task 1: Create Framebuffer class

**Files:**
- Create: `engine/render/framebuffer.h`
- Create: `engine/render/framebuffer.cpp`

- [ ] **Step 1: Write `engine/render/framebuffer.h`**

```cpp
#pragma once
#include "engine/render/gl_loader.h"

namespace fate {

class Framebuffer {
public:
    Framebuffer() = default;
    // No auto-cleanup in destructor (matches SpriteBatch::init/shutdown pattern)
    ~Framebuffer() = default;

    bool create(int width, int height);
    void destroy();
    void resize(int width, int height);
    void bind();
    void unbind();

    unsigned int textureId() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool isValid() const { return fbo_ != 0; }

private:
    unsigned int fbo_ = 0;
    unsigned int texture_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace fate
```

- [ ] **Step 2: Write `engine/render/framebuffer.cpp`**

```cpp
#include "engine/render/framebuffer.h"
#include "engine/core/logger.h"

namespace fate {

bool Framebuffer::create(int width, int height) {
    if (width <= 0 || height <= 0) return false;

    width_ = width;
    height_ = height;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("Framebuffer", "FBO incomplete: 0x%x", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        destroy();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    LOG_INFO("Framebuffer", "Created FBO %u (%dx%d)", fbo_, width_, height_);
    return true;
}

void Framebuffer::destroy() {
    if (texture_) { glDeleteTextures(1, &texture_); texture_ = 0; }
    if (fbo_) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    width_ = 0;
    height_ = 0;
}

void Framebuffer::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == width_ && height == height_) return;
    destroy();
    create(width, height);
}

void Framebuffer::bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

void Framebuffer::unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace fate
```

- [ ] **Step 3: Build to verify compilation**

```bash
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build build --config Debug
```
Expected: Compiles cleanly. Framebuffer is not yet used so no linker issues.

- [ ] **Step 4: Commit**

```bash
git add engine/render/framebuffer.h engine/render/framebuffer.cpp
git commit -m "Add Framebuffer class for FBO-backed viewport rendering"
```

### Task 2: Update Editor header with new state and method signatures

**Files:**
- Modify: `engine/editor/editor.h`

- [ ] **Step 1: Add includes and new members to `editor.h`**

Add `#include "engine/render/framebuffer.h"` to the includes.

Add these new members to the `private` section (after `bool showDemoWindow_`):

```cpp
    // Viewport FBO
    Framebuffer viewportFbo_;

    // Viewport tracking (set each frame by drawSceneViewport)
    Vec2 viewportPos_ = {0, 0};    // screen-space position of viewport content
    Vec2 viewportSize_ = {0, 0};   // pixel size of viewport content region
    bool viewportHovered_ = false;  // mouse is over viewport content

    // Toolbar toggles
    bool showGrid_ = true;
    bool showCollisionDebug_ = false;
```

- [ ] **Step 2: Add new public method signatures**

Replace the existing `render()` signature with the split methods. Add viewport accessors. Keep all existing methods.

Add to public section:
```cpp
    // Split render into scene (FBO-bound) and UI (ImGui panels)
    void renderScene(SpriteBatch* batch, Camera* camera);
    void renderUI(World* world, Camera* camera, SpriteBatch* batch);

    // Viewport info (for input routing in App)
    Vec2 viewportPos() const { return viewportPos_; }
    Vec2 viewportSize() const { return viewportSize_; }
    bool isViewportHovered() const { return viewportHovered_; }

    // Toolbar toggle accessors
    bool showCollisionDebug() const { return showCollisionDebug_; }
    bool showGrid() const { return showGrid_; }

    // FBO management
    Framebuffer& viewportFbo() { return viewportFbo_; }
```

- [ ] **Step 3: Add new private draw method signatures**

Add to the private draw functions section:
```cpp
    void drawDockSpace();
    void drawSceneViewport();
    void drawViewportHUD(World* world);
    void drawDebugInfoPanel(World* world);
```

- [ ] **Step 4: Do NOT build or commit yet** — Task 3 must be completed first (new methods declared here are not yet implemented). Tasks 2 and 3 are committed together.

---

## Chunk 2: Editor Implementation — DockSpace, Viewport Panel, Split Render

### Task 3: Implement DockSpace, viewport panel, and split render methods

**Files:**
- Modify: `engine/editor/editor.cpp` (lines 91-120: current `render()`, lines 1244-1275: `drawHUD()`, lines 1316-1470: `drawToolbar()`)

This is the largest task. The key changes to `editor.cpp`:

1. Replace `render()` with `renderScene()` + `renderUI()`
2. Add `drawDockSpace()` — full-window ImGui DockSpace with DockBuilder default layout
3. Add `drawSceneViewport()` — ImGui window showing FBO texture via `ImGui::Image`
4. Add `drawDebugInfoPanel()` — FPS, entity count, camera info
5. Modify `drawToolbar()` — add Grid toggle, Collision Debug toggle
6. Remove fixed `SetNextWindowPos`/`SetNextWindowSize` from all panels (let DockSpace manage them)
7. Modify `shutdown()` to call `viewportFbo_.destroy()`

- [ ] **Step 1: Replace `render()` with `renderScene()` and `renderUI()`**

Replace lines 91-120 of `editor.cpp` (the current `render` method) with:

```cpp
void Editor::renderScene(SpriteBatch* batch, Camera* camera) {
    // Called while FBO is bound — draw in-viewport overlays via SpriteBatch
    if (!open_ || !batch || !camera) return;

    if (showGrid_) {
        drawSceneGrid(batch, camera);
    }

    // Selection highlight (already inside drawSceneGrid, which does its own begin/end)
    // Grid already handles selection rendering, so nothing else needed here
}

void Editor::renderUI(World* world, Camera* camera, SpriteBatch* batch) {
    if (!frameStarted_) return;

    // HUD always visible (when editor is closed, this is the only thing drawn)
    if (!open_) {
        drawHUD(world);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        return;
    }

    // Full editor UI
    drawDockSpace();
    drawMenuBar(world);
    drawToolbar(world);
    drawSceneViewport();
    // Draw tile coord HUD positioned inside the viewport panel (ImGui overlay)
    drawViewportHUD(world);
    drawHierarchy(world);
    drawInspector();
    drawConsole(world);
    LogViewer::instance().draw();
    drawTilePalette(world, camera);
    drawAssetBrowser(world, camera);
    drawDebugInfoPanel(world);

    if (showDemoWindow_) {
        ImGui::ShowDemoWindow(&showDemoWindow_);
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
```

Remove the old `render()` method entirely — `app.cpp` will be updated in Task 4 to call the new methods.

- [ ] **Step 1b: Implement `drawViewportHUD()` — tile coord HUD positioned inside viewport panel**

This draws the tile coordinate HUD as an ImGui overlay positioned relative to the viewport panel's content region when the editor is open. It cannot render into the FBO (ImGui defers rendering), so instead we position it on top of the viewport panel area.

```cpp
void Editor::drawViewportHUD(World* world) {
    if (!world || viewportSize_.x <= 0 || viewportSize_.y <= 0) return;

    Entity* player = world->findByTag("player");
    if (!player) return;

    auto* t = player->getComponent<Transform>();
    if (!t) return;

    char buf[64];
    snprintf(buf, sizeof(buf), "(%d, %d)", Coords::tileX(t->position.x), Coords::tileY(t->position.y));

    ImVec2 textSize = ImGui::CalcTextSize(buf);
    ImVec2 padding(12.0f, 6.0f);
    float winWidth = textSize.x + padding.x * 2.0f;
    // Center horizontally within viewport panel
    float x = viewportPos_.x + (viewportSize_.x - winWidth) * 0.5f;
    float y = viewportPos_.y + 6.0f;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::Begin("##HUD_Viewport", nullptr, flags);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.9f), "%s", buf);
    ImGui::End();
    ImGui::PopStyleVar(2);
}
```

Add `drawViewportHUD` to the private draw functions in `editor.h` (Task 2 Step 3).

- [ ] **Step 2: Implement `drawDockSpace()`**

Add after the render methods:

```cpp
void Editor::drawDockSpace() {
    // Full-window dockspace
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                  ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("##DockSpaceHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");

    // Set default layout on first launch
    if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID dockMain = dockspaceId;
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.17f, nullptr, &dockMain);
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.23f, nullptr, &dockMain);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.25f, nullptr, &dockMain);

        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Scene", dockMain);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);
        ImGui::DockBuilderDockWindow("Project", dockBottom);
        ImGui::DockBuilderDockWindow("Console", dockBottom);
        ImGui::DockBuilderDockWindow("Log", dockBottom);
        ImGui::DockBuilderDockWindow("Debug Info", dockBottom);
        ImGui::DockBuilderDockWindow("Tile Palette", dockRight);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();
}
```

- [ ] **Step 3: Implement `drawSceneViewport()`**

```cpp
void Editor::drawSceneViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("Scene")) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        viewportSize_ = {avail.x, avail.y};

        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        viewportPos_ = {cursorPos.x, cursorPos.y};
        viewportHovered_ = ImGui::IsWindowHovered();

        // Resize FBO to match viewport panel
        int fbW = (int)avail.x;
        int fbH = (int)avail.y;
        if (fbW > 0 && fbH > 0) {
            viewportFbo_.resize(fbW, fbH);

            if (viewportFbo_.isValid()) {
                // Display FBO texture with flipped UV (OpenGL origin is bottom-left)
                ImGui::Image(
                    (ImTextureID)(intptr_t)viewportFbo_.textureId(),
                    avail,
                    ImVec2(0, 1), ImVec2(1, 0)  // UV flip
                );
            }
        }
    } else {
        viewportSize_ = {0, 0};
        viewportHovered_ = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
```

- [ ] **Step 4: Implement `drawDebugInfoPanel()`**

```cpp
void Editor::drawDebugInfoPanel(World* world) {
    if (ImGui::Begin("Debug Info")) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("FPS: %.1f (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
        ImGui::Separator();

        if (world) {
            ImGui::Text("Entities: %zu", world->entityCount());
        }

        ImGui::Separator();
        ImGui::Text("Viewport: %dx%d", (int)viewportSize_.x, (int)viewportSize_.y);
        ImGui::Text("FBO: %dx%d", viewportFbo_.width(), viewportFbo_.height());

        // Camera info — camera pointer is passed through renderUI, but we can
        // access it from the App if needed. For now, display what we have.
        // The implementer should wire camera_ through or add an accessor.
        ImGui::Separator();
        ImGui::Text("Paused: %s", paused_ ? "Yes" : "No");
        ImGui::Text("Tool: %s", currentTool_ == EditorTool::Move ? "Move" :
                                 currentTool_ == EditorTool::Resize ? "Resize" :
                                 currentTool_ == EditorTool::Paint ? "Paint" : "Erase");
    }
    ImGui::End();
}
```

- [ ] **Step 5: Modify `drawToolbar()` — add Grid + Collision Debug toggles**

In `drawToolbar()` (around line 1452-1458), replace the existing grid checkbox section:

Current code (lines 1452-1458):
```cpp
        // Grid + Layer toggles
        ImGui::Checkbox("Grid", &gridSnap_);
        ImGui::SameLine();
        ImGui::Checkbox("Gnd", &showGroundLayer_);
        ImGui::SameLine();
        ImGui::Checkbox("Obj", &showObstacleLayer_);
```

Replace with:
```cpp
        // Grid + Debug toggles
        ImGui::Checkbox("Grid", &showGrid_);
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &gridSnap_);
        ImGui::SameLine();
        ImGui::Checkbox("Colliders", &showCollisionDebug_);
        ImGui::SameLine();
        ImGui::Checkbox("Gnd", &showGroundLayer_);
        ImGui::SameLine();
        ImGui::Checkbox("Obj", &showObstacleLayer_);
```

- [ ] **Step 6: Remove fixed window positions from all panels**

Remove or comment out every `ImGui::SetNextWindowPos(...)` and `ImGui::SetNextWindowSize(...)` call that uses `ImGuiCond_FirstUseEver` from these methods, since DockBuilder now handles positioning:

- `drawHierarchy()` (line 1477-1478): Remove both `SetNextWindowPos` and `SetNextWindowSize`
- `drawInspector()` (line 1593-1594): Remove both
- `drawAssetBrowser()` (line 436-437): Remove both
- `drawTilePalette()` (lines 771-773): Remove `SetNextWindowPos`, `SetNextWindowSize`, and `SetNextWindowCollapsed`
- `drawToolbar()` (lines 1317-1318): Remove `SetNextWindowPos` and `SetNextWindowSize` — toolbar now lives inside the DockSpace menu bar or as a fixed toolbar area

For `drawToolbar()`, change the window flags to allow docking:
```cpp
    // Remove the fixed positioning (lines 1317-1320)
    // The toolbar should now be a standard window
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse;
```

- [ ] **Step 7: Update `shutdown()` to destroy FBO**

In `Editor::shutdown()` (line 64-68), add `viewportFbo_.destroy();` before the ImGui shutdown calls:

```cpp
void Editor::shutdown() {
    viewportFbo_.destroy();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}
```

- [ ] **Step 8: Build to verify**

```bash
"$CMAKE" --build build --config Debug
```
Expected: Compiles. May have linker errors if old `render()` is still referenced from `app.cpp` — that's addressed in Task 4.

- [ ] **Step 9: Commit (includes Task 2 header changes)**

```bash
git add engine/editor/editor.h engine/editor/editor.cpp
git commit -m "Implement DockSpace, viewport panel, and split render methods"
```

---

## Chunk 3: App Render Loop + Input Routing

### Task 4: Restructure App::render() for FBO path

**Files:**
- Modify: `engine/app.cpp` (lines 293-305: `render()`)
- Modify: `engine/app.h` (minor: add include)

- [ ] **Step 1: Update `App::render()` in `engine/app.cpp`**

Replace lines 293-305 with:

```cpp
void App::render() {
    auto* scene = SceneManager::instance().currentScene();
    World* world = scene ? &scene->world() : nullptr;
    auto& editor = Editor::instance();

    if (editor.isOpen()) {
        // --- FBO path: render game into viewport framebuffer ---
        auto& fbo = editor.viewportFbo();
        Vec2 vpSize = editor.viewportSize();
        int fbW = (int)vpSize.x;
        int fbH = (int)vpSize.y;

        if (fbW > 0 && fbH > 0 && fbo.isValid()) {
            fbo.bind();
            glClear(GL_COLOR_BUFFER_BIT);

            onRender(spriteBatch_, camera_);

            // In-viewport overlays (grid, selection highlights)
            editor.renderScene(&spriteBatch_, &camera_);

            // IMPORTANT: All SpriteBatch begin/end pairs must complete before FBO unbind.
            // renderScene's sub-functions (drawSceneGrid) manage their own begin/end.
            // onRender() must also ensure its SpriteBatch calls are flushed before returning.

            fbo.unbind();
        }

        // Restore window viewport
        glViewport(0, 0, config_.windowWidth, config_.windowHeight);
        glClearColor(0.12f, 0.12f, 0.15f, 1.0f);  // dark editor background
        glClear(GL_COLOR_BUFFER_BIT);

        // Editor UI (DockSpace + all panels + viewport showing FBO texture)
        editor.renderUI(world, &camera_, &spriteBatch_);

        // Restore game clear color
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    } else {
        // --- Direct path: render game to screen (no FBO overhead) ---
        glClear(GL_COLOR_BUFFER_BIT);
        onRender(spriteBatch_, camera_);

        // HUD-only overlay (tile coords)
        editor.renderUI(world, &camera_, &spriteBatch_);
    }

    SDL_GL_SwapWindow(window_);
}
```

- [ ] **Step 2: Build and verify**

```bash
"$CMAKE" --build build --config Debug
```
Expected: Compiles cleanly. The old `Editor::render()` call is gone.

- [ ] **Step 3: Commit**

```bash
git add engine/app.cpp engine/app.h
git commit -m "Restructure render loop for FBO viewport path"
```

### Task 5: Rework input routing through viewport

**Files:**
- Modify: `engine/app.cpp` (lines 113-258: `processEvents()`)

This task replaces all `!Editor::instance().wantsMouse()` checks with viewport-aware routing, remaps coordinates to viewport-local space, and removes F3 auto-pause.

- [ ] **Step 1: Remove F3 auto-pause**

In `processEvents()`, lines 155-158, change:

```cpp
                if (event.key.keysym.scancode == SDL_SCANCODE_F3) {
                    Editor::instance().toggle();
                    Editor::instance().setPaused(Editor::instance().isOpen());
                    LOG_INFO("App", "Editor: %s", Editor::instance().isOpen() ? "OPEN (paused)" : "CLOSED (resumed)");
                }
```

To:

```cpp
                if (event.key.keysym.scancode == SDL_SCANCODE_F3) {
                    Editor::instance().toggle();
                    LOG_INFO("App", "Editor: %s", Editor::instance().isOpen() ? "OPEN" : "CLOSED");
                }
```

- [ ] **Step 2: Fix keyboard routing for play mode**

In `processEvents()`, line 123, change the input gating from:

```cpp
        if (!Editor::instance().wantsInput()) {
```

To:

```cpp
        // Keyboard routing: when editor is open and playing, let game keys through
        // unless an ImGui text field has focus. Mouse routing is handled per-event below.
        bool editorWantsKeyboard = Editor::instance().isOpen() &&
            Editor::instance().wantsKeyboard() && Editor::instance().isPaused();
        if (!editorWantsKeyboard) {
```

This only gates keyboard input — mouse routing is handled per-event via `isViewportHovered()` checks below. When playing with editor open, game keys (WASD) go through unless an ImGui text field is active.

- [ ] **Step 3: Replace mouse routing — scroll wheel zoom**

Lines 177-186 (SDL_MOUSEWHEEL). Change:

```cpp
                if (Editor::instance().isOpen() && !Editor::instance().wantsMouse()) {
```

To:

```cpp
                if (Editor::instance().isOpen() && Editor::instance().isViewportHovered()) {
```

- [ ] **Step 4: Replace mouse routing — left click**

Lines 189-210 (SDL_MOUSEBUTTONDOWN). Replace the condition and coordinate mapping:

```cpp
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT &&
                    Editor::instance().isOpen() &&
                    Editor::instance().isViewportHovered() &&
                    Editor::instance().isPaused()) {
                    auto* scene = SceneManager::instance().currentScene();
                    if (scene) {
                        // Map to viewport-local coordinates
                        Vec2 vpPos = Editor::instance().viewportPos();
                        Vec2 vpSize = Editor::instance().viewportSize();
                        Vec2 screenPos = {
                            (float)event.button.x - vpPos.x,
                            (float)event.button.y - vpPos.y
                        };
                        int vpW = (int)vpSize.x;
                        int vpH = (int)vpSize.y;

                        if (Editor::instance().isTilePaintMode()) {
                            Editor::instance().paintTileAt(
                                &scene->world(), &camera_, screenPos, vpW, vpH);
                        } else if (Editor::instance().isEraseMode()) {
                            Editor::instance().eraseTileAt(
                                &scene->world(), &camera_, screenPos, vpW, vpH);
                        } else {
                            Editor::instance().handleSceneClick(
                                &scene->world(), &camera_, screenPos, vpW, vpH);
                        }
                    }
                }
                break;
```

- [ ] **Step 5: Replace mouse routing — mouse motion (drag)**

Lines 213-250 (SDL_MOUSEMOTION). Replace with viewport-aware version:

```cpp
            case SDL_MOUSEMOTION:
                if (Editor::instance().isOpen() && Editor::instance().isViewportHovered()) {
                    Vec2 vpPos = Editor::instance().viewportPos();
                    Vec2 vpSize = Editor::instance().viewportSize();
                    Vec2 localPos = {
                        (float)event.motion.x - vpPos.x,
                        (float)event.motion.y - vpPos.y
                    };
                    int vpW = (int)vpSize.x;
                    int vpH = (int)vpSize.y;

                    // Right-click drag: pan camera
                    if (event.motion.state & SDL_BUTTON_RMASK) {
                        float scaleX = Camera::VIRTUAL_WIDTH / (float)vpW / camera_.zoom();
                        float scaleY = Camera::VIRTUAL_HEIGHT / (float)vpH / camera_.zoom();
                        Vec2 panDelta = {
                            -(float)event.motion.xrel * scaleX,
                            (float)event.motion.yrel * scaleY
                        };
                        camera_.setPosition(camera_.position() + panDelta);
                    }
                    // Left-click drag: paint tiles or move entity (only when paused/editing)
                    else if ((event.motion.state & SDL_BUTTON_LMASK) && Editor::instance().isPaused()) {
                        if (Editor::instance().isTilePaintMode()) {
                            auto* scene = SceneManager::instance().currentScene();
                            if (scene) {
                                Editor::instance().paintTileAt(
                                    &scene->world(), &camera_, localPos, vpW, vpH);
                            }
                        } else if (Editor::instance().isEraseMode()) {
                            auto* scene = SceneManager::instance().currentScene();
                            if (scene) {
                                Editor::instance().eraseTileAt(
                                    &scene->world(), &camera_, localPos, vpW, vpH);
                            }
                        } else {
                            Editor::instance().handleSceneDrag(
                                &camera_, localPos, vpW, vpH);
                        }
                    }
                }
                break;
```

- [ ] **Step 6: Update window resize handler**

In the `SDL_WINDOWEVENT_RESIZED` handler (lines 147-150), keep the window viewport update but note that FBO viewport is managed separately by `drawSceneViewport()`. No change needed here — the FBO auto-resizes each frame.

- [ ] **Step 7: Build and verify**

```bash
"$CMAKE" --build build --config Debug
```

- [ ] **Step 8: Commit**

```bash
git add engine/app.cpp
git commit -m "Rework input routing through viewport panel with coordinate mapping"
```

---

## Chunk 4: Final Integration + Build Verification

### Task 6: Wire up collision debug flag

**Files:**
- Modify: `engine/editor/editor.h` (already done in Task 2)

The `showCollisionDebug_` flag is now on the Editor. Game code (in `game_app.cpp`, which is gitignored/proprietary) reads it via `Editor::instance().showCollisionDebug()`. The game-side change is:

Replace any `showCollisionDebug_` member variable in GameApp with `Editor::instance().showCollisionDebug()`.

Remove the F2 keybind for collision debug from game_app.cpp and rely on the editor toolbar toggle.

Since `game_app.cpp` is gitignored, this is a manual note for the user.

- [ ] **Step 1: Document game-side change needed**

The user must update `game/game_app.cpp` to:
1. Replace `showCollisionDebug_` references with `Editor::instance().showCollisionDebug()`
2. Remove F2 toggle keybind (now handled by editor toolbar)
3. When editor is open and paused, skip game HUD ImGui calls (`HudBarsUI::draw()`, `SkillBarUI::draw()`, `InventoryUI::draw()`)

### Task 7: Full build and smoke test

- [ ] **Step 1: Full rebuild**

```bash
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build build --config Debug
```

- [ ] **Step 2: Run and verify**

Launch the game. Verify:
1. Game runs normally with editor closed (no FBO, no ImGui overhead beyond tile coord HUD)
2. Press F3 — editor opens with DockSpace layout
3. Viewport panel shows the game scene rendered via FBO
4. Panels (Hierarchy, Inspector, Asset Browser) are docked and rearrangeable
5. Click and drag panels to rearrange — DockSpace works
6. Grid toggle in toolbar shows/hides grid in viewport
7. Collision Debug toggle works
8. Play/Pause button controls game logic
9. Click in viewport to select entities (when paused)
10. Right-drag in viewport to pan camera
11. Scroll wheel in viewport to zoom
12. Close editor with F3 — game renders full-screen again

- [ ] **Step 3: Final commit**

```bash
git add -A
git commit -m "Complete editor viewport separation: DockSpace + FBO viewport"
```
