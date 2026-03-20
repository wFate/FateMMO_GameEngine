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
#include <ImGuizmo.h>
#include <SDL.h>
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

// Display resolution presets for play-testing
struct DisplayPreset {
    const char* name;
    int width;
    int height;
};

static constexpr DisplayPreset kDisplayPresets[] = {
    {"Free Aspect",         0,    0},
    {"iPhone 16 Pro",     852,  393},
    {"iPhone 16 Pro Max", 932,  430},
    {"iPhone SE",         667,  375},
    {"iPad Pro 11\"",    1194,  834},
    {"iPad Pro 12.9\"",  1366, 1024},
    {"Samsung S24",       780,  360},
    {"Pixel 9",           915,  412},
    {"1080p",            1920, 1080},
    {"720p",             1280,  720},
    {"4K",               3840, 2160},
};
static constexpr int kDisplayPresetCount = sizeof(kDisplayPresets) / sizeof(kDisplayPresets[0]);

// Editor tool modes (like Unity W/E/R)
enum class EditorTool {
    Move,    // W - drag to move entities
    Scale,   // E - drag handles to scale/resize
    Rotate,  // R - rotate entity
    Paint,   // B - tile painting mode
    Erase    // X - click to delete ground tiles
};

class Editor {
public:
    static Editor& instance() {
        static Editor s_instance;
        return s_instance;
    }

    bool init(SDL_Window* window, SDL_GLContext glContext);
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
    bool showGrid() const { return showGrid_; }

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

    bool wantsInput() const { return open_ && (wantsKeyboard_ || wantsMouse_); }
    bool wantsKeyboard() const { return open_ && wantsKeyboard_; }
    bool wantsMouse() const { return open_ && wantsMouse_; }

    Entity* selectedEntity() const { return selectedEntity_; }
    void clearSelection() { selectedEntity_ = nullptr; isDraggingEntity_ = false; selectedEntities_.clear(); }
    void cancelPlacement() { isDraggingAsset_ = false; draggedAssetPath_.clear(); }
    bool isTilePaintMode() const { return currentTool_ == EditorTool::Paint && selectedTileIndex_ >= 0; }
    bool isEraseMode() const { return currentTool_ == EditorTool::Erase; }
    bool isScaleMode() const { return currentTool_ == EditorTool::Scale; }

    EditorTool currentTool() const { return currentTool_; }

    bool gridSnapEnabled() const { return gridSnap_; }
    float gridSize() const { return gridSize_; }

    void saveScene(World* world, const std::string& path);
    void loadScene(World* world, const std::string& path);

    void setPostProcessConfig(PostProcessConfig* cfg) { postProcessConfig_ = cfg; }

    void setAssetRoot(const std::string& root) { assetRoot_ = root; }
    void setSourceDir(const std::string& dir) { sourceDir_ = dir; }
    void scanAssets();

    // Multi-select
    const std::set<EntityHandle>& selectedEntities() const { return selectedEntities_; }
    bool isSelected(EntityHandle h) const { return selectedEntities_.count(h) > 0; }
    bool isSelected(EntityId id) const {
        // Legacy compat: search by raw id
        for (auto& h : selectedEntities_) {
            if (h.index() == id) return true;
        }
        return false;
    }

    // Entity locking
    static bool isEntityLocked(Entity* e) { return e && e->tag() == "ground"; }

    // Erase tile at position
    void eraseTileAt(World* world, Camera* camera, const Vec2& screenPos,
                     int windowWidth, int windowHeight);

    // Process keyboard shortcuts (called from App)
    void handleKeyShortcuts(World* world, const SDL_Event& event);

private:
    Editor() = default;

    bool open_ = true;   // Editor is always visible — the editor IS the application
    bool paused_ = true;  // Start paused (editing mode)
    bool frameStarted_ = false;
    bool wantsKeyboard_ = false;
    bool wantsMouse_ = false;
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

    // Display resolution preset (0 = Free Aspect, fills viewport)
    int displayPresetIdx_ = 0;

    // Saved camera state — restored when returning to edit mode
    Vec2 savedCamPos_ = {0, 0};
    float savedCamZoom_ = 1.0f;

    bool openSavePrefab_ = false;
    bool resetLayout_ = false;  // Set via View > Reset Layout
    std::string currentScenePath_;  // Path of currently loaded/saved scene

    // Tool mode
    EditorTool currentTool_ = EditorTool::Move;

    // Grid
    bool gridSnap_ = true;
    float gridSize_ = 32.0f;

    // Selection
    Entity* selectedEntity_ = nullptr;
    std::set<EntityHandle> selectedEntities_; // multi-select
    bool isDraggingEntity_ = false;
    bool isResizingEntity_ = false;
    int resizeHandle_ = -1;
    Vec2 dragStartWorldPos_;
    Vec2 dragStartEntityPos_;
    Vec2 dragStartEntitySize_;

    // Layer visibility
    bool showGroundLayer_ = true;
    bool showObstacleLayer_ = true;
    bool showPlayerLayer_ = true;

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

    // Console command
    char consoleCmdBuf_[256] = "";

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
    void drawViewportHUD(World* world);
    void drawDebugInfoPanel(World* world);
    void loadTileset(const std::string& path, int tileSize = 32);

    // Console command execution
    void executeCommand(World* world, const std::string& cmd);
};

} // namespace fate
