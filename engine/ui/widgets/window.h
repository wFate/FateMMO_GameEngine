#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"

namespace fate {

class Window : public UINode {
public:
    Window(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;

    bool onPress(const Vec2& localPos) override;
    void onRelease(const Vec2& localPos) override;

    std::string title;
    bool closeable = true;
    bool resizable = false;
    bool minimizable = false;
    bool minimized = false;
    float titleBarHeight = 28.0f;

    UIClickCallback onClose;

    // Public accessors for UIManager drag handling
    bool isDragging() const { return isDragging_; }
    const Vec2& dragOffset() const { return dragOffset_; }

private:
    bool isDragging_ = false;
    Vec2 dragOffset_;
    bool isResizing_ = false;
};

} // namespace fate
