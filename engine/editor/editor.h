#pragma once
#include "engine/ecs/world.h"
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
#ifdef FATE_HAS_GAME
#include "game/components/tile_layer_component.h"
#endif
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
};

// Safe area insets are LANDSCAPE values (game runs landscape).
// In landscape: Dynamic Island/notch moves to LEFT side, home indicator at BOTTOM (reduced).
//                                                                      top bot left right
static constexpr DeviceProfile kDeviceProfiles[] = {
    // Apple iPhone
    {"iPhone SE (3rd gen)",  "Apple iPhone", 1334,  750, 2.0f,  0,  0,  0,  0, false, false},
    {"iPhone 14",            "Apple iPhone", 2532, 1170, 3.0f,  0, 21, 47,  0, true,  false},
    {"iPhone 14 Plus",       "Apple iPhone", 2778, 1284, 3.0f,  0, 21, 47,  0, true,  false},
    {"iPhone 14 Pro",        "Apple iPhone", 2556, 1179, 3.0f,  0, 21, 59,  0, false, true},
    {"iPhone 14 Pro Max",    "Apple iPhone", 2796, 1290, 3.0f,  0, 21, 59,  0, false, true},
    {"iPhone 15",            "Apple iPhone", 2556, 1179, 3.0f,  0, 21, 59,  0, false, true},
    {"iPhone 15 Plus",       "Apple iPhone", 2796, 1290, 3.0f,  0, 21, 59,  0, false, true},
    {"iPhone 15 Pro",        "Apple iPhone", 2556, 1179, 3.0f,  0, 21, 59,  0, false, true},
    {"iPhone 15 Pro Max",    "Apple iPhone", 2796, 1290, 3.0f,  0, 21, 59,  0, false, true},
    {"iPhone 16",            "Apple iPhone", 2556, 1179, 3.0f,  0, 21, 59,  0, false, true},
    {"iPhone 16 Plus",       "Apple iPhone", 2796, 1290, 3.0f,  0, 21, 59,  0, false, true},
    {"iPhone 16 Pro",        "Apple iPhone", 2622, 1206, 3.0f,  0, 21, 59,  0, false, true},
    {"iPhone 16 Pro Max",    "Apple iPhone", 2868, 1320, 3.0f,  0, 21, 59,  0, false, true},
    {"iPhone 17 Pro",        "Apple iPhone", 2622, 1206, 3.0f,  0, 21, 59,  0, false, true},
    // Apple iPad (fullscreen landscape games: no insets)
    {"iPad (10th gen)",      "Apple iPad",   2360, 1640, 2.0f,  0,  0,  0,  0, false, false},
    {"iPad Air (M3)",        "Apple iPad",   2360, 1640, 2.0f,  0,  0,  0,  0, false, false},
    {"iPad Pro 11\" (M4)",   "Apple iPad",   2420, 1668, 2.0f,  0,  0,  0,  0, false, false},
    {"iPad Pro 13\" (M4)",   "Apple iPad",   2752, 2064, 2.0f,  0,  0,  0,  0, false, false},
    // Android (landscape: status bar hidden, nav bar at bottom)
    {"Pixel 9",              "Android",      2424, 1080, 2.6f,  0, 24,  0,  0, false, false},
    {"Pixel 9 Pro",          "Android",      2856, 1280, 2.8f,  0, 24,  0,  0, false, false},
    {"Samsung S24",          "Android",      2340, 1080, 3.0f,  0, 24,  0,  0, false, false},
    {"Samsung S24 Ultra",    "Android",      3120, 1440, 3.0f,  0, 24,  0,  0, false, false},
    {"Samsung S25",          "Android",      2340, 1080, 3.0f,  0, 24,  0,  0, false, false},
    // Desktop
    {"720p",                 "Desktop",      1280,  720, 1.0f,  0,  0,  0,  0, false, false},
    {"1080p",                "Desktop",      1920, 1080, 1.0f,  0,  0,  0,  0, false, false},
    {"1440p",                "Desktop",      2560, 1440, 1.0f,  0,  0,  0,  0, false, false},
    {"4K",                   "Desktop",      3840, 2160, 1.0f,  0,  0,  0,  0, false, false},
    {"Ultrawide 1080p",      "Desktop",      2560, 1080, 1.0f,  0,  0,  0,  0, false, false},
    // Free Aspect
    {"Free Aspect",          "Free",            0,    0, 1.0f,  0,  0,  0,  0, false, false},
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
    bool showGameUI() const { return showGameUI_; }

    // FBO management
    Framebuffer& viewportFbo() { return viewportFbo_; }

    void handleSceneClick(World* world, Camera* camera, const Vec2& screenPos,
                          int windowWidth, int windowHeight);
    void handleSceneDrag(Camera* camera, const Vec2& screenPos,
                         int windowWidth, int windowHeight);
    void handleMouseUp();
    void paintTileAt(World* world, Camera* camera, const Vec2& screenPos,
                     int windowWidth, int windowHeight);

    bool isOpen() const { return open_; }
    void toggle() { open_ = !open_; }
    bool isPaused() const { return paused_; }
    void setPaused(bool p) { paused_ = p; }
    const std::string& currentScenePath() const { return currentScenePath_; }
    void setCurrentScenePath(const std::string& p) { currentScenePath_ = p; }

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

    bool gridSnapEnabled() const { return gridSnap_; }
    float gridSize() const { return gridSize_; }

    void saveScene(World* world, const std::string& path);
    void loadScene(World* world, const std::string& path);

    // Play-in-editor: snapshot/restore ECS state
    void enterPlayMode(World* world);
    void exitPlayMode(World* world);
    bool inPlayMode() const { return inPlayMode_; }

    void setPostProcessConfig(PostProcessConfig* cfg) { postProcessConfig_ = cfg; }
    void setUIManager(UIManager* mgr) {
        uiManager_ = mgr;
#ifdef FATE_HAS_GAME
        if (mgr) {
            mgr->addScreenReloadListener([this](const std::string&) {
                if (uiManager_) uiEditorPanel_.revalidateSelection(*uiManager_);
            });
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

    // Saved gameplay camera state — restored when resuming from pause
    Vec2 pausedCamPos_ = {0, 0};
    float pausedCamZoom_ = 1.0f;

    bool openSavePrefab_ = false;
    bool resetLayout_ = false;  // Set via View > Reset Layout
    std::string currentScenePath_;  // Path of currently loaded/saved scene

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
