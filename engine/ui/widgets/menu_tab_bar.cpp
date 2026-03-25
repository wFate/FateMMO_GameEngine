#include "engine/ui/widgets/menu_tab_bar.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <algorithm>

namespace fate {

MenuTabBar::MenuTabBar(const std::string& id) : UINode(id, "menu_tab_bar") {}

void MenuTabBar::setActiveTab(int tab) {
    int count = static_cast<int>(tabLabels.size());
    if (count == 0) return;
    tab = ((tab % count) + count) % count; // wrap
    if (tab == activeTab) return;
    activeTab = tab;
    if (onTabChanged) onTabChanged(activeTab);
}

bool MenuTabBar::onPress(const Vec2& localPos) {
    if (!enabled_) return false;
    int count = static_cast<int>(tabLabels.size());
    if (count == 0) return false;

    float s = layoutScale_;
    float h = computedRect_.h;
    float scaledArrow = arrowSize * s;
    float scaledTab = tabSize * s;

    float x = localPos.x;
    float y = localPos.y;

    // Must be within vertical bounds
    if (y < 0.0f || y > h) return false;

    // Left arrow region
    if (x >= 0.0f && x < scaledArrow) {
        setActiveTab(activeTab - 1);
        return true;
    }

    // Tab regions
    float tabsStartX = scaledArrow;
    float tabsEndX = tabsStartX + scaledTab * count;
    if (x >= tabsStartX && x < tabsEndX) {
        int idx = static_cast<int>((x - tabsStartX) / scaledTab);
        idx = std::clamp(idx, 0, count - 1);
        setActiveTab(idx);
        return true;
    }

    // Right arrow region
    if (x >= tabsEndX && x < tabsEndX + scaledArrow) {
        setActiveTab(activeTab + 1);
        return true;
    }

    return false;
}

void MenuTabBar::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);
    float s = layoutScale_;
    int count = static_cast<int>(tabLabels.size());
    if (count == 0) return;

    float scaledArrow = arrowSize * s;
    float scaledTab = tabSize * s;
    float h = rect.h;

    // Colors matching TWOM gold metallic palette
    Color activeTabBg   = {0.784f, 0.659f, 0.196f, 1.0f};  // #C8A832
    Color inactiveTabBg = {0.290f, 0.247f, 0.184f, 1.0f};   // #4A3F2F
    Color arrowBg       = {0.220f, 0.188f, 0.140f, 1.0f};   // slightly darker
    Color borderColor   = {0.35f, 0.26f, 0.14f, 1.0f};      // brown border
    Color activeText    = {0.15f, 0.10f, 0.05f, 1.0f};      // dark text on gold
    Color inactiveText  = {0.85f, 0.78f, 0.60f, 1.0f};      // light text on dark
    Color arrowText     = {0.90f, 0.82f, 0.55f, 1.0f};      // gold arrow text

    float borderW = 1.0f * s;

    // Full bar border background
    float totalW = scaledArrow * 2.0f + scaledTab * count;
    float barCx = rect.x + scaledArrow + scaledTab * count * 0.5f;
    float barCy = rect.y + h * 0.5f;
    batch.drawRect({barCx, barCy}, {totalW + borderW * 2.0f, h + borderW * 2.0f},
                   borderColor, d - 0.1f);

    // Left arrow button
    {
        float cx = rect.x + scaledArrow * 0.5f;
        float cy = rect.y + h * 0.5f;
        batch.drawRect({cx, cy}, {scaledArrow, h}, arrowBg, d);

        float fontSize = scaledFont(12.0f);
        const char* arrow = "<";
        Vec2 ts = sdf.measure(arrow, fontSize);
        sdf.drawScreen(batch, arrow,
            {cx - ts.x * 0.5f, cy - ts.y * 0.5f},
            fontSize, arrowText, d + 0.2f);
    }

    // Tabs
    for (int i = 0; i < count; ++i) {
        float tabX = rect.x + scaledArrow + scaledTab * i;
        float cx = tabX + scaledTab * 0.5f;
        float cy = rect.y + h * 0.5f;

        bool active = (i == activeTab);
        Color bg = active ? activeTabBg : inactiveTabBg;
        Color fg = active ? activeText : inactiveText;

        batch.drawRect({cx, cy}, {scaledTab, h}, bg, d);

        // 1px highlight on top edge of active tab
        if (active) {
            Color highlight = {0.95f, 0.88f, 0.45f, 0.6f};
            batch.drawRect({cx, rect.y + borderW * 0.5f},
                           {scaledTab, borderW}, highlight, d + 0.05f);
        }

        // Tab label
        float fontSize = scaledFont(9.0f);
        const char* label = (i < static_cast<int>(tabLabels.size())) ? tabLabels[i].c_str() : "";
        Vec2 ts = sdf.measure(label, fontSize);
        sdf.drawScreen(batch, label,
            {cx - ts.x * 0.5f, cy - ts.y * 0.5f},
            fontSize, fg, d + 0.2f);
    }

    // Right arrow button
    {
        float cx = rect.x + scaledArrow + scaledTab * count + scaledArrow * 0.5f;
        float cy = rect.y + h * 0.5f;
        batch.drawRect({cx, cy}, {scaledArrow, h}, arrowBg, d);

        float fontSize = scaledFont(12.0f);
        const char* arrow = ">";
        Vec2 ts = sdf.measure(arrow, fontSize);
        sdf.drawScreen(batch, arrow,
            {cx - ts.x * 0.5f, cy - ts.y * 0.5f},
            fontSize, arrowText, d + 0.2f);
    }

    renderChildren(batch, sdf);
}

} // namespace fate
