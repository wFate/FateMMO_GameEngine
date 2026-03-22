#include "engine/ui/widgets/tooltip.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"

namespace fate {

Tooltip::Tooltip(const std::string& id)
    : UINode(id, "tooltip") {}

void Tooltip::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    const float depth = 900.0f;

    // Dark background
    Color bg{0.05f, 0.05f, 0.05f, 0.90f};
    batch.drawRect(
        {rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
        {rect.w, rect.h},
        bg,
        depth);

    // Border
    Color bc{0.6f, 0.6f, 0.6f, 1.0f};
    float bw = 1.0f;
    float d = depth + 0.1f;

    // Top (full width)
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f},
                   {rect.w, bw}, bc, d);
    // Bottom (full width)
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f},
                   {rect.w, bw}, bc, d);
    // Left (inner height)
    float innerH = rect.h - bw * 2.0f;
    batch.drawRect({rect.x + bw * 0.5f, rect.y + rect.h * 0.5f},
                   {bw, innerH}, bc, d);
    // Right (inner height)
    batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f},
                   {bw, innerH}, bc, d);

    // Text
    if (!tooltipText.empty()) {
        Color tc{1.0f, 1.0f, 1.0f, 1.0f};
        sdf.drawScreen(batch, tooltipText,
            {rect.x + 6.0f, rect.y + 4.0f},
            12.0f, tc, depth + 0.2f);
    }
}

} // namespace fate
