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

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Dark background
    Color bg = (style.backgroundColor.a > 0.0f)
             ? style.backgroundColor
             : Color{0.03f, 0.03f, 0.03f, 0.92f};
    bg.a *= style.opacity;
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

    // Text overlay: "EXP XX.XXX %  current_xp"
    char buf[64];
    float pct = ratio * 100.0f;
    snprintf(buf, sizeof(buf), "EXP %.3f %%  %.0f", pct, xp);
    float fontSize = scaledFont(9.0f);
    Vec2 ts = sdf.measure(buf, fontSize);
    float tx = rect.x + (rect.w - ts.x) * 0.5f;
    float ty = rect.y + (rect.h - ts.y) * 0.5f;

    // Shadow for readability
    float shadowOff = 1.0f * layoutScale_;
    Color shadow = {0.0f, 0.0f, 0.0f, 0.9f};
    sdf.drawScreen(batch, buf, {tx + shadowOff, ty + shadowOff}, fontSize, shadow, d + 0.15f);

    Color white = (style.textColor.a > 0.0f)
               ? style.textColor
               : Color{1.0f, 1.0f, 1.0f, 0.95f};
    sdf.drawScreen(batch, buf, {tx, ty}, fontSize, white, d + 0.2f);

    renderChildren(batch, sdf);
}

} // namespace fate
