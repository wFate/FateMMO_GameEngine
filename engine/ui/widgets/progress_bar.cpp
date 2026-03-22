#include "engine/ui/widgets/progress_bar.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <algorithm>
#include <cstdio>

namespace fate {

ProgressBar::ProgressBar(const std::string& id) : UINode(id, "progress_bar") {}

float ProgressBar::fillRatio() const {
    if (maxValue <= 0.0f) return 0.0f;
    return std::clamp(value / maxValue, 0.0f, 1.0f);
}

void ProgressBar::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);
    float ratio = fillRatio();

    // Background
    Color bg = style.backgroundColor;
    bg.a *= style.opacity;
    if (bg.a > 0.0f)
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                       {rect.w, rect.h}, bg, d);

    // Fill
    if (ratio > 0.0f) {
        Color fc = fillColor;
        fc.a *= style.opacity;
        float fw, fh, fx, fy;
        switch (direction) {
            case BarDirection::LeftToRight:
                fw = rect.w * ratio; fh = rect.h;
                fx = rect.x + fw * 0.5f; fy = rect.y + rect.h * 0.5f;
                break;
            case BarDirection::RightToLeft:
                fw = rect.w * ratio; fh = rect.h;
                fx = rect.x + rect.w - fw * 0.5f; fy = rect.y + rect.h * 0.5f;
                break;
            case BarDirection::BottomToTop:
                fw = rect.w; fh = rect.h * ratio;
                fx = rect.x + rect.w * 0.5f; fy = rect.y + rect.h - fh * 0.5f;
                break;
            case BarDirection::TopToBottom:
                fw = rect.w; fh = rect.h * ratio;
                fx = rect.x + rect.w * 0.5f; fy = rect.y + fh * 0.5f;
                break;
        }
        batch.drawRect({fx, fy}, {fw, fh}, fc, d + 0.1f);
    }

    // Border
    if (style.borderWidth > 0.0f && style.borderColor.a > 0.0f) {
        Color bc = style.borderColor;
        bc.a *= style.opacity;
        float bw = style.borderWidth;
        float bd = d + 0.2f;
        float innerH = rect.h - bw * 2.0f;
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f}, {rect.w, bw}, bc, bd);
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f}, {rect.w, bw}, bc, bd);
        batch.drawRect({rect.x + bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bc, bd);
        batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bc, bd);
    }

    // Text overlay
    if (showText) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.0f/%.0f", value, maxValue);
        float fontSize = style.fontSize > 0 ? style.fontSize : 12.0f;
        Vec2 ts = sdf.measure(buf, fontSize);
        Color tc = style.textColor;
        tc.a *= style.opacity;
        sdf.drawScreen(batch, buf,
            {rect.x + (rect.w - ts.x) * 0.5f, rect.y + (rect.h - ts.y) * 0.5f},
            fontSize, tc, d + 0.3f);
    }

    renderChildren(batch, sdf);
}

} // namespace fate
