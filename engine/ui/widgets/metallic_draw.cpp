// engine/ui/widgets/metallic_draw.cpp
#include "metallic_draw.h"
#include <cmath>

namespace fate {

static constexpr int RING_COUNT = 6;
static const Color EDGE_COLOR  {0.65f, 0.55f, 0.38f, 1.0f};
static const Color CENTER_COLOR{0.85f, 0.78f, 0.62f, 1.0f};
static const Color BORDER_COLOR{0.45f, 0.35f, 0.22f, 1.0f};
static const Color HIGHLIGHT   {0.95f, 0.90f, 0.78f, 0.4f};
static const Color SHADOW      {0.50f, 0.40f, 0.28f, 0.3f};

Color metallicLerp(const Color& a, const Color& b, float t) {
    return {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t
    };
}

int metallicRingCount() { return RING_COUNT; }

void drawMetallicCircle(SpriteBatch& batch, Vec2 center, float radius,
                        float depth, float opacity) {
    constexpr int seg = 24;

    // 1. Outer border ring
    Color border = BORDER_COLOR;
    border.a *= opacity;
    batch.drawRing(center, radius, 3.0f, border, depth - 0.01f, seg);

    // 2. Concentric filled rings (outermost first)
    float radii[RING_COUNT] = {1.0f, 0.85f, 0.70f, 0.55f, 0.40f, 0.25f};
    for (int i = 0; i < RING_COUNT; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(RING_COUNT - 1);
        Color c = metallicLerp(EDGE_COLOR, CENTER_COLOR, t);
        c.a *= opacity;
        batch.drawCircle(center, radius * radii[i], c, depth, seg);
    }

    // 3. Highlight crescent at ~11 o'clock
    float hlAngle = 2.094f; // ~120 degrees (11 o'clock in radians from 3 o'clock)
    Vec2 hlOff{std::cos(hlAngle) * radius * 0.2f, -std::sin(hlAngle) * radius * 0.2f};
    Color hl = HIGHLIGHT;
    hl.a *= opacity;
    batch.drawArc(Vec2{center.x + hlOff.x, center.y + hlOff.y},
                  radius * 0.55f, 1.57f, 2.62f, hl, depth + 0.02f, seg);

    // 4. Lower shadow band at bottom
    Color sh = SHADOW;
    sh.a *= opacity;
    // Bottom arc: in screen space (y-down), bottom is at PI/2. Cover ~100 degrees around bottom.
    batch.drawArc(center, radius * 0.95f, 0.7f, 2.44f, sh, depth + 0.01f, seg);
}

} // namespace fate
