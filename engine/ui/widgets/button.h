#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"

namespace fate {

class Button : public UINode {
public:
    Button(const std::string& id);

    void render(SpriteBatch& batch, SDFText& text) override;

    bool onPress(const Vec2& localPos) override;
    void onRelease(const Vec2& localPos) override;
    void onHoverEnter() override;
    void onHoverExit() override;

    bool isPressed() const { return pressed_; }

    std::string text;
    std::string icon;
    UIClickCallback onClick;
};

} // namespace fate
