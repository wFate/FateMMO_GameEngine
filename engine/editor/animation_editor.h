#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

struct ImFont;

namespace fate {

struct PackedSheetMeta;

// Template data (.anim)
struct AnimState {
    std::string name;
    float frameRate = 8.0f;
    bool loop = true;
    int hitFrame = -1;
    std::unordered_map<std::string, int> frameCount; // direction -> count
};

struct AnimTemplate {
    int version = 1;
    std::string name;
    std::string entityType;
    std::vector<AnimState> states;
};

// Frame set data (.frameset)
using FrameMap = std::unordered_map<std::string,
                    std::unordered_map<std::string, std::vector<std::string>>>;

struct FrameSet {
    int version = 1;
    std::string templateName;
    std::string layer;
    std::string variant;
    FrameMap frames;
    std::string packedSheet;
    std::string packedMeta;
};

// Animation editor panel — dockable ImGui window for editing .anim and .frameset files
class AnimationEditor {
public:
    void init();
    void shutdown();
    void draw();

    bool isOpen() const { return open_; }
    void setOpen(bool o) { open_ = o; }
    void setFonts(ImFont* heading, ImFont* small) { fontHeading_ = heading; fontSmall_ = small; }

    void openFile(const std::string& path);
    void openWithSheet(const std::string& texturePath);
    void setSourceDir(const std::string& dir) { sourceDir_ = dir; }

private:
    bool open_ = false;

    // Template state
    AnimTemplate template_;
    std::string templatePath_;
    std::unordered_map<std::string, FrameSet> frameSets_;

    // UI selection state
    int selectedStateIdx_ = 0;
    int selectedDirection_ = 0;
    std::string selectedLayer_ = "body";
    std::string selectedVariant_;
    int selectedFrameIdx_ = -1;

    // Preview playback state
    bool previewPlaying_ = false;
    float previewTimer_ = 0.0f;
    int previewFrame_ = 0;

    // Texture cache
    std::unordered_map<std::string, unsigned int> frameTexCache_;

    // UI popup / dialog state
    char openPathBuf_[256] = {};
    char newStateBuf_[64] = {};
    char variantBuf_[64] = {};
    char loadFrameSetBuf_[256] = {};

    // Slicer mode state
    bool slicerMode_ = false;
    std::string sheetTexturePath_;
    unsigned int sheetTexture_ = 0;
    int slicerCellW_ = 32;
    int slicerCellH_ = 32;
    float slicerZoom_ = 2.0f;
    int slicerHoveredFrame_ = -1;
    std::string sourceDir_;

    // State name -> direction -> assigned frame indices (from the sliced grid)
    std::unordered_map<std::string,
        std::unordered_map<std::string, std::vector<int>>> slicerFrameAssignments_;

    // Draw sub-panels
    void drawTopBar();
    void drawFrameWorkspace();
    void drawStateList();
    void drawFrameStrip();
    void drawStateProperties();
    void drawPreview();
    void drawMenuBar();
    void drawSlicerView();
    void drawSlicerFrameStrip();

    // Slicer I/O
    void saveMetaJson(const std::string& sheetPath);
    void loadMetaJson(const std::string& sheetPath);
    void reconstructStatesFromMeta(const PackedSheetMeta& meta);
    void newMobTemplate();
    void newPlayerTemplate();

    // File I/O
    void newTemplate(const std::string& entityType);
    void loadTemplate(const std::string& path);
    void saveTemplate();
    void loadFrameSet(const std::string& path);
    void saveFrameSet(const std::string& layerVariantKey);
    void packFrameSet(const std::string& layerVariantKey);

    // Helpers
    std::string currentLayerVariantKey() const;
    std::vector<std::string>& currentFrameList();
    const char* directionName(int idx) const;

    // Fallback for currentFrameList() when no valid state is selected
    // Modifications to this vector are discarded on the next call.
    std::vector<std::string> fallbackFrames_;
    unsigned int loadFrameTexture(const std::string& path);

    ImFont* fontHeading_ = nullptr;
    ImFont* fontSmall_ = nullptr;
};

} // namespace fate
