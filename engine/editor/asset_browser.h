#pragma once
#include "engine/render/texture.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

struct ImFont;

namespace fate {

class World;
class Camera;

class AssetBrowser {
public:
    void init(const std::string& assetRoot, const std::string& sourceDir);
    void shutdown() { thumbCache_.clear(); }
    void scan();  // Recursively scan asset directory
    void draw(World* world, Camera* camera);  // Main ImGui rendering

    // Drag-and-drop state (read by Editor for scene placement)
    bool isDraggingAsset() const { return isDraggingAsset_; }
    const std::string& draggedAssetPath() const { return draggedAssetPath_; }
    void clearDrag() { isDraggingAsset_ = false; draggedAssetPath_.clear(); }

    // Callback for opening animation files
    std::function<void(const std::string&)> onOpenAnimation;

    void setFonts(ImFont* heading, ImFont* small) { fontHeading_ = heading; fontSmall_ = small; }

private:
    // Asset entry with type classification
    enum class AssetType { Sprite, Script, Scene, Shader, Audio, Font, Tile, Prefab, Animation, Other };

    struct Entry {
        std::string name;
        std::string fullPath;
        std::string relativePath;  // relative to assetRoot_
        std::string extension;
        AssetType type = AssetType::Other;
        std::shared_ptr<Texture> thumbnail;  // lazy-loaded
        bool isDirectory = false;
    };

    std::string assetRoot_;
    std::string sourceDir_;

    // Current view state
    std::string currentDir_;    // current directory relative to assetRoot
    std::vector<Entry> currentEntries_;  // entries in current directory

    // Search/filter
    char searchBuf_[128] = "";

    // Grid layout
    int thumbnailSize_ = 64;

    // Selection & drag-and-drop placement
    std::string selectedPath_;
    bool isDraggingAsset_ = false;
    std::string draggedAssetPath_;

    // Thumbnail cache
    std::unordered_map<std::string, std::shared_ptr<Texture>> thumbCache_;

    // Methods
    void scanDirectory(const std::string& dir);
    void drawBreadcrumb();
    void drawSearchBar();
    void drawGrid(World* world, Camera* camera);
    AssetType classifyFile(const std::string& name, const std::string& ext) const;
    std::shared_ptr<Texture> getThumbnail(const Entry& entry);
    void navigateTo(const std::string& relDir);

    ImFont* fontHeading_ = nullptr;
    ImFont* fontSmall_ = nullptr;
};

} // namespace fate
