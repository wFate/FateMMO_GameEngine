#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include <vector>
#include <functional>
#include <string>

namespace fate {

class LeftSidebar : public UINode {
public:
    LeftSidebar(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;

    float buttonSize = 40.0f;
    float spacing = 8.0f;
    std::vector<std::string> panelLabels;  // e.g., {"Status", "Skills", "Inv", "Map", "Menu"}
    std::string activePanel;

    std::function<void(int index, const std::string& label)> onPanelSelect;
};

} // namespace fate
