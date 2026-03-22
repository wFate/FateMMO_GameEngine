#pragma once
#include "engine/ui/ui_node.h"

namespace fate {

enum class TextAlign : uint8_t { Left, Center, Right };

class Label : public UINode {
public:
    Label(const std::string& id);

    void render(SpriteBatch& batch, SDFText& text) override;

    std::string text;
    TextAlign align = TextAlign::Left;
    bool wordWrap = false;
};

} // namespace fate
