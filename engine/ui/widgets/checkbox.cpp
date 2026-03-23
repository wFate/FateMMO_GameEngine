#include "engine/ui/widgets/checkbox.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"

namespace fate {

Checkbox::Checkbox(const std::string& id) : UINode(id, "checkbox") {}

bool Checkbox::onPress(const Vec2&) {
    checked = !checked;
    if (onToggle) onToggle(id_);
    return true;
}

void Checkbox::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);
    float op = style.opacity;

    // Box position: vertically centered within the widget
    float boxY = rect.y + (rect.h - boxSize) * 0.5f;

    // Box background
    Color bg(0.12f, 0.12f, 0.16f, 1.0f);
    bg.a *= op;
    batch.drawRect({rect.x + boxSize * 0.5f, boxY + boxSize * 0.5f},
                   {boxSize, boxSize}, bg, d);

    // Box border (4 non-overlapping edge rects, same pattern as Slot)
    Color bc = hovered_ ? Color(0.7f, 0.6f, 0.3f, 1.0f) : Color(0.35f, 0.3f, 0.2f, 0.8f);
    bc.a *= op;
    float bw = 1.0f;
    float innerH = boxSize - bw * 2.0f;
    // Top edge
    batch.drawRect({rect.x + boxSize * 0.5f, boxY + bw * 0.5f},
                   {boxSize, bw}, bc, d + 0.1f);
    // Bottom edge
    batch.drawRect({rect.x + boxSize * 0.5f, boxY + boxSize - bw * 0.5f},
                   {boxSize, bw}, bc, d + 0.1f);
    // Left edge
    batch.drawRect({rect.x + bw * 0.5f, boxY + boxSize * 0.5f},
                   {bw, innerH}, bc, d + 0.1f);
    // Right edge
    batch.drawRect({rect.x + boxSize - bw * 0.5f, boxY + boxSize * 0.5f},
                   {bw, innerH}, bc, d + 0.1f);

    // Check mark: filled inner square when checked (gold)
    if (checked) {
        float inset = 3.0f;
        float innerSize = boxSize - inset * 2.0f;
        Color checkColor(0.8f, 0.7f, 0.3f, 1.0f);
        checkColor.a *= op;
        batch.drawRect({rect.x + boxSize * 0.5f, boxY + boxSize * 0.5f},
                       {innerSize, innerSize}, checkColor, d + 0.2f);
    }

    // Label text to the right of the box
    if (!label.empty()) {
        Color tc = style.textColor;
        tc.a *= op;
        float fontSize = style.fontSize > 0 ? style.fontSize : 14.0f;
        Vec2 textSize = sdf.measure(label, fontSize);
        float tx = rect.x + boxSize + spacing;
        float ty = rect.y + (rect.h - textSize.y) * 0.5f;
        sdf.drawScreen(batch, label, {tx, ty}, fontSize, tc, d + 0.2f);
    }

    renderChildren(batch, sdf);
}

} // namespace fate
