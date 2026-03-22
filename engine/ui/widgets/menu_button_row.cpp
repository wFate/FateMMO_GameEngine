#include "engine/ui/widgets/menu_button_row.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"

namespace fate {

MenuButtonRow::MenuButtonRow(const std::string& id)
    : UINode(id, "menu_button_row") {}

void MenuButtonRow::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);
    float r = buttonSize * 0.5f;

    float labelFontSize = 8.0f;

    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
        float cx = rect.x + r + static_cast<float>(i) * (buttonSize + spacing);
        float cy = rect.y + r;

        // Button circle background
        Color bg = {0.18f, 0.18f, 0.25f, 0.88f};
        batch.drawCircle({cx, cy}, r, bg, d, 20);

        // Border ring
        Color border = {0.5f, 0.5f, 0.7f, 0.9f};
        batch.drawRing({cx, cy}, r, 2.0f, border, d + 0.1f, 20);

        // Label text centered below circle
        const std::string& lbl = labels[i];
        Vec2 ts = sdf.measure(lbl.c_str(), labelFontSize);
        Color white = {1.0f, 1.0f, 1.0f, 0.9f};
        sdf.drawScreen(batch, lbl.c_str(),
            {cx - ts.x * 0.5f, cy + r + 2.0f},
            labelFontSize, white, d + 0.2f);
    }

    renderChildren(batch, sdf);
}

bool MenuButtonRow::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
        float cx = buttonSize * 0.5f + static_cast<float>(i) * (buttonSize + spacing);
        float cy = buttonSize * 0.5f;
        float dx = localPos.x - cx;
        float dy = localPos.y - cy;
        float dist2 = dx * dx + dy * dy;
        float r = buttonSize * 0.5f;
        if (dist2 <= r * r) {
            if (onButtonClick) onButtonClick(i, labels[i]);
            return true;
        }
    }
    return false;
}

} // namespace fate
