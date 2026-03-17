# Editor Viewport Separation Design

**Date:** 2026-03-16
**Status:** Draft
**Goal:** Restructure the editor so the game scene renders into an FBO-backed ImGui viewport panel, with all editor panels (hierarchy, inspector, debug info) living outside the viewport in a Unity-style dockable layout.

## Context

Currently the editor renders ImGui panels as floating overlays on top of the full-window game view. The game owns the entire window, and editor UI is "baked into" the scene. This makes the editor feel like a debug overlay rather than a proper development tool.

## Requirements

1. **FBO Viewport** — Game renders to a framebuffer object, displayed as a texture inside an ImGui panel
2. **ImGui DockSpace** — Full docking layout. Panels are draggable, tabbable, rearrangeable (Unity-style)
3. **Always-live rendering** — Scene always renders in the viewport. Gameplay logic paused/unpaused via editor toggle
4. **Input routing** — Paused (editing): editor tools own viewport clicks. Playing: game gets input
5. **Tile coordinate HUD** — Stays in-viewport (game feature, always visible to players)
6. **Collision debug + Grid** — Editor toolbar toggles (not in-game hotkeys)
7. **Debug info panel** — FPS, entity count, player stats in a dockable editor panel
8. **Viewport tabs** — Support for tabbing (future Scene-only tab possible)
9. **No FBO overhead when editor closed** — Game renders directly to screen during normal gameplay

## Architecture

### Render Pipeline

**Editor closed (gameplay):**
```
glClear
  -> onRender(batch, camera)        [game draws to screen]
  -> drawHUD (tile coords overlay)  [ImGui overlay, always visible]
  -> ImGui::Render()
  -> SwapWindow
```

**Editor open:**
```
Bind FBO
  -> glViewport(FBO size)
  -> glClear
  -> onRender(batch, camera)        [game draws into FBO]
  -> Editor::renderScene(batch, camera)
       -> drawSceneGrid (if toggled) [grid via SpriteBatch into FBO]
       -> drawTileCoordHUD (via SpriteBatch/TextRenderer into FBO)
  -> SpriteBatch::end() / flush     [clean GL state boundary]
Unbind FBO

glViewport(window size)
glClear (editor background color)
Editor::renderUI(world)
  -> ImGui DockSpace (full window)
  -> drawMenuBar()
  -> drawToolbar()
  -> drawSceneViewport()            [ImGui::Image(FBO texture, UV flipped: (0,1)→(0,0))]
  -> drawHierarchy()
  -> drawInspector()
  -> drawAssetBrowser()
  -> drawTilePalette()
  -> drawConsole()
  -> drawLogViewer()
  -> drawDebugInfoPanel()           [FPS, entity count, stats]
  -> ImGui::Render()
SwapWindow
```

**Critical detail — Editor::render() splits into two methods:**
- `renderScene(batch, camera)` — called while FBO is bound. Draws grid, selection highlights, and tile coord HUD via SpriteBatch (NOT ImGui, since ImGui defers all rendering to `ImGui::Render()` which happens after FBO unbind).
- `renderUI(world)` — called after FBO is unbound. Draws DockSpace, all ImGui panels, and the viewport panel showing the FBO texture.

**Critical detail — Tile coordinate HUD rendering path:**
- **Editor open:** Rendered via SpriteBatch into the FBO (not ImGui). This ensures it is truly "in-viewport" and clipped to the viewport panel. The engine already has text rendering capability via SpriteBatch.
- **Editor closed:** Rendered as an ImGui overlay on the bare game window (current behavior).

**Critical detail — GL state boundary:**
SpriteBatch must fully flush (`end()`) before the FBO is unbound, so that all draw calls target the FBO. After unbinding, ImGui's `ImGui_ImplOpenGL3_RenderDrawData()` saves/restores GL state, so no conflicts arise.

### New Class: `Framebuffer`

**File:** `engine/render/framebuffer.h` / `framebuffer.cpp`

```cpp
class Framebuffer {
public:
    bool create(int width, int height);
    void destroy();
    void resize(int width, int height);  // recreates if dimensions changed
    void bind();
    void unbind();

    unsigned int textureId() const;
    int width() const;
    int height() const;

private:
    unsigned int fbo_ = 0;
    unsigned int texture_ = 0;
    int width_ = 0;
    int height_ = 0;
};
```

- Color texture attachment only (no depth/stencil — 2D engine; SpriteBatch handles depth sorting in software via `SpriteDrawParams::depth`)
- Destructor does NOT auto-cleanup (matching `SpriteBatch::init()`/`shutdown()` pattern). Caller must call `destroy()` explicitly.
- **Texture filtering: `GL_NEAREST` for both min and mag** — required for pixel art; `GL_LINEAR` would blur sprites
- `resize()` compares dimensions and only recreates if they changed. Truncates ImGui float dimensions to `int` via floor.
- `unbind()` restores binding to default framebuffer (0)
- FBO resolution matches the ImGui viewport panel's content region size

### DockSpace Layout

Default layout set via `ImGui::DockBuilder` on first launch:

```
+---------------------------------------------------------------+
| Menu Bar                                                       |
+---------------------------------------------------------------+
| Toolbar [Save|Load|Play/Pause|Undo|Redo|Move|Resize|Paint|    |
|          Erase|Grid Toggle|Collision Toggle]                   |
+-----------+-----------------------------------+----------------+
| HIERARCHY | SCENE VIEWPORT                    | INSPECTOR      |
| (220px)   | (FBO texture via ImGui::Image)    | (300px)        |
|           |                                   |                |
| - World   |   +-------------------------+    | Transform      |
|   - Player|   |                         |    |  X: 128        |
|   - Enemy |   |  Game renders here      |    |  Y: 256        |
|   - ...   |   |                         |    | Sprite         |
|           |   +-------------------------+    |  player.png    |
+-----------+-----------------------------------+----------------+
| ASSET BROWSER | CONSOLE | LOG | DEBUG INFO     (tabbed, 200px) |
+---------------------------------------------------------------+
```

All panels are dockable windows. User arrangement persists via `imgui.ini`.

**DockBuilder implementation notes:**
- Use `ImGui::DockSpace(dockspace_id)` with an explicit ID, NOT `DockSpaceOverViewport` with `PassthruCentralNode`
- Check `ImGui::DockBuilderGetNode(dockspace_id) == nullptr` to detect first launch and run `DockBuilderSplitNode`/`DockBuilderDockWindow` to set default layout
- Panel names in `DockBuilderDockWindow` must exactly match the titles used in `ImGui::Begin()`

### Input Routing

When the editor is open, mouse interaction in the viewport panel works as follows:

1. Editor tracks `viewportPos_`, `viewportSize_`, `viewportHovered_` from the ImGui viewport panel's content region each frame (set inside `drawSceneViewport()` via `ImGui::GetCursorScreenPos()`, `ImGui::GetContentRegionAvail()`, `ImGui::IsWindowHovered()`)
2. On mouse events, `App::processEvents()` checks `viewportHovered_` instead of `!wantsMouse()`. This is necessary because the viewport is itself an ImGui window, so `io.WantCaptureMouse` will be true even when the mouse is over the viewport content. All six existing `!wantsMouse()` checks in `App::processEvents()` must be replaced with `viewportHovered_`.
3. Screen coordinates are mapped to viewport-local coordinates:
   ```
   localX = mouseX - viewportPos_.x
   localY = mouseY - viewportPos_.y
   ```
4. These local coordinates are passed to `Camera::screenToWorld()` using `viewportSize_` as the window dimensions (not the actual window size)
5. This mapping applies to: click/select, drag/move, tile paint, tile erase, camera pan, camera zoom
6. **Camera pan formula must change when editor is open:**
   ```cpp
   // Current:
   float scaleX = Camera::VIRTUAL_WIDTH / (float)config_.windowWidth / camera_.zoom();
   // When editor open, use viewport size:
   float scaleX = Camera::VIRTUAL_WIDTH / (float)editor.viewportSize().x / camera_.zoom();
   ```

**Play/Pause behavior:**
- **Paused (editing):** Viewport clicks → editor tools (select, drag, paint, erase). Game logic does not run. Keyboard goes to editor shortcuts.
- **Playing:** Viewport clicks → game input. Game logic runs. Keyboard (WASD, etc.) routes to game UNLESS an ImGui text field has focus (`io.WantCaptureKeyboard`). Editor panels remain visible and responsive but don't interact with the viewport.

**Keyboard routing when editor is open:**
The existing `wantsInput()` gate at `App::processEvents()` line 123 uses `io.WantCaptureKeyboard`. When playing with editor open, this would block game keys because the viewport is an ImGui window. Fix: when `!isPaused()` and no ImGui text field is actively edited, bypass the keyboard capture gate and route keys to the game. The check becomes: `wantsKeyboard_ && isPaused()` instead of just `wantsKeyboard_`.

### Toolbar Changes

The toolbar gains three new toggle buttons:

| Button | Current Behavior | New Behavior |
|--------|-----------------|--------------|
| **Play/Pause** | F3 toggles editor + auto-pauses | Toolbar button inside editor. Controls `paused_` independently of editor open/close. |
| **Collision Debug** | F2 keyboard toggle (game-side) | Toolbar toggle button. Editor sets a flag, game reads it during render. |
| **Grid** | Always on when editor is open + grid snap enabled | Toolbar toggle button. Independent of grid snap. |

**F3 behavior change:** F3 still toggles the editor open/close, but no longer auto-pauses gameplay. The auto-pause line (`Editor::instance().setPaused(Editor::instance().isOpen())`) in `App::processEvents()` must be removed. Pause is now controlled exclusively via the toolbar Play/Pause button.

### Debug Info Panel

A new dockable panel (`drawDebugInfoPanel()`) that displays:
- FPS and frame time
- Entity count
- Draw calls / sprite count
- Player stats (if available)
- Camera position and zoom

This replaces the `renderEditorDebugPanel()` that currently exists in `game_app.cpp`.

## Files Affected

| File | Change Type | Description |
|------|-------------|-------------|
| `engine/render/framebuffer.h` | **NEW** | Framebuffer class header |
| `engine/render/framebuffer.cpp` | **NEW** | Framebuffer implementation |
| `engine/editor/editor.h` | **MODIFY** | Add Framebuffer member, viewport state, new methods (`renderScene`, `renderUI`), toolbar toggle state (`showCollisionDebug_`, `showGrid_`) |
| `engine/editor/editor.cpp` | **MODIFY** | Split `render()` into `renderScene()`+`renderUI()`, DockSpace setup, viewport panel with UV-flipped `ImGui::Image`, toolbar toggles, input coord mapping, debug info panel, SpriteBatch-based tile coord HUD |
| `engine/app.h` | **MODIFY** | Expose viewport info accessors for input routing |
| `engine/app.cpp` | **MODIFY** | Restructure `render()` for FBO path, replace `!wantsMouse()` with `viewportHovered_`, remap input coords through viewport, remove F3 auto-pause |
| `game/game_app.cpp` | **MODIFY** | Move `showCollisionDebug_` flag to editor, move `renderEditorDebugPanel()` logic to `Editor::drawDebugInfoPanel()` |
| `game/game_app.h` | **MODIFY** | Remove `showCollisionDebug_`, `renderEditorDebugPanel()` declarations |
| `CMakeLists.txt` | **MODIFY** | Add `engine/render/framebuffer.cpp` to build |

## Edge Cases

- **Window resize:** FBO resizes to match viewport panel's content region on each frame (via `resize()` which no-ops if dimensions haven't changed)
- **Editor first open:** DockBuilder sets default layout. Subsequent opens restore from `imgui.ini`
- **Viewport aspect ratio:** The FBO fills the viewport panel's available space. Camera's virtual resolution (480x270) is independent of pixel resolution, so aspect ratio is always correct
- **Zero-size viewport:** If a panel covers the entire viewport (e.g. during rearrangement), skip FBO render that frame
- **SpriteBatch interaction:** SpriteBatch uses Camera's view-projection matrix (virtual coordinates). Changing the FBO pixel resolution only affects output resolution, not coordinate math
- **ImGui::Image UV flip:** OpenGL textures have (0,0) at bottom-left, ImGui expects top-left. `drawSceneViewport()` must use flipped UV: `ImVec2(0,1)` to `ImVec2(1,0)`
- **Game-side ImGui rendering:** When the editor is open, game HUD elements (HudBarsUI, SkillBarUI, InventoryUI) are suppressed entirely. The game is paused, so there is no reason to display gameplay UI. When playing with editor open, these panels appear as floating ImGui windows over the editor chrome (outside the viewport). The game's `onRender()` must check `Editor::instance().isPaused()` and skip ImGui HUD calls when paused.
- **drawSceneGrid begin/end ownership:** `drawSceneGrid()` keeps its existing pattern of calling `batch->begin()`/`batch->end()` internally as a self-contained unit. Each in-viewport rendering sub-function manages its own batch lifecycle. The "flush" in the pipeline diagram means "ensure no batch is still open before FBO unbind," not that a wrapping begin/end spans all calls.
- **Zero-size FBO guard:** `Framebuffer::resize()` refuses to create a zero-size FBO (returns early / no-ops). `glTexImage2D` with width=0 or height=0 is undefined behavior on some drivers. `App::render()` skips the FBO render path when `viewportSize_` is zero.

## What Does NOT Change

- Camera class — virtual resolution and coordinate math unchanged
- SpriteBatch class — draws into whatever FBO/screen is bound, no changes needed
- Scene/World/ECS — completely unaffected
- Tilemap rendering — draws via SpriteBatch, unaffected
- Game logic — only the render target changes, not game systems
