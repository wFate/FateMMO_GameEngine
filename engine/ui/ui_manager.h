#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_theme.h"
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

private:
    UITheme theme_;
    std::vector<std::string> screenOrder_;
    std::unordered_map<std::string, std::unique_ptr<UINode>> screens_;

    std::unique_ptr<UINode> parseNode(const nlohmann::json& j);
    AnchorPreset parsePreset(const std::string& name);
    void applyThemeStyles(UINode* node);
};

} // namespace fate
