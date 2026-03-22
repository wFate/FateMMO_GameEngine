#pragma once
#include "engine/ui/ui_node.h"

namespace fate {

class ScrollView : public UINode {
public:
    ScrollView(const std::string& id);

    void render(SpriteBatch& batch, SDFText& text) override;

    bool onPress(const Vec2& localPos) override;
    bool onKeyInput(int scancode, bool pressed) override;

    void scroll(float deltaY);

    float scrollOffset = 0.0f;
    float contentHeight = 0.0f;
    float scrollSpeed = 30.0f;
};

} // namespace fate
