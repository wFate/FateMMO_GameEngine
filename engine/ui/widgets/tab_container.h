#pragma once
#include "engine/ui/ui_node.h"
#include <vector>

namespace fate {

class TabContainer : public UINode {
public:
    TabContainer(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;

    void addTab(const std::string& label, std::unique_ptr<UINode> content);
    int activeTab = 0;
    float tabHeight = 30.0f;

    std::vector<std::string> tabLabels_;  // public so parseNode can populate it
};

} // namespace fate
