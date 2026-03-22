#include "engine/ui/widgets/panel.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"

namespace fate {

Panel::Panel(const std::string& id)
    : UINode(id, "panel") {}

void Panel::render(SpriteBatch& batch, SDFText& text) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;

    // Draw background fill
    if (style.backgroundColor.a > 0.0f) {
        Color bg = style.backgroundColor;
        bg.a *= style.opacity;
        batch.drawRect(
            {rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
            {rect.w, rect.h},
            bg,
            static_cast<float>(zOrder_));
    }

    // Draw border (4 non-overlapping edge rects — avoids double-blend at corners)
    if (style.borderWidth > 0.0f && style.borderColor.a > 0.0f) {
        Color bc = style.borderColor;
        bc.a *= style.opacity;
        float bw = style.borderWidth;
        float d = static_cast<float>(zOrder_) + 0.1f;

        // Top (full width)
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f},
                       {rect.w, bw}, bc, d);
        // Bottom (full width)
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f},
                       {rect.w, bw}, bc, d);
        // Left (between top and bottom to avoid corner overlap)
        float innerH = rect.h - bw * 2.0f;
        batch.drawRect({rect.x + bw * 0.5f, rect.y + rect.h * 0.5f},
                       {bw, innerH}, bc, d);
        // Right (between top and bottom)
        batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f},
                       {bw, innerH}, bc, d);
    }

    // Draw title if present
    if (!title.empty()) {
        Color tc = style.textColor;
        tc.a *= style.opacity;
        float fontSize = style.fontSize > 0 ? style.fontSize : 14.0f;
        text.drawScreen(batch, title,
            {rect.x + 10.0f, rect.y + 4.0f},
            fontSize, tc, static_cast<float>(zOrder_) + 0.2f);
    }

    renderChildren(batch, text);
}

bool Panel::onPress(const Vec2& localPos) {
    if (!enabled_ || !draggable) return false;
    isDragging_ = true;
    dragOffset_ = localPos;
    return true;
}

void Panel::onRelease(const Vec2&) {
    isDragging_ = false;
}

} // namespace fate
