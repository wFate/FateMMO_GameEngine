#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include <string>

namespace fate {

class ConfirmDialog : public UINode {
public:
    ConfirmDialog(const std::string& id);

    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;
    void onRelease(const Vec2& localPos) override;

    std::string message = "Are you sure?";
    std::string confirmText = "Confirm";
    std::string cancelText = "Cancel";
    float buttonWidth = 100.0f;
    float buttonHeight = 32.0f;
    float buttonSpacing = 16.0f;

    UIClickCallback onConfirm;
    UIClickCallback onCancel;

private:
    // Returns the button rects in local space (relative to computedRect_)
    Rect confirmButtonRect() const;
    Rect cancelButtonRect() const;

    bool confirmHovered_ = false;
    bool cancelHovered_ = false;
    bool confirmPressed_ = false;
    bool cancelPressed_ = false;
};

} // namespace fate
