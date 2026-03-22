#include "engine/ui/widgets/label.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"

namespace fate {

Label::Label(const std::string& id)
    : UINode(id, "label") {}

void Label::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_ || text.empty()) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;

    Color tc = style.textColor;
    tc.a *= style.opacity;
    float fontSize = style.fontSize > 0 ? style.fontSize : 14.0f;

    Vec2 textSize = sdf.measure(text, fontSize);
    float tx = rect.x;

    switch (align) {
        case TextAlign::Left:
            tx = rect.x;
            break;
        case TextAlign::Center:
            tx = rect.x + (rect.w - textSize.x) * 0.5f;
            break;
        case TextAlign::Right:
            tx = rect.x + rect.w - textSize.x;
            break;
    }

    float ty = rect.y + (rect.h - textSize.y) * 0.5f;

    sdf.drawScreen(batch, text, {tx, ty}, fontSize, tc,
                   static_cast<float>(zOrder_) + 0.1f);

    renderChildren(batch, sdf);
}

} // namespace fate
