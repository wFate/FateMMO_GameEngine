#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace fate {

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

    void openFile(const std::string& path);

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

    // Draw sub-panels
    void drawTopBar();
    void drawFrameWorkspace();
    void drawStateList();
    void drawFrameStrip();
    void drawStateProperties();
    void drawPreview();
    void drawMenuBar();

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
    unsigned int loadFrameTexture(const std::string& path);
};

} // namespace fate
