#include "engine/ui/widgets/target_frame.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <algorithm>

namespace fate {

TargetFrame::TargetFrame(const std::string& id)
    : UINode(id, "target_frame") {
    visible_ = false;  // hidden until a target is selected
}

void TargetFrame::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Semi-transparent dark panel background
    Color panelBg = (style.backgroundColor.a > 0.0f)
                  ? style.backgroundColor
                  : Color{0.08f, 0.08f, 0.12f, 0.88f};
    panelBg.a *= style.opacity;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, panelBg, d);

    // 1px border
    Color borderColor = (style.borderColor.a > 0.0f)
                      ? style.borderColor
                      : Color{0.4f, 0.4f, 0.5f, 0.8f};
    float bw = 1.0f;
    float innerH = rect.h - bw * 2.0f;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f},
                   {rect.w, bw}, borderColor, d + 0.05f);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f},
                   {rect.w, bw}, borderColor, d + 0.05f);
    batch.drawRect({rect.x + bw * 0.5f, rect.y + rect.h * 0.5f},
                   {bw, innerH}, borderColor, d + 0.05f);
    batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f},
                   {bw, innerH}, borderColor, d + 0.05f);

    float fontSize = scaledFont(11.0f);
    Color white = (style.textColor.a > 0.0f)
               ? style.textColor
               : Color{1.0f, 1.0f, 1.0f, 1.0f};

    // Name text centered at top
    if (!targetName.empty()) {
        Vec2 ts = sdf.measure(targetName.c_str(), fontSize);
        sdf.drawScreen(batch, targetName.c_str(),
            {rect.x + (rect.w - ts.x) * 0.5f, rect.y + 4.0f * layoutScale_},
            fontSize, white, d + 0.1f);
    }

    // HP bar below name
    float barPad  = 6.0f * layoutScale_;
    float barH    = 12.0f * layoutScale_;
    float nameH   = fontSize + 6.0f * layoutScale_;
    float barY    = rect.y + nameH;
    float barW    = rect.w - barPad * 2.0f;

    // Bar background
    Color barBg = {0.1f, 0.1f, 0.1f, 0.85f};
    batch.drawRect({rect.x + rect.w * 0.5f, barY + barH * 0.5f},
                   {barW, barH}, barBg, d + 0.1f);

    // HP fill
    float hpRatio = (maxHp > 0.0f) ? std::clamp(hp / maxHp, 0.0f, 1.0f) : 0.0f;
    if (hpRatio > 0.0f) {
        Color hpColor = {0.75f, 0.15f, 0.15f, 1.0f};
        float fw = barW * hpRatio;
        float fx = rect.x + barPad + fw * 0.5f;
        batch.drawRect({fx, barY + barH * 0.5f}, {fw, barH}, hpColor, d + 0.2f);
    }

    // HP text overlay
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f/%.0f", hp, maxHp);
    float smallFont = scaledFont(9.0f);
    Vec2 ts = sdf.measure(buf, smallFont);
    sdf.drawScreen(batch, buf,
        {rect.x + (rect.w - ts.x) * 0.5f, barY + (barH - ts.y) * 0.5f},
        smallFont, white, d + 0.3f);

    renderChildren(batch, sdf);
}

} // namespace fate
