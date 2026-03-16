#pragma once
#include "engine/ecs/world.h"
#include "engine/ecs/entity.h"
#include "engine/render/camera.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/texture.h"
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

// Editor tool modes (like Unity W/E/R)
enum class EditorTool {
    Move,    // W - drag to move entities
    Resize,  // E - drag handles to resize
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
    void render(World* world, Camera* camera, SpriteBatch* batch);

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

    EditorTool currentTool() const { return currentTool_; }

    bool gridSnapEnabled() const { return gridSnap_; }
    float gridSize() const { return gridSize_; }

    void saveScene(World* world, const std::string& path);
    void loadScene(World* world, const std::string& path);

    void setAssetRoot(const std::string& root) { assetRoot_ = root; }
    void setSourceDir(const std::string& dir) { sourceDir_ = dir; }
    void scanAssets();

    // Multi-select
    const std::set<EntityId>& selectedEntities() const { return selectedEntities_; }
    bool isSelected(EntityId id) const { return selectedEntities_.count(id) > 0; }

    // Entity locking
    static bool isEntityLocked(Entity* e) { return e && e->tag() == "ground"; }

    // Erase tile at position
    void eraseTileAt(World* world, Camera* camera, const Vec2& screenPos,
                     int windowWidth, int windowHeight);

    // Process keyboard shortcuts (called from App)
    void handleKeyShortcuts(World* world, const SDL_Event& event);

private:
    Editor() = default;

    bool open_ = false;
    bool paused_ = false;
    bool frameStarted_ = false;
    bool wantsKeyboard_ = false;
    bool wantsMouse_ = false;
    bool showDemoWindow_ = false;
    bool openSavePrefab_ = false;

    // Tool mode
    EditorTool currentTool_ = EditorTool::Move;

    // Grid
    bool gridSnap_ = true;
    float gridSize_ = 32.0f;

    // Selection
    Entity* selectedEntity_ = nullptr;
    std::set<EntityId> selectedEntities_; // multi-select
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

    // Draw functions
    void drawHUD(World* world);
    void drawMenuBar(World* world);
    void drawToolbar(World* world);
    void drawHierarchy(World* world);
    void drawInspector();
    void drawAssetBrowser(World* world, Camera* camera);
    void drawTilePalette(World* world, Camera* camera);
    void drawSceneGrid(SpriteBatch* batch, Camera* camera);
    void drawConsole(World* world);
    void loadTileset(const std::string& path, int tileSize = 32);

    // Console command execution
    void executeCommand(World* world, const std::string& cmd);
};

} // namespace fate
