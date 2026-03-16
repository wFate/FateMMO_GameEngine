#pragma once
#include "engine/ecs/world.h"
#include "engine/ecs/entity.h"
#include "engine/render/camera.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/texture.h"
#include <SDL.h>
#include <string>
#include <vector>
#include <memory>

namespace fate {

// Asset types
enum class AssetType {
    Sprite,   // .png, .jpg, .bmp
    Script,   // .h, .cpp
    Scene,    // .json (in scenes/)
    Shader,   // .vert, .frag
    Other
};

// Asset entry for the project browser
struct AssetEntry {
    std::string name;       // filename without path
    std::string fullPath;   // full path for loading
    std::string relativePath; // relative to project root for serialization
    AssetType type = AssetType::Other;
    std::shared_ptr<Texture> thumbnail; // loaded on demand (sprites only)
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

    // Scene interaction (called from App when editor is open and mouse isn't on ImGui)
    void handleSceneClick(World* world, Camera* camera, const Vec2& screenPos,
                          int windowWidth, int windowHeight);
    void handleSceneDrag(Camera* camera, const Vec2& screenPos,
                         int windowWidth, int windowHeight);
    void handleMouseUp();

    bool isOpen() const { return open_; }
    void toggle() { open_ = !open_; }
    bool isPaused() const { return paused_; }
    void setPaused(bool p) { paused_ = p; }

    bool wantsInput() const { return open_ && (wantsKeyboard_ || wantsMouse_); }
    bool wantsKeyboard() const { return open_ && wantsKeyboard_; }
    bool wantsMouse() const { return open_ && wantsMouse_; }

    Entity* selectedEntity() const { return selectedEntity_; }
    void clearSelection() { selectedEntity_ = nullptr; isDraggingEntity_ = false; }
    void cancelPlacement() { isDraggingAsset_ = false; draggedAssetPath_.clear(); }

    // Grid snap
    bool gridSnapEnabled() const { return gridSnap_; }
    float gridSize() const { return gridSize_; }

    // Scene save/load
    void saveScene(World* world, const std::string& path);
    void loadScene(World* world, const std::string& path);

    // Asset browser needs to know asset root
    void setAssetRoot(const std::string& root) { assetRoot_ = root; }
    void scanAssets();

private:
    Editor() = default;

    bool open_ = false;
    bool paused_ = false;
    bool frameStarted_ = false;
    bool wantsKeyboard_ = false;
    bool wantsMouse_ = false;
    bool showDemoWindow_ = false;

    // Grid
    bool gridSnap_ = true;
    float gridSize_ = 32.0f;

    // Selection
    Entity* selectedEntity_ = nullptr;
    bool isDraggingEntity_ = false;
    Vec2 dragStartWorldPos_;
    Vec2 dragStartEntityPos_;

    // Asset browser
    std::string assetRoot_ = "assets";
    std::vector<AssetEntry> assets_;
    std::string draggedAssetPath_;  // non-empty when dragging from asset browser
    bool isDraggingAsset_ = false;

    // Draw functions
    void drawHUD(World* world);
    void drawMenuBar(World* world);
    void drawToolbar(World* world);
    void drawHierarchy(World* world);
    void drawInspector();
    void drawAssetBrowser(World* world, Camera* camera);
    void drawSceneGrid(SpriteBatch* batch, Camera* camera);
};

} // namespace fate
