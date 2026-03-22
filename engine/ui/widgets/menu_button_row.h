#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include <vector>
#include <functional>

namespace fate {

class MenuButtonRow : public UINode {
public:
    MenuButtonRow(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;

    float buttonSize = 36.0f;
    float spacing    = 8.0f;
    std::vector<std::string> labels;  // e.g., {"Event", "Shop", "Map", "Inv", "Menu"}

    std::function<void(int index, const std::string& label)> onButtonClick;
};

} // namespace fate
