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
    std::vector<std::string> tabLabels = {"STS", "INV", "SKL", "GLD", "SOC", "SET", "SHP", "PET", "CRF", "COL", "COS"};
    float tabSize  = 50.0f;   // width of each tab cell
    float arrowSize = 28.0f;  // width of left/right arrow buttons

    // Font sizes (base, before layoutScale_)
    float tabFontSize   = 9.0f;
    float arrowFontSize = 12.0f;

    // Border
    float borderWidth = 1.0f;

    // Colors
    Color activeTabBg   = {0.784f, 0.659f, 0.196f, 1.0f};  // gold
    Color inactiveTabBg = {0.290f, 0.247f, 0.184f, 1.0f};   // dark brown
    Color arrowBg       = {0.220f, 0.188f, 0.140f, 1.0f};   // darker brown
    Color borderColor   = {0.35f, 0.26f, 0.14f, 1.0f};      // brown border
    Color activeTextColor   = {0.15f, 0.10f, 0.05f, 1.0f};  // dark on gold
    Color inactiveTextColor = {0.85f, 0.78f, 0.60f, 1.0f};  // light on dark
    Color arrowTextColor    = {0.90f, 0.82f, 0.55f, 1.0f};  // gold arrow text
    Color highlightColor    = {0.95f, 0.88f, 0.45f, 0.6f};  // active tab top edge

    std::function<void(int)> onTabChanged;

    // Programmatic tab switch (fires callback)
    void setActiveTab(int tab);
};

} // namespace fate
