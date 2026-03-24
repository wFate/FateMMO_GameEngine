// engine/ui/widgets/metallic_draw.h
#pragma once
#include "engine/render/sprite_batch.h"

namespace fate {

// Color interpolation helper (exposed for testing)
Color metallicLerp(const Color& a, const Color& b, float t);

// Number of concentric rings used (exposed for testing)
int metallicRingCount();

// Draw a gold metallic filled circle with embossed look.
// All metallic UI buttons use this. When texture sprites are ready,
// replace the body with a single batch.draw() call.
void drawMetallicCircle(SpriteBatch& batch, Vec2 center, float radius,
                        float depth, float opacity = 1.0f);

} // namespace fate
