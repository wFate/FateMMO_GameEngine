#pragma once
#include <functional>
#include <set>
#include <utility>
#include "engine/ecs/world.h"
#include "engine/render/layout_class.h"
#include "engine/ecs/entity.h"
#include "engine/ecs/entity_handle.h"
#include "engine/render/camera.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/texture.h"
#include "engine/render/framebuffer.h"
#include "engine/render/shader.h"
#include "engine/render/post_process.h"
#if defined(ENGINE_MEMORY_DEBUG)
#include "engine/editor/memory_panel.h"
#include <implot.h>
#endif
#include "engine/editor/tile_tools.h"
#include "engine/components/tile_layer_component.h"
#include "engine/editor/undo.h"
#include "engine/editor/node_editor.h"
#include "engine/editor/animation_editor.h"
#include "engine/editor/combat_text_editor.h"
#include "engine/editor/asset_browser.h"
#ifdef FATE_HAS_GAME
#include "engine/editor/ui_editor_panel.h"
#else
namespace fate { class UIManager; }
#endif
#include "engine/editor/paper_doll_panel.h"
#include "engine/editor/content_browser_panel.h"
#include <ImGuizmo.h>
#include <SDL.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <set>
#include <memory>

namespace fate {

enum class AssetType { Sprite, Script, Scene, Shader, Other };

struct AssetEntry {
    std::string name;
    std::string fullPath;
    std::string relativePath;
    AssetType type = AssetType::Other;
    std::shared_ptr<Texture> thumbnail;
};

// Device profile for play-testing (resolution + safe area insets)
struct DeviceProfile {
    const char* name;
    const char* category;     // "Apple iPhone", "Apple iPad", "Android", "Desktop"
    int    width;             // native pixels
    int    height;
    float  scaleFactor;       // Retina/DPI scale
    float  safeTop;
    float  safeBottom;
    float  safeLeft;
    float  safeRight;
    bool   hasNotch;
    bool   hasDynamicIsland;
    // Layout class for variant JSON selection. Editor switches loaded screens
    // when this changes. See engine/render/layout_class.h for the cutoffs and
    // file-naming convention (foo.tablet.json / foo.compact.json fall back to
    // foo.json when missing).
    //   0 = Base (modern phones / ultrawide)
    //   1 = Compact (iPhone SE / 16:9 desktop)
    //   2 = Tablet (iPads)
    // Free Aspect uses Base — runtime classifies by real viewport aspect.
    int layoutClass;
};

// Safe area insets are LANDSCAPE values (game runs landscape).
// In landscape: Dynamic Island/notch moves to LEFT side, home indicator at BOTTOM (reduced).
//                                                                      top bot left right                       lyt
static constexpr DeviceProfile kDeviceProfiles[] = {
    // Apple iPhone
    {"iPhone SE (3rd gen)",  "Apple iPhone", 1334,  750, 2.0f,  0,  0,  0,  0, false, false, /*Compact*/1},
    {"iPhone 14",            "Apple iPhone", 2532, 1170, 3.0f,  0, 21, 47,  0, true,  false, /*Base*/0},
    {"iPhone 14 Plus",       "Apple iPhone", 2778, 1284, 3.0f,  0, 21, 47,  0, true,  false, /*Base*/0},
    {"iPhone 14 Pro",        "Apple iPhone", 2556, 1179, 3.0f,  0, 21, 59,  0, false, true,  /*Base*/0},
    {"iPhone 14 Pro Max",    "Apple iPhone", 2796, 1290, 3.0f,  0, 21, 59,  0, false, true,  /*Base*/0},
    {"iPhone 15",            "Apple iPhone", 2556, 1179, 3.0f,  0, 21, 59,  0, false, true,  /*Base*/0},
    {"iPhone 15 Plus",       "Apple iPhone", 2796, 1290, 3.0f,  0, 21, 59,  0, false, true,  /*Base*/0},
    {"iPhone 15 Pro",        "Apple iPhone", 2556, 1179, 3.0f,  0, 21, 59,  0, false, true,  /*Base*/0},
    {"iPhone 15 Pro Max",    "Apple iPhone", 2796, 1290, 3.0f,  0, 21, 59,  0, false, true,  /*Base*/0},
    {"iPhone 16",            "Apple iPhone", 2556, 1179, 3.0f,  0, 21, 59,  0, false, true,  /*Base*/0},
    {"iPhone 16 Plus",       "Apple iPhone", 2796, 1290, 3.0f,  0, 21, 59,  0, false, true,  /*Base*/0},
    {"iPhone 16 Pro",        "Apple iPhone", 2622, 1206, 3.0f,  0, 21, 59,  0, false, true,  /*Base*/0},
    {"iPhone 16 Pro Max",    "Apple iPhone", 2868, 1320, 3.0f,  0, 21, 59,  0, false, true,  /*Base*/0},
    {"iPhone 17 Pro",        "Apple iPhone", 2622, 1206, 3.0f,  0, 21, 59,  0, false, true,  /*Base*/0},
    // Apple iPad (fullscreen landscape games: no insets)
    {"iPad (10th gen)",      "Apple iPad",   2360, 1640, 2.0f,  0,  0,  0,  0, false, false, /*Tablet*/2},
    {"iPad Air (M3)",        "Apple iPad",   2360, 1640, 2.0f,  0,  0,  0,  0, false, false, /*Tablet*/2},
    {"iPad Pro 11\" (M4)",   "Apple iPad",   2420, 1668, 2.0f,  0,  0,  0,  0, false, false, /*Tablet*/2},
    {"iPad Pro 13\" (M4)",   "Apple iPad",   2752, 2064, 2.0f,  0,  0,  0,  0, false, false, /*Tablet*/2},
    // Android (landscape: status bar hidden, nav bar at bottom)
    {"Pixel 9",              "Android",      2424, 1080, 2.6f,  0, 24,  0,  0, false, false, /*Base*/0},
    {"Pixel 9 Pro",          "Android",      2856, 1280, 2.8f,  0, 24,  0,  0, false, false, /*Base*/0},
    {"Samsung S24",          "Android",      2340, 1080, 3.0f,  0, 24,  0,  0, false, false, /*Base*/0},
    {"Samsung S24 Ultra",    "Android",      3120, 1440, 3.0f,  0, 24,  0,  0, false, false, /*Base*/0},
    {"Samsung S25",          "Android",      2340, 1080, 3.0f,  0, 24,  0,  0, false, false, /*Base*/0},
    // Desktop
    {"720p",                 "Desktop",      1280,  720, 1.0f,  0,  0,  0,  0, false, false, /*Compact*/1},
    {"1080p",                "Desktop",      1920, 1080, 1.0f,  0,  0,  0,  0, false, false, /*Compact*/1},
    {"1440p",                "Desktop",      2560, 1440, 1.0f,  0,  0,  0,  0, false, false, /*Compact*/1},
    {"4K",                   "Desktop",      3840, 2160, 1.0f,  0,  0,  0,  0, false, false, /*Compact*/1},
    {"Ultrawide 1080p",      "Desktop",      2560, 1080, 1.0f,  0,  0,  0,  0, false, false, /*Base*/0},
    // Free Aspect
    {"Free Aspect",          "Free",            0,    0, 1.0f,  0,  0,  0,  0, false, false, /*Base*/0},
};
static constexpr int kDeviceProfileCount = sizeof(kDeviceProfiles) / sizeof(kDeviceProfiles[0]);
static constexpr int kDefaultDeviceIdx = 13; // iPhone 17 Pro

// Editor tool modes (like Unity W/E/R)
enum class EditorTool {
    Move,      // W - drag to move entities
    Scale,     // E - drag handles to scale/resize
    Rotate,    // R - rotate entity
    Paint,     // B - tile painting mode
    Erase,     // X - click to delete ground tiles
    Fill,      // G - flood fill
    RectFill,  // U - rectangle fill
    LineTool   // L - line tool
};

class Editor {
public:
    static Editor& instance() {
        static Editor s_instance;
        return s_instance;
    }

#ifdef FATEMMO_METAL
    bool init(SDL_Window* window, void* metalLayer);
#else
    bool init(SDL_Window* window, SDL_GLContext glContext);
#endif
    void shutdown();

    void processEvent(const SDL_Event& event);
    void beginFrame();
    // Split render into scene (FBO-bound) and UI (ImGui panels)
    void renderScene(SpriteBatch* batch, Camera* camera);
    void renderUI(World* world, Camera* camera, SpriteBatch* batch, FrameArena* frameArena = nullptr);

    // Viewport info (for input routing in App)
    Vec2 viewportPos() const { return viewportPos_; }
    Vec2 viewportSize() const { return viewportSize_; }
    bool isViewportHovered() const { return viewportHovered_; }

    // Toolbar toggle accessors
    bool showCollisionDebug() const { return showCollisionDebug_; }
    bool showSpawnDebug() const { return showSpawnDebug_; }
    bool showGrid() const { return showGrid_; }
    std::string currentSceneId() const;
    bool showGameUI() const { return showGameUI_; }

    // FBO management
    Framebuffer& viewportFbo() { return viewportFbo_; }

    void handleSceneClick(World* world, Camera* camera, const Vec2& screenPos,
                          int windowWidth, int windowHeight);
    void handleSceneDrag(Camera* camera, const Vec2& screenPos,
                         int windowWidth, int windowHeight);
    void handleMouseUp();
    // Returns true if scroll was consumed by spawn zone resize (caller should skip camera zoom)
    bool handleSpawnZoneScroll(float scrollY);
    void paintTileAt(World* world, Camera* camera, const Vec2& screenPos,
                     int windowWidth, int windowHeight);

    // Demo-side tile-tool dispatchers (defined in editor_tile.cpp). The
    // FATE_HAS_GAME branch in editor.cpp does this dispatch inline because
    // tile clicks compete with selection/drag/spawn-zone/NPC handlers.
    void dispatchTileSceneClick(World* world, Camera* camera, const Vec2& screenPos,
                                int windowWidth, int windowHeight);
    void dispatchTileSceneDrag(Camera* camera, const Vec2& screenPos,
                               int windowWidth, int windowHeight);
    void finishTileMouseUp(World* world);
    void drawTileBrushPreview(SpriteBatch* batch, Camera* camera);

    // Iterates Transform+SpriteComponent and renders enabled sprites — used by
    // the demo build (the FATE_HAS_GAME path uses game/systems/render_system).
    void renderTilemap(SpriteBatch* batch, Camera* camera);

    bool isOpen() const { return open_; }
    void toggle() { open_ = !open_; }
    bool isPaused() const { return paused_; }
    void setPaused(bool p) { paused_ = p; }
    const std::string& currentScenePath() const { return currentScenePath_; }
    void setCurrentScenePath(const std::string& p) { currentScenePath_ = p; }

    // Dirty-domain bookkeeping. Wired to UndoSystem in init() so every push
    // / undo / redo flips the right flag. Ctrl+S reads these to save only
    // the file(s) that actually diverged from disk; each branch clears its
    // own bit only after the corresponding write succeeded.
    void markSceneDirty() { sceneDirty_ = true; }
    void markPlayerPrefabDirty() { playerPrefabDirty_ = true; }
    // UI dirtiness is keyed by (screenId, LayoutClass-at-edit-time). When a
    // user edits Tablet then switches device, the recorded class pins the
    // save target to the variant file that was actually authored — Ctrl+S
    // never writes the in-memory tree to a different class's path.
    void markUIScreenDirty(const std::string& screenId) {
        if (screenId.empty()) return;
        dirtyScreens_.insert({screenId, fate::LayoutClassRegistry::current()});
    }
    bool sceneDirty() const { return sceneDirty_; }
    bool playerPrefabDirty() const { return playerPrefabDirty_; }
    bool isUIScreenDirty(const std::string& screenId) const {
        for (const auto& kv : dirtyScreens_) {
            if (kv.first == screenId) return true;
        }
        return false;
    }
    // Drop only scene-local dirty state (the entity diff for the current
    // scene file). UI screens and the player prefab are cross-scene
    // documents — they live in their own files, not in any scene.json —
    // so their dirty bits MUST survive a scene switch. Without that
    // distinction, editing a UI offset and then opening a different scene
    // would silently discard the UI edit.
    void clearSceneDirty() { sceneDirty_ = false; }
    // Drop the entire UI dirty set. Used by tests and by cleanup paths
    // that have no UIManager to flush against. Production save paths use
    // flushDirtyUIScreens() which clears per-screen on success only.
    void clearUIScreenDirty() { dirtyScreens_.clear(); }
    // Drop the player-prefab dirty bit without saving. Used by tests and
    // by stale-bit recovery when the player entity is gone.
    void clearPlayerPrefabDirty() { playerPrefabDirty_ = false; }
    // Save the dirty player prefab using the currently-alive world before
    // it is destroyed by a scene reload. Returns true if there is nothing
    // to save, the save succeeded, or the dirty bit was stale (player
    // already gone — bit gets dropped). Returns false only on an actual
    // PrefabLibrary::save() failure; loadScene treats that as a hard
    // load-abort so unsaved edits never silently disappear with the world.
    bool flushDirtyPlayerPrefab(World* world);
    // Save every (screenId, class) currently in dirtyScreens_ at its
    // recorded variant path. Wired to UIManager::beforeReloadCallback so a
    // device-class change can't strand pending edits in an in-memory tree
    // that's about to be reloaded. Returns true iff every dirty screen was
    // FULLY persisted (runtime + source). A return of false means at least
    // one screen kept its dirty bit and the in-memory tree must NOT be
    // replaced — the layout-class reload should refuse to proceed.
    bool flushDirtyUIScreens();

    // Called when a scene is loaded during play mode (for spectator integration)
    std::function<void(const std::string& sceneName)> onSceneLoadedInPlayMode;

    // Admin observer mode callbacks
    std::function<void()> onObserveRequested;
    std::function<void()> onObserveStop;
    bool isObserving_ = false;

    // Game-side ImGui panels that need to dock into the editor dockspace must
    // draw AFTER drawDockSpace() so DockBuilder targets resolve on first Begin.
    // GameApp registers its drawNetworkPanel here.
    std::function<void()> onDrawDockedGamePanels;

    // GameApp registers &showNetPanel_ here so View > Network can toggle the
    // panel even when no game-side bool is reachable from the editor menu.
    bool* netPanelOpen_ = nullptr;

    bool wantsInput() const { return open_ && (wantsKeyboard_ || wantsMouse_); }
    bool wantsKeyboard() const { return open_ && wantsKeyboard_; }
    bool wantsMouse() const { return open_ && wantsMouse_; }

    Entity* selectedEntity() const { return selectedEntity_; }
    void clearSelection() { selectedHandle_ = {}; selectedEntity_ = nullptr; isDraggingEntity_ = false; selectedEntities_.clear(); }
    void cancelPlacement() { isDraggingAsset_ = false; draggedAssetPath_.clear(); pendingBrushStroke_.reset(); }
    void refreshSelection(World* world) {
        if (selectedHandle_.isNull()) { selectedEntity_ = nullptr; return; }
        selectedEntity_ = world ? world->getEntity(selectedHandle_) : nullptr;
        if (!selectedEntity_) { selectedHandle_ = {}; selectedEntities_.clear(); }
    }
    bool isTilePaintMode() const {
        if (currentTool_ != EditorTool::Paint &&
            currentTool_ != EditorTool::Fill &&
            currentTool_ != EditorTool::RectFill &&
            currentTool_ != EditorTool::LineTool) return false;
        if (selectedTileLayer_ == "collision") return true;
        return selectedTileIndex_ >= 0;
    }

    static int layerIndex(const std::string& layer) {
        if (layer == "ground")    return 0;
        if (layer == "detail")    return 1;
        if (layer == "fringe")    return 2;
        if (layer == "collision") return 3;
        return 0;
    }
    static float layerBaseDepth(const std::string& layer) {
        if (layer == "ground")    return 0.0f;
        if (layer == "detail")    return 10.0f;
        if (layer == "fringe")    return 100.0f;
        if (layer == "collision") return -1.0f;
        return 0.0f;
    }
    bool isEraseMode() const { return currentTool_ == EditorTool::Erase; }
    bool isScaleMode() const { return currentTool_ == EditorTool::Scale; }

    EditorTool currentTool() const { return currentTool_; }
    // Test-only accessors — do not use in production code.
    void setCurrentTool(EditorTool t) { currentTool_ = t; }
    void setSelectedTileLayer(const std::string& layer) { selectedTileLayer_ = layer; }
    void setSelectedTileIndex(int index) { selectedTileIndex_ = index; }

    bool gridSnapEnabled() const { return gridSnap_; }
    float gridSize() const { return gridSize_; }

    // Returns true on successful write to runtime (and source, if configured).
    // On failure, see lastSaveStatus() / lastSaveSucceeded() for detail.
    bool saveScene(World* world, const std::string& path);
    const std::string& lastSaveStatus() const { return lastSaveStatus_; }
    bool lastSaveSucceeded() const { return lastSaveSucceeded_; }

    // Tile-tool feedback ("no palette texture", "select a tile first", etc.).
    // Surfaced in the HUD next to the save status so designers see why a
    // paint/fill/rect/line click had no visible effect, instead of having to
    // tail the log.
    const std::string& lastToolStatus() const { return lastToolStatus_; }
    void clearToolStatus() { lastToolStatus_.clear(); }
    void loadScene(World* world, const std::string& path);

    // Play-in-editor: snapshot/restore ECS state
    void enterPlayMode(World* world);
    void exitPlayMode(World* world);
    bool inPlayMode() const { return inPlayMode_; }

    // Default Observer behavior — paused off, hide editor chrome.
    // Called by AppConfig default when user hasn't overridden onObserveStart/Stop.
    void beginLocalObserve();
    void endLocalObserve();

    void setPostProcessConfig(PostProcessConfig* cfg) { postProcessConfig_ = cfg; }
    void setUIManager(UIManager* mgr) {
        uiManager_ = mgr;
#ifdef FATE_HAS_GAME
        if (mgr) {
            mgr->addScreenReloadListener([this](const std::string&) {
                if (uiManager_) uiEditorPanel_.revalidateSelection(*uiManager_);
            });
            // A device-class change reloads every screen tree in place; if
            // the user has unsaved edits in the outgoing class, persist
            // them at the recorded variant path BEFORE the trees are
            // replaced. flushDirtyUIScreens returns false on any partial
            // write failure; UIManager treats that as "abort the reload"
            // so the dirty bit cannot end up pointing at a tree the user
            // can no longer see.
            mgr->setBeforeReloadCallback([this]() { return flushDirtyUIScreens(); });
        }
#endif
    }
    UIManager* uiManager() const { return uiManager_; }
#ifdef FATE_HAS_GAME
    UIEditorPanel& uiEditorPanel() { return uiEditorPanel_; }
#endif
    ContentBrowserPanel& contentBrowserPanel() { return contentBrowserPanel_; }

    void setAssetRoot(const std::string& root) { assetRoot_ = root; assetBrowser_.init(".", root, sourceDir_); }
    void setSourceDir(const std::string& dir) {
        sourceDir_ = dir;
#ifdef FATE_HAS_GAME
        uiEditorPanel_.setSourceDir(dir);
#endif
        animationEditor_.setSourceDir(dir);
    }
    void scanAssets();

    // Multi-select
    const std::set<EntityHandle>& selectedEntities() const { return selectedEntities_; }
    bool isSelected(EntityHandle h) const { return selectedEntities_.count(h) > 0; }

    // Inspector undo capture (call after each editable ImGui widget)
    void captureInspectorUndo();

    // Entity locking (ground tiles locked by default, toggleable via toolbar)
    bool groundLocked_ = true;
    bool isEntityLocked(Entity* e) const { return groundLocked_ && e && e->tag() == "ground"; }

    // Erase tile at position
    void eraseTileAt(World* world, Camera* camera, const Vec2& screenPos,
                     int windowWidth, int windowHeight);

    // Process keyboard shortcuts (called from App)
    void handleKeyShortcuts(World* world, const SDL_Event& event);

private:
    Editor() = default;

    bool open_ = true;   // Editor is always visible — the editor IS the application
    bool paused_ = true;  // Start paused (editing mode)
    nlohmann::json playModeSnapshot_;  // ECS state before entering play mode
    nlohmann::json sceneMetadata_;  // preserved across editor save/load for round-trip
    bool inPlayMode_ = false;
    bool editorChromeHidden_ = false;  // true while Observer mode hides editor panels
    bool frameStarted_ = false;
    bool wantsKeyboard_ = false;
    bool wantsMouse_ = false;

    // Font stack (loaded in init, used via PushFont/PopFont)
    ImFont* fontBody_ = nullptr;
    ImFont* fontHeading_ = nullptr;
    ImFont* fontSmall_ = nullptr;

    bool showDemoWindow_ = false;
#if defined(ENGINE_MEMORY_DEBUG)
    bool showMemoryPanel_ = false;
#endif

    // Cached pointers for menu bar / viewport toolbar (set each frame in renderUI)
    World* dockWorld_ = nullptr;
    Camera* dockCamera_ = nullptr;

    // Viewport FBO
    Framebuffer viewportFbo_;

    // Viewport tracking (set each frame by drawSceneViewport)
    Vec2 viewportPos_ = {0, 0};
    Vec2 viewportSize_ = {0, 0};
    bool viewportHovered_ = false;

    // Toolbar toggles
    bool showGrid_ = true;
    bool showCollisionDebug_ = false;
    bool showSpawnDebug_ = false;
    bool showGameUI_ = true;

    // Device profile index (default: iPhone 17 Pro)
    int displayPresetIdx_ = kDefaultDeviceIdx;
    bool showSafeAreaOverlay_ = false;

    // Saved camera state — restored when returning to edit mode
    Vec2 savedCamPos_ = {0, 0};
    float savedCamZoom_ = 1.0f;

    bool openSavePrefab_ = false;
    // Modal-local error message for the SavePrefabPopup. Kept separate
    // from lastSaveStatus_ so the popup's own failures don't bleed into
    // the dirty-domain HUD strip (and vice versa). Cleared whenever the
    // popup re-opens, on Cancel, or after a successful save.
    std::string prefabPopupError_;
    bool resetLayout_ = false;  // Set via View > Reset Layout
    std::string currentScenePath_;  // Path of currently loaded/saved scene

    // Dirty-domain state. See markSceneDirty / markPlayerPrefabDirty /
    // markUIScreenDirty above. UI dirtiness is keyed by (screenId, class)
    // so editing screen A then switching selection to screen B (or device)
    // still writes A at the variant file it was authored against; the
    // prefab flag is set when an authored edit lands on the player entity
    // (saveScene filters players out so a scene save alone would lose
    // those tweaks).
    bool sceneDirty_ = false;
    bool playerPrefabDirty_ = false;
    std::set<std::pair<std::string, fate::LayoutClass>> dirtyScreens_;

    // Populated by saveScene() so the editor HUD / hotkey-bind can surface
    // write failures. Prior to this flag a failed atomic write was silently
    // discarded and the UI still reported success.
    std::string lastSaveStatus_;
    bool lastSaveSucceeded_ = true;

    // Populated by paintTileAt() / handleMouseUp() when a tile-tool click
    // was rejected (no palette, no tile selected, out-of-range index, etc.).
    // Cleared on every successful tile op so it only flashes when actionable.
    std::string lastToolStatus_;

    // Tool mode
    EditorTool currentTool_ = EditorTool::Move;

    // Grid
    bool gridSnap_ = true;
    float gridSize_ = 32.0f;

    // Selection
    EntityHandle selectedHandle_;                       // authoritative — survives entity recreation
    Entity* selectedEntity_ = nullptr;
    std::set<EntityHandle> selectedEntities_; // multi-select
    bool isDraggingEntity_ = false;
    bool isResizingEntity_ = false;
    int resizeHandle_ = -1;
    Vec2 dragStartWorldPos_;
    Vec2 dragStartEntityPos_;
    Vec2 dragStartEntitySize_;

    // Spawn zone overlay (DB-backed circles)
    int selectedSpawnZoneIdx_ = -1;       // index into contentBrowserPanel_.spawnList()
    bool isDraggingSpawnZone_ = false;
    Vec2 spawnDragStartWorld_;
    float spawnDragStartCx_ = 0, spawnDragStartCy_ = 0;
    float spawnRadiusBeforeResize_ = 0;
    bool spawnRadiusDirty_ = false;       // true when scroll-wheel changed radius

    // Tile layer visibility (ground, detail, fringe, collision)
    bool showLayer_[4] = {true, true, true, true};

    // Asset browser
    std::string assetRoot_ = "assets";
    std::string sourceDir_;
    std::vector<AssetEntry> assets_;
    std::string draggedAssetPath_;
    bool isDraggingAsset_ = false;

    // Tile palette
    std::shared_ptr<Texture> paletteTexture_;
    std::string paletteTexturePath_;
    int paletteTileSize_ = 32;
    int paletteColumns_ = 0;
    int paletteRows_ = 0;
    int selectedTileIndex_ = -1;
    std::string selectedTileLayer_ = "ground";
    int brushSize_ = 1;

    // Rect/line tool drag state
    Vec2i toolDragStart_ = {-1, -1};
    Vec2i toolDragEnd_ = {-1, -1};
    bool isToolDragging_ = false;

    // Brush stroke grouping (Paint tool)
    std::unique_ptr<CompoundCommand> pendingBrushStroke_;

    // Console command
    char consoleCmdBuf_[256] = "";

    // Inspector undo capture (snapshot before/after field edit)
    nlohmann::json pendingInspectorSnapshot_;
    EntityHandle pendingInspectorHandle_;

    // Delete confirmation state
    bool pendingDeleteFile_ = false;
    std::string pendingDeletePath_;
    bool pendingDeletePrefab_ = false;
    std::string pendingDeletePrefabName_;

    // Grid shader (lazy-loaded)
    Shader gridShader_;
    bool gridShaderLoaded_ = false;
    bool gridShaderAttempted_ = false;

    // Post-process panel
    bool showPostProcessPanel_ = false;
    PostProcessConfig* postProcessConfig_ = nullptr;

    // UI editor panel
#ifdef FATE_HAS_GAME
    UIEditorPanel uiEditorPanel_;
#endif
    UIManager* uiManager_ = nullptr;

    // Dialogue node editor panel
    DialogueNodeEditor dialogueEditor_;

    // Animation editor panel
    AnimationEditor animationEditor_;

    // Paper doll editor panel
    PaperDollPanel paperDollPanel_;

    // Content browser panel (admin content editing)
    ContentBrowserPanel contentBrowserPanel_;

    // Combat text editor
    bool showCombatTextEditor_ = false;

    // Role nameplates editor (Window menu)
    bool showRoleNameplatesPanel_ = false;
    void drawRoleNameplatesPanel();

    // Tile palette panel (demo build wires this through View menu)
    bool showTilePalette_ = false;

    // Enhanced asset browser
    AssetBrowser assetBrowser_;
    bool useEnhancedBrowser_ = true;

    // ImGuizmo operation mode (synced to currentTool_)
    ImGuizmo::OPERATION gizmoOperation_ = ImGuizmo::TRANSLATE;

    // Draw functions
    void drawHUD(World* world);
    void drawMenuBar(World* world);
    void drawToolbar(World* world);
    void drawHierarchy(World* world);
    void drawInspector();
    void drawAssetBrowser(World* world, Camera* camera);
    void drawTilePalette(World* world, Camera* camera);
    void drawSceneGrid(SpriteBatch* batch, Camera* camera);
    void drawSceneGridShader(Camera* camera); // fullscreen quad grid shader
    void drawSelectionOutlines(SpriteBatch* batch, Camera* camera); // stencil outlines
    void drawImGuizmo(Camera* camera); // ImGuizmo transform handles
    void drawConsole(World* world);
    void drawDockSpace();
    void drawSceneViewport();
    void drawDebugInfoPanel(World* world);
    void loadTileset(const std::string& path, int tileSize = 32);
    void applyLayerVisibility(World* world);

    // Console command execution
    void executeCommand(World* world, const std::string& cmd);
};

} // namespace fate
