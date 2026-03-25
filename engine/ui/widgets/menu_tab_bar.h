#pragma once
#include "engine/ui/ui_node.h"
#include <vector>
#include <string>
#include <functional>

namespace fate {

class MenuTabBar : public UINode {
public:
    MenuTabBar(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;

    int activeTab = 0;
    std::vector<std::string> tabLabels = {"STS", "INV", "SKL", "GLD", "SOC", "SET", "SHP"};
    float tabSize  = 50.0f;   // width of each tab cell
    float arrowSize = 28.0f;  // width of left/right arrow buttons

    std::function<void(int)> onTabChanged;

    // Programmatic tab switch (fires callback)
    void setActiveTab(int tab);
};

} // namespace fate
