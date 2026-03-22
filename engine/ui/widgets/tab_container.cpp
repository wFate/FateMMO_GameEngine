#include "engine/ui/widgets/tab_container.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"

namespace fate {

TabContainer::TabContainer(const std::string& id) : UINode(id, "tab_container") {}

void TabContainer::addTab(const std::string& label, std::unique_ptr<UINode> content) {
    tabLabels_.push_back(label);
    content->setVisible(static_cast<int>(tabLabels_.size()) - 1 == activeTab);
    addChild(std::move(content));
}

bool TabContainer::onPress(const Vec2& localPos) {
    if (localPos.y > tabHeight) return false;  // not in tab bar

    // Determine which tab was clicked
    float tabWidth = computedRect_.w / std::max(1, static_cast<int>(tabLabels_.size()));
    int clickedTab = static_cast<int>(localPos.x / tabWidth);
    if (clickedTab >= 0 && clickedTab < static_cast<int>(tabLabels_.size())) {
        // Hide old tab content, show new
        if (activeTab >= 0 && activeTab < static_cast<int>(children_.size()))
            children_[activeTab]->setVisible(false);
        activeTab = clickedTab;
        if (activeTab < static_cast<int>(children_.size()))
            children_[activeTab]->setVisible(true);
    }
    return true;
}

void TabContainer::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);
    int tabCount = static_cast<int>(tabLabels_.size());
    if (tabCount == 0) { renderChildren(batch, sdf); return; }

    float tabWidth = rect.w / tabCount;
    float fontSize = style.fontSize > 0 ? style.fontSize : 13.0f;

    // Draw tab bar
    for (int i = 0; i < tabCount; i++) {
        float tx = rect.x + i * tabWidth;
        Color tabBg = (i == activeTab) ? Color(0.15f, 0.13f, 0.1f, 0.95f)
                                       : Color(0.08f, 0.08f, 0.1f, 0.8f);
        tabBg.a *= style.opacity;
        batch.drawRect({tx + tabWidth * 0.5f, rect.y + tabHeight * 0.5f},
                       {tabWidth, tabHeight}, tabBg, d);

        // Tab label
        Color tc = (i == activeTab) ? Color(1.0f, 0.9f, 0.7f, 1.0f) : Color(0.6f, 0.6f, 0.55f, 1.0f);
        tc.a *= style.opacity;
        Vec2 ts = sdf.measure(tabLabels_[i], fontSize);
        sdf.drawScreen(batch, tabLabels_[i],
            {tx + (tabWidth - ts.x) * 0.5f, rect.y + (tabHeight - ts.y) * 0.5f},
            fontSize, tc, d + 0.1f);
    }

    // Active tab underline
    float ax = rect.x + activeTab * tabWidth;
    batch.drawRect({ax + tabWidth * 0.5f, rect.y + tabHeight - 1.0f},
                   {tabWidth, 2.0f}, Color(0.8f, 0.6f, 0.2f, style.opacity), d + 0.2f);

    // Render only active tab content
    if (activeTab >= 0 && activeTab < static_cast<int>(children_.size())) {
        children_[activeTab]->render(batch, sdf);
    }
}

} // namespace fate
