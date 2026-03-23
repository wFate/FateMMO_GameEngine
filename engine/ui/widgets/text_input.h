#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"

namespace fate {

class TextInput : public UINode {
public:
    TextInput(const std::string& id);

    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;
    void onFocusGained() override;
    void onFocusLost() override;
    bool onKeyInput(int scancode, bool pressed) override;
    bool onTextInput(const std::string& input) override;

    std::string text;
    std::string placeholder;
    int maxLength = 0;
    bool masked = false;
    int cursorPos = 0;
    UITextCallback onSubmit;
};

} // namespace fate
