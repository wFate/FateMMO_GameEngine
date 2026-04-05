#pragma once
#include <string>
#include <array>

struct ImFont;
struct ImDrawList;

namespace fate {

class AnimationPreview {
public:
    void init();
    void shutdown();

    // Called each frame by AnimationEditor
    // direction: 0=down, 1=up, 2=side
    void draw(int currentFrame, int direction, float zoom);

    // Layer configuration
    void setLayerVisible(int layer, bool visible); // 0=body..4=weapon
    void setClassPreset(const std::string& preset); // "warrior", "mage", "archer"
    void setLayerStyle(int layer, const std::string& style);

    // Sheet association
    void setPrimarySheet(const std::string& sheetPath, int frameW, int frameH, int columns);

    // Onion skinning
    void setOnionSkin(bool enabled) { onionSkin_ = enabled; }
    bool onionSkinEnabled() const { return onionSkin_; }

    // Public state for AnimationEditor to draw controls
    std::array<bool, 5> layerVisible_ = {true, true, true, true, true};
    std::string classPreset_ = "mage";
    std::array<std::string, 5> layerStyles_; // body, hair, armor, hat, weapon
    int selectedDirection_ = 0; // 0=down, 1=up, 2=side

private:
    std::string sheetPath_;
    int frameW_ = 48, frameH_ = 96, columns_ = 1;
    bool onionSkin_ = false;

    void drawCheckerboard(ImDrawList* drawList, float x, float y, float w, float h) const;
    void drawLayerFrame(ImDrawList* drawList, unsigned int texId, int frame,
                        float x, float y, float zoom, float alpha,
                        float offsetX = 0.0f, float offsetY = 0.0f) const;
};

} // namespace fate
