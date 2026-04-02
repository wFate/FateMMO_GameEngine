#pragma once
// Minimal no-op stubs for demo/public builds without the proprietary UI system.
// Used when FATE_HAS_GAME is not defined.

#include "engine/core/types.h"
#include <string>
#include <functional>
#include <vector>

namespace fate {

class SpriteBatch;
class SDFText;

class UINode {
public:
    bool visible() const { return false; }
    bool onKeyInput(int, bool) { return false; }
    void onFocusLost() {}
    void onFocusGained() {}
};

class UIManager {
public:
    bool loadTheme(const std::string&) { return false; }
    void update(float) {}
    void computeLayout(float, float) {}
    void render(SpriteBatch&, SDFText&) {}
    void handleInput() {}
    void handleTextInput(const std::string&) {}
    void setInputTransform(float, float, float, float) {}
    UINode* focusedNode() const { return nullptr; }
    UINode* pressedNode() const { return nullptr; }
    void clearFocus() {}
    void suppressHotReload(float = 1.5f) {}
    void addScreenReloadListener(std::function<void(const std::string&)>) {}
    UINode* getScreen(const std::string&) { return nullptr; }
    bool loadScreenFromString(const std::string&, const std::string&) { return false; }
    std::vector<std::string> screenIds() const { return {}; }
};

} // namespace fate
