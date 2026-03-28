#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include <functional>

namespace fate {

class SettingsPanel : public UINode {
public:
    SettingsPanel(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;

    // --- Serializable layout properties (editor-tweakable) ---
    float titleFontSize       = 16.0f;
    float sectionFontSize     = 13.0f;
    float labelFontSize       = 11.0f;
    float buttonFontSize      = 13.0f;
    float logoutButtonWidth   = 120.0f;
    float logoutButtonHeight  = 36.0f;
    float buttonCornerRadius  = 4.0f;
    float sectionSpacing      = 20.0f;
    float itemSpacing         = 8.0f;
    float borderWidth         = 2.0f;

    // --- Serializable colors (editor-tweakable) ---
    Color titleColor          = {0.25f, 0.16f, 0.08f, 1.0f};
    Color sectionColor        = {0.35f, 0.25f, 0.12f, 1.0f};
    Color labelColor          = {0.50f, 0.40f, 0.30f, 1.0f};
    Color logoutBtnColor      = {0.55f, 0.15f, 0.15f, 0.9f};
    Color logoutBtnHoverColor = {0.70f, 0.20f, 0.20f, 0.95f};
    Color logoutTextColor     = {1.0f, 0.95f, 0.90f, 1.0f};
    Color dividerColor        = {0.70f, 0.60f, 0.40f, 0.4f};

    // --- Position offsets (unscaled, multiplied by layoutScale_ at render) ---
    Vec2 titleOffset          = {0.0f, 0.0f};
    Vec2 logoutOffset         = {0.0f, 0.0f};

    // --- Callbacks ---
    std::function<void()> onLogout;
    UIClickCallback onClose;

private:
    Rect closeBtnRect_;
    Rect logoutBtnRect_;
};

} // namespace fate
