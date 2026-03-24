#include "engine/ui/widgets/dpad.h"
#include "engine/ui/widgets/metallic_draw.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cmath>

namespace fate {

DPad::DPad(const std::string& id) : UINode(id, "dpad") {}

Direction DPad::directionFromLocal(const Vec2& localPos) const {
    float s = layoutScale_;
    float ds = dpadSize * s;
    // Center of the widget in local space
    float cx = ds * 0.5f;
    float cy = ds * 0.5f;

    float dx = localPos.x - cx;
    float dy = localPos.y - cy;  // positive = downward on screen

    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < deadZoneRadius * s) return Direction::None;

    // Determine cardinal direction by comparing absolute components
    if (std::abs(dx) >= std::abs(dy)) {
        return (dx > 0.0f) ? Direction::Right : Direction::Left;
    } else {
        // dy positive = down on screen, dy negative = up on screen
        return (dy < 0.0f) ? Direction::Up : Direction::Down;
    }
}

bool DPad::isInsideCross(const Vec2& localPos) const {
    float s = layoutScale_;
    float ds = dpadSize * s;
    float cx = ds * 0.5f;
    float cy = ds * 0.5f;
    float armW = ds * 0.32f;   // must match render()
    float armH = ds * 0.85f;   // must match render()
    float halfW = armW * 0.5f;
    float halfH = armH * 0.5f;
    // Horizontal arm rect
    bool inHoriz = (localPos.x >= cx - halfH && localPos.x <= cx + halfH &&
                    localPos.y >= cy - halfW && localPos.y <= cy + halfW);
    // Vertical arm rect
    bool inVert  = (localPos.x >= cx - halfW && localPos.x <= cx + halfW &&
                    localPos.y >= cy - halfH && localPos.y <= cy + halfH);
    return inHoriz || inVert;
}

bool DPad::onPress(const Vec2& localPos) {
    if (!enabled_) return false;
    // Only consume clicks that land on the cross shape
    if (!isInsideCross(localPos)) return false;
    Direction dir = directionFromLocal(localPos);
    if (dir == Direction::None) return false; // dead zone — don't consume
    if (dir == activeDirection) return true;  // same direction — consume but skip callback
    activeDirection = dir;
    if (onDirectionChange) onDirectionChange(id_);
    return true;
}

void DPad::onRelease(const Vec2&) {
    activeDirection = Direction::None;
    if (onDirectionChange) onDirectionChange(id_);
}

void DPad::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);
    float cx = rect.x + rect.w * 0.5f;
    float cy = rect.y + rect.h * 0.5f;

    float s = layoutScale_;
    float ds = dpadSize * s;
    float radius = ds * 0.5f;

    // Gold metallic background circle
    drawMetallicCircle(batch, {cx, cy}, radius, d - 0.2f, 0.85f);

    // Cross arms — tan metallic tones
    Color armColor = {0.80f, 0.73f, 0.58f, 0.85f};
    Color activeColor = {1.0f, 0.90f, 0.55f, 0.85f};
    Color centerColor = {0.60f, 0.52f, 0.38f, 0.85f};

    float armW = ds * 0.32f;
    float armH = ds * 0.85f;

    // Draw cross arms
    batch.drawRect({cx, cy}, {armH, armW}, armColor, d);
    batch.drawRect({cx, cy}, {armW, armH}, armColor, d);

    // 1px highlight on top edge of each arm
    Color armHighlight = {0.90f, 0.85f, 0.72f, 0.5f};
    float halfArmW = armW * 0.5f;
    float halfArmH = armH * 0.5f;
    // Horizontal arm top edge
    batch.drawRect({cx, cy - halfArmW}, {armH, 1.0f}, armHighlight, d + 0.01f);
    // Vertical arm top edge
    batch.drawRect({cx, cy - halfArmH}, {armW, 1.0f}, armHighlight, d + 0.01f);

    // Center dead zone (darker square)
    float centerSz = ds * 0.22f;
    batch.drawRect({cx, cy}, {centerSz, centerSz}, centerColor, d + 0.05f);

    // Directional arrow triangles (drawn as small rects pointing outward)
    float arrowDist = ds * 0.32f;  // distance from center to arrow
    float arrowW = ds * 0.08f;     // arrow width
    float arrowH = ds * 0.12f;     // arrow height
    Color arrowColor = {0.95f, 0.92f, 0.85f, 0.85f};

    // Up arrow
    batch.drawRect({cx, cy - arrowDist}, {arrowW, arrowH}, arrowColor, d + 0.08f);
    // Down arrow
    batch.drawRect({cx, cy + arrowDist}, {arrowW, arrowH}, arrowColor, d + 0.08f);
    // Left arrow
    batch.drawRect({cx - arrowDist, cy}, {arrowH, arrowW}, arrowColor, d + 0.08f);
    // Right arrow
    batch.drawRect({cx + arrowDist, cy}, {arrowH, arrowW}, arrowColor, d + 0.08f);

    // Highlight active direction arm
    float armLen = ds * 0.42f;
    float offset = ds * 0.21f;
    switch (activeDirection) {
        case Direction::Up:
            batch.drawRect({cx, cy - offset}, {armW, armLen}, activeColor, d + 0.1f);
            break;
        case Direction::Down:
            batch.drawRect({cx, cy + offset}, {armW, armLen}, activeColor, d + 0.1f);
            break;
        case Direction::Left:
            batch.drawRect({cx - offset, cy}, {armLen, armW}, activeColor, d + 0.1f);
            break;
        case Direction::Right:
            batch.drawRect({cx + offset, cy}, {armLen, armW}, activeColor, d + 0.1f);
            break;
        default:
            break;
    }

    renderChildren(batch, sdf);
}

} // namespace fate
