#include "engine/ui/widgets/exp_bar.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <algorithm>

namespace fate {

EXPBar::EXPBar(const std::string& id)
    : UINode(id, "exp_bar") {}

void EXPBar::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Dark background
    Color bg = {0.05f, 0.05f, 0.05f, 0.85f};
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, bg, d);

    // Gold XP fill
    float ratio = (xpToLevel > 0.0f) ? std::clamp(xp / xpToLevel, 0.0f, 1.0f) : 0.0f;
    if (ratio > 0.0f) {
        Color fillColor = {0.8f, 0.65f, 0.1f, 1.0f};
        float fw = rect.w * ratio;
        batch.drawRect({rect.x + fw * 0.5f, rect.y + rect.h * 0.5f},
                       {fw, rect.h}, fillColor, d + 0.1f);
    }

    // Text overlay: "EXP XX.X%  current_xp"
    char buf[64];
    float pct = ratio * 100.0f;
    snprintf(buf, sizeof(buf), "EXP %.1f%%  %.0f", pct, xp);
    float fontSize = 9.0f;
    Vec2 ts = sdf.measure(buf, fontSize);
    Color white = {1.0f, 1.0f, 1.0f, 0.9f};
    sdf.drawScreen(batch, buf,
        {rect.x + (rect.w - ts.x) * 0.5f, rect.y + (rect.h - ts.y) * 0.5f},
        fontSize, white, d + 0.2f);

    renderChildren(batch, sdf);
}

} // namespace fate
