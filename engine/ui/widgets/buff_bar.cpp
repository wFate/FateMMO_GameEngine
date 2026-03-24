#include "engine/ui/widgets/buff_bar.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace fate {

static constexpr float PI = 3.14159265f;

BuffBar::BuffBar(const std::string& id) : UINode(id, "buff_bar") {}

Color BuffBar::colorForEffectType(uint8_t effectType) {
    // EffectType mapping:
    //  0=Bleed, 1=Burn, 2=Poison, 3=Slow, 4=ArmorShred, 5=HuntersMark  -> debuffs (red)
    //  6=AttackUp, 7=ArmorUp, 8=SpeedUp, 9=ManaRegenUp                  -> buffs (green)
    // 10=Shield, 11=Invulnerable, 12=StunImmune, 13=GuaranteedCrit      -> shields (blue)
    // 14=Transform, 15=Bewitched                                         -> utility (yellow)
    // 16=ExpGainUp                                                       -> buff (green)

    if (effectType <= 5) {
        // Debuffs: reddish
        return Color(0.75f, 0.18f, 0.18f, 0.9f);
    } else if (effectType <= 9) {
        // Buffs: greenish
        return Color(0.18f, 0.72f, 0.28f, 0.9f);
    } else if (effectType <= 13) {
        // Shields: bluish
        return Color(0.22f, 0.45f, 0.82f, 0.9f);
    } else if (effectType <= 15) {
        // Utility: yellowish
        return Color(0.82f, 0.72f, 0.22f, 0.9f);
    } else if (effectType == 16) {
        // ExpGainUp: greenish (buff)
        return Color(0.18f, 0.72f, 0.28f, 0.9f);
    }
    // Unknown: gray
    return Color(0.5f, 0.5f, 0.5f, 0.9f);
}

void BuffBar::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    int count = static_cast<int>(buffs.size());
    if (count > maxVisible) count = maxVisible;

    for (int i = 0; i < count; ++i) {
        const auto& buff = buffs[i];

        float ix = rect.x + static_cast<float>(i) * (iconSize + spacing) + iconSize * 0.5f;
        float iy = rect.y + rect.h * 0.5f;
        Vec2 center = {ix, iy};

        // Background fill colored by effect category
        Color bg = colorForEffectType(buff.effectType);
        batch.drawRect(center, {iconSize, iconSize}, bg, d);

        // 1px border (slightly brighter)
        Color border = {bg.r * 1.3f, bg.g * 1.3f, bg.b * 1.3f, 1.0f};
        // Clamp to 1.0
        if (border.r > 1.0f) border.r = 1.0f;
        if (border.g > 1.0f) border.g = 1.0f;
        if (border.b > 1.0f) border.b = 1.0f;

        float bw = 1.0f;
        float halfIcon = iconSize * 0.5f;
        float innerH = iconSize - bw * 2.0f;
        // Top edge
        batch.drawRect({center.x, center.y - halfIcon + bw * 0.5f},
                       {iconSize, bw}, border, d + 0.1f);
        // Bottom edge
        batch.drawRect({center.x, center.y + halfIcon - bw * 0.5f},
                       {iconSize, bw}, border, d + 0.1f);
        // Left edge
        batch.drawRect({center.x - halfIcon + bw * 0.5f, center.y},
                       {bw, innerH}, border, d + 0.1f);
        // Right edge
        batch.drawRect({center.x + halfIcon - bw * 0.5f, center.y},
                       {bw, innerH}, border, d + 0.1f);

        // Duration sweep overlay (clockwise from top, dark arc)
        if (buff.totalDuration > 0.0f) {
            float ratio = buff.remainingTime / buff.totalDuration;
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            if (ratio > 0.001f) {
                Color cdColor = {0.0f, 0.0f, 0.0f, 0.55f};
                // Sweep from top (-PI/2) clockwise by ratio * 2PI
                float startAngle = -PI * 0.5f;
                float endAngle = startAngle + ratio * 2.0f * PI;
                float arcRadius = iconSize * 0.5f - 1.0f;
                batch.drawArc(center, arcRadius, startAngle, endAngle,
                              cdColor, d + 0.2f, 16);
            }
        }

        // Stack count badge (bottom-right, if stacks > 1)
        if (buff.stacks > 1) {
            char stackBuf[8];
            std::snprintf(stackBuf, sizeof(stackBuf), "%d", buff.stacks);
            float fontSize = scaledFont(8.0f);
            Vec2 ts = sdf.measure(stackBuf, fontSize);
            Color stackColor = {1.0f, 1.0f, 1.0f, 1.0f};
            // Position at bottom-right of icon
            float tx = center.x + halfIcon - ts.x - 1.0f;
            float ty = center.y + halfIcon - ts.y - 1.0f;
            // Small dark background behind the text for readability
            Color stackBg = {0.0f, 0.0f, 0.0f, 0.7f};
            batch.drawRect({tx + ts.x * 0.5f, ty + ts.y * 0.5f},
                           {ts.x + 2.0f, ts.y + 1.0f}, stackBg, d + 0.25f);
            sdf.drawScreen(batch, stackBuf, {tx, ty}, fontSize, stackColor, d + 0.3f);
        }
    }

    renderChildren(batch, sdf);
}

} // namespace fate
