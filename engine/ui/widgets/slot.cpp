#include "engine/ui/widgets/slot.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>

namespace fate {

Slot::Slot(const std::string& id) : UINode(id, "slot") {}

DragPayload Slot::createDragPayload() const {
    DragPayload p;
    p.type = slotType.empty() ? "item" : slotType;
    p.data = itemId;
    p.sourceId = id_;
    p.active = true;
    return p;
}

bool Slot::onPress(const Vec2&) {
    if (onSlotClick) onSlotClick(id_);
    return true;
}

bool Slot::acceptsDrop(const DragPayload& payload) const {
    if (acceptsDragType.empty()) return true;
    return payload.type == acceptsDragType;
}

void Slot::onDrop(const DragPayload& payload) {
    itemId = payload.data;
}

void Slot::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Slot background
    Color bg = style.backgroundColor.a > 0 ? style.backgroundColor : Color(0.12f, 0.12f, 0.16f, 1.0f);
    bg.a *= style.opacity;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, bg, d);

    // Border
    Color bc = hovered_
        ? ((style.hoverColor.a > 0.0f) ? style.hoverColor : Color{0.7f, 0.6f, 0.3f, 1.0f})
        : (style.borderColor.a > 0 ? style.borderColor : Color(0.35f, 0.3f, 0.2f, 0.8f));
    bc.a *= style.opacity;
    float bw = 1.0f;
    float innerH = rect.h - bw * 2.0f;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f}, {rect.w, bw}, bc, d + 0.1f);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f}, {rect.w, bw}, bc, d + 0.1f);
    batch.drawRect({rect.x + bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bc, d + 0.1f);
    batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bc, d + 0.1f);

    // Quantity badge
    if (quantity > 1) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", quantity);
        float fontSize = 10.0f;
        sdf.drawScreen(batch, buf, {rect.x + rect.w - 16.0f, rect.y + rect.h - 14.0f},
                       fontSize, Color::white(), d + 0.3f);
    }

    renderChildren(batch, sdf);
}

} // namespace fate
