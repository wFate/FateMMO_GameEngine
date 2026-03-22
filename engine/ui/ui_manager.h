#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_theme.h"
#include "engine/ui/ui_input.h"
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

    UINode* hoveredNode() const { return hoveredNode_; }
    UINode* focusedNode() const { return focusedNode_; }
    UINode* pressedNode() const { return pressedNode_; }
    DragPayload& dragPayload() { return dragPayload_; }
    bool isDragging() const { return dragPayload_.active; }

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

    std::unique_ptr<UINode> parseNode(const nlohmann::json& j);
    AnchorPreset parsePreset(const std::string& name);
    void applyThemeStyles(UINode* node);
};

} // namespace fate
