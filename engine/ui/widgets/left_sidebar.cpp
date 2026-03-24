#include "engine/ui/widgets/left_sidebar.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"

namespace fate {

LeftSidebar::LeftSidebar(const std::string& id)
    : UINode(id, "left_sidebar") {}

void LeftSidebar::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);
    float s = layoutScale_;
    float bs = buttonSize * s;
    float sp = spacing * s;
    float r = bs * 0.5f;

    // Background strip — dark semi-transparent
    Color stripBg = {0.08f, 0.08f, 0.12f, 0.88f};
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, stripBg, d);

    // Border on the right edge of the strip
    Color stripBorder = {0.4f, 0.35f, 0.2f, 0.8f};
    batch.drawRect({rect.x + rect.w - 1.0f, rect.y + rect.h * 0.5f},
                   {2.0f, rect.h}, stripBorder, d + 0.05f);

    float labelFontSize = scaledFont(7.0f);

    for (int i = 0; i < static_cast<int>(panelLabels.size()); ++i) {
        float cx = rect.x + rect.w * 0.5f;
        float cy = rect.y + r + static_cast<float>(i) * (bs + sp);

        bool isActive = (!activePanel.empty() && panelLabels[i] == activePanel);

        // Button circle background
        Color bg = isActive
            ? Color{0.75f, 0.60f, 0.15f, 0.95f}   // accent gold for active
            : Color{0.15f, 0.15f, 0.22f, 0.88f};   // dark for inactive
        batch.drawCircle({cx, cy}, r, bg, d + 0.1f, 20);

        // Border ring — brighter on active
        Color border = isActive
            ? Color{1.0f, 0.85f, 0.3f, 1.0f}
            : Color{0.45f, 0.45f, 0.65f, 0.85f};
        batch.drawRing({cx, cy}, r, 2.0f, border, d + 0.2f, 20);

        // Label text centered below circle
        const std::string& lbl = panelLabels[i];
        Vec2 ts = sdf.measure(lbl.c_str(), labelFontSize);
        Color textColor = isActive
            ? Color{1.0f, 1.0f, 0.9f, 1.0f}
            : Color{0.8f, 0.8f, 0.75f, 0.9f};
        sdf.drawScreen(batch, lbl.c_str(),
            {cx - ts.x * 0.5f, cy + r + 2.0f},
            labelFontSize, textColor, d + 0.3f);
    }

    renderChildren(batch, sdf);
}

bool LeftSidebar::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    float s = layoutScale_;
    float bs = buttonSize * s;
    float sp = spacing * s;
    float r = bs * 0.5f;
    float cx = computedRect_.w * 0.5f;

    for (int i = 0; i < static_cast<int>(panelLabels.size()); ++i) {
        float cy = r + static_cast<float>(i) * (bs + sp);
        float dx = localPos.x - cx;
        float dy = localPos.y - cy;
        float dist2 = dx * dx + dy * dy;
        if (dist2 <= r * r) {
            if (onPanelSelect) onPanelSelect(i, panelLabels[i]);
            return true;
        }
    }
    return false;
}

} // namespace fate
