#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_theme.h"
#include "engine/ui/ui_input.h"
#include "engine/ui/widgets/tooltip.h"
#include "engine/ui/ui_data_binding.h"
#include "engine/ui/ui_hot_reload.h"
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

namespace fate {

class SpriteBatch;
class SDFText;

class UIManager {
public:
    // Screen loading
    bool loadScreen(const std::string& filepath);
    bool loadScreenFromString(const std::string& screenId, const std::string& jsonStr);
    void unloadScreen(const std::string& screenId);
    UINode* getScreen(const std::string& screenId);

    // Theme
    UITheme& theme() { return theme_; }
    bool loadTheme(const std::string& filepath);

    // Data binding
    UIDataBinding& dataBinding() { return dataBinding_; }

    // Per-frame
    void update(float dt);
    void computeLayout(float screenWidth, float screenHeight);
    void render(SpriteBatch& batch, SDFText& text);

    // Hit-testing
    UINode* hitTest(const Vec2& point);

    // Input routing
    void handleInput();
    void updateHover(const Vec2& mousePos);
    void handlePress(const Vec2& mousePos);
    void handleRelease(const Vec2& mousePos);
    void handleKeyInput(int scancode, bool pressed);
    void handleTextInput(const std::string& text);

    // Map raw (window-space) mouse coordinates into layout space.
    // offset  = viewport top-left in window coords
    // scale   = FBO size / displayed size  (>1 when FBO is larger than panel)
    void setInputTransform(float offX, float offY, float scaleX, float scaleY) {
        inputOffsetX_ = offX; inputOffsetY_ = offY;
        inputScaleX_  = scaleX; inputScaleY_ = scaleY;
    }

    UINode* hoveredNode() const { return hoveredNode_; }
    UINode* focusedNode() const { return focusedNode_; }
    UINode* pressedNode() const { return pressedNode_; }
    bool wantCaptureMouse() const { return hoveredNode_ != nullptr || pressedNode_ != nullptr; }
    DragPayload& dragPayload() { return dragPayload_; }
    bool isDragging() const { return dragPayload_.active; }

    // Editor: list loaded screen IDs
    std::vector<std::string> screenIds() const { return screenOrder_; }

private:
    UITheme theme_;
    std::vector<std::string> screenOrder_;
    std::unordered_map<std::string, std::unique_ptr<UINode>> screens_;

    UINode* hoveredNode_ = nullptr;
    UINode* focusedNode_ = nullptr;
    UINode* pressedNode_ = nullptr;
    Vec2 pressStartPos_;
    DragPayload dragPayload_;
    static constexpr float DRAG_THRESHOLD = 5.0f;

    Tooltip tooltip_{"__tooltip__"};
    float tooltipHoverTime_ = 0.0f;
    UINode* tooltipTarget_ = nullptr;
    float screenWidth_ = 0.0f;
    float screenHeight_ = 0.0f;
    static constexpr float TOOLTIP_DELAY = 0.5f;
    // UI authored at 900px height; pixel values scale proportionally for other viewports
    static constexpr float UI_REFERENCE_HEIGHT = 900.0f;

    UIDataBinding dataBinding_;

    float inputOffsetX_ = 0.0f;
    float inputOffsetY_ = 0.0f;
    float inputScaleX_  = 1.0f;
    float inputScaleY_  = 1.0f;

    UIHotReload hotReload_;
    float hotReloadTimer_ = 0.0f;
    std::unordered_map<std::string, std::string> screenFilePaths_;

    std::unique_ptr<UINode> parseNode(const nlohmann::json& j);
    AnchorPreset parsePreset(const std::string& name);
    void applyThemeStyles(UINode* node);
};

} // namespace fate
