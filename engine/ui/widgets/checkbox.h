#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include <string>

namespace fate {

class Checkbox : public UINode {
public:
    Checkbox(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;

    bool checked = false;
    std::string label;
    float boxSize = 16.0f;
    float spacing = 6.0f;

    UIClickCallback onToggle;
};

} // namespace fate
