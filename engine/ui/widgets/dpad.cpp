#include "engine/ui/widgets/dpad.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cmath>

namespace fate {

DPad::DPad(const std::string& id) : UINode(id, "dpad") {}

Direction DPad::directionFromLocal(const Vec2& localPos) const {
    // Center of the widget in local space
    float cx = dpadSize * 0.5f;
    float cy = dpadSize * 0.5f;

    float dx = localPos.x - cx;
    float dy = localPos.y - cy;  // positive = downward on screen

    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < deadZoneRadius) return Direction::None;

    // Determine cardinal direction by comparing absolute components
    if (std::abs(dx) >= std::abs(dy)) {
        return (dx > 0.0f) ? Direction::Right : Direction::Left;
    } else {
        // dy positive = down on screen, dy negative = up on screen
        return (dy < 0.0f) ? Direction::Up : Direction::Down;
    }
}

bool DPad::onPress(const Vec2& localPos) {
    if (!enabled_) return false;
    Direction dir = directionFromLocal(localPos);
    activeDirection = dir;
    if (onDirectionChange) onDirectionChange(id_);
    return true;
}

void DPad::onRelease(const Vec2&) {
    activeDirection = Direction::None;
    if (onDirectionChange) onDirectionChange(id_);
}

void DPad::render(SpriteBatch& batch, SDFText& text) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);
    float cx = rect.x + rect.w * 0.5f;
    float cy = rect.y + rect.h * 0.5f;

    Color base = {0.3f, 0.3f, 0.3f, opacity};
    Color active = {0.6f, 0.6f, 0.8f, opacity};

    float armW = dpadSize * 0.35f;
    float armH = dpadSize * 0.9f;

    // Horizontal arm
    batch.drawRect({cx, cy}, {armH, armW}, base, d);
    // Vertical arm
    batch.drawRect({cx, cy}, {armW, armH}, base, d);

    // Highlight active direction arm
    float armLen = dpadSize * 0.45f;
    float armHalf = armW * 0.5f;
    float offset = dpadSize * 0.225f;
    switch (activeDirection) {
        case Direction::Up:
            batch.drawRect({cx, cy - offset}, {armW, armLen}, active, d + 0.1f);
            break;
        case Direction::Down:
            batch.drawRect({cx, cy + offset}, {armW, armLen}, active, d + 0.1f);
            break;
        case Direction::Left:
            batch.drawRect({cx - offset, cy}, {armLen, armW}, active, d + 0.1f);
            break;
        case Direction::Right:
            batch.drawRect({cx + offset, cy}, {armLen, armW}, active, d + 0.1f);
            break;
        default:
            break;
    }

    // Outer ring
    float radius = dpadSize * 0.5f;
    Color ring = {0.5f, 0.5f, 0.5f, opacity * 0.7f};
    batch.drawCircle({cx, cy}, radius, ring, d - 0.1f, 24);

    renderChildren(batch, text);

    (void)armHalf; // suppress unused warning
}

} // namespace fate
