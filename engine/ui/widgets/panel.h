#pragma once
#include "engine/ui/ui_node.h"

namespace fate {

class Panel : public UINode {
public:
    Panel(const std::string& id);

    void render(SpriteBatch& batch, SDFText& text) override;

    bool onPress(const Vec2& localPos) override;
    void onRelease(const Vec2& localPos) override;

    bool draggable = false;
    bool closeable = false;
    std::string title;

    Vec2 dragOffset_;
    bool isDragging_ = false;
};

} // namespace fate
