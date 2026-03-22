#include "engine/ui/widgets/button.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"

namespace fate {

Button::Button(const std::string& id) : UINode(id, "button") {}

bool Button::onPress(const Vec2&) {
    if (!enabled_) return false;
    pressed_ = true;
    return true;
}

void Button::onRelease(const Vec2&) {
    if (!enabled_) return;
    if (pressed_ && onClick) onClick(id_);
    pressed_ = false;
}

void Button::onHoverEnter() { UINode::onHoverEnter(); }
void Button::onHoverExit() { UINode::onHoverExit(); pressed_ = false; }

void Button::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;

    Color bg = style.backgroundColor;
    if (!enabled_ && style.disabledColor.a > 0) bg = style.disabledColor;
    else if (pressed_ && style.pressedColor.a > 0) bg = style.pressedColor;
    else if (hovered_ && style.hoverColor.a > 0) bg = style.hoverColor;
    bg.a *= style.opacity;

    if (bg.a > 0.0f)
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                       {rect.w, rect.h}, bg, static_cast<float>(zOrder_));

    if (style.borderWidth > 0.0f && style.borderColor.a > 0.0f) {
        Color bc = style.borderColor;
        bc.a *= style.opacity;
        float bw = style.borderWidth;
        float d = static_cast<float>(zOrder_) + 0.1f;
        float innerH = rect.h - bw * 2.0f;
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f}, {rect.w, bw}, bc, d);
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f}, {rect.w, bw}, bc, d);
        batch.drawRect({rect.x + bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bc, d);
        batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bc, d);
    }

    if (!text.empty()) {
        Color tc = style.textColor;
        tc.a *= style.opacity;
        float fontSize = style.fontSize > 0 ? style.fontSize : 14.0f;
        Vec2 textSize = sdf.measure(text, fontSize);
        float tx = rect.x + (rect.w - textSize.x) * 0.5f;
        float ty = rect.y + (rect.h - textSize.y) * 0.5f;
        sdf.drawScreen(batch, text, {tx, ty}, fontSize, tc,
                       static_cast<float>(zOrder_) + 0.2f);
    }

    renderChildren(batch, sdf);
}

} // namespace fate
