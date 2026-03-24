#include "engine/ui/widgets/boss_hp_bar.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <algorithm>

namespace fate {

BossHPBar::BossHPBar(const std::string& id) : UINode(id, "boss_hp_bar") {
    visible_ = false;  // hidden until boss engaged
}

float BossHPBar::hpRatio() const {
    if (maxHP <= 0) return 0.0f;
    float ratio = static_cast<float>(currentHP) / static_cast<float>(maxHP);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    return ratio;
}

void BossHPBar::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Semi-transparent dark background panel (full widget width)
    Color panelBg = (style.backgroundColor.a > 0.0f)
                  ? style.backgroundColor
                  : Color{0.04f, 0.04f, 0.08f, 0.82f};
    panelBg.a *= style.opacity;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, panelBg, d);

    // Boss name centered above the bar (gold text, 14px)
    float nameFontSize = 14.0f;
    Color goldText = {1.0f, 0.84f, 0.0f, 1.0f};
    float nameY = rect.y + 4.0f;

    if (!bossName.empty()) {
        Vec2 nameSize = sdf.measure(bossName, nameFontSize);
        float nameX = rect.x + (rect.w - nameSize.x) * 0.5f;
        sdf.drawScreen(batch, bossName, {nameX, nameY},
                       nameFontSize, goldText, d + 0.1f);
    }

    // HP bar positioned below the name
    float nameBlockH = nameFontSize + 8.0f;
    float barX = rect.x + barPadding;
    float barY = rect.y + nameBlockH;
    float barW = rect.w - barPadding * 2.0f;

    // Dark track underneath
    Color trackColor = {0.08f, 0.08f, 0.08f, 0.9f};
    batch.drawRect({barX + barW * 0.5f, barY + barHeight * 0.5f},
                   {barW, barHeight}, trackColor, d + 0.1f);

    // Red fill proportional to HP ratio
    float ratio = hpRatio();
    if (ratio > 0.0f) {
        Color hpFill = {0.78f, 0.12f, 0.12f, 1.0f};
        float fillW = barW * ratio;
        float fillX = barX + fillW * 0.5f;
        batch.drawRect({fillX, barY + barHeight * 0.5f},
                       {fillW, barHeight}, hpFill, d + 0.2f);
    }

    // 1px border around bar
    Color borderColor = (style.borderColor.a > 0.0f)
                      ? style.borderColor
                      : Color{0.45f, 0.45f, 0.55f, 0.8f};
    float bw = 1.0f;
    float innerH = barHeight - bw * 2.0f;
    // Top edge
    batch.drawRect({barX + barW * 0.5f, barY + bw * 0.5f},
                   {barW, bw}, borderColor, d + 0.3f);
    // Bottom edge
    batch.drawRect({barX + barW * 0.5f, barY + barHeight - bw * 0.5f},
                   {barW, bw}, borderColor, d + 0.3f);
    // Left edge
    batch.drawRect({barX + bw * 0.5f, barY + barHeight * 0.5f},
                   {bw, innerH}, borderColor, d + 0.3f);
    // Right edge
    batch.drawRect({barX + barW - bw * 0.5f, barY + barHeight * 0.5f},
                   {bw, innerH}, borderColor, d + 0.3f);

    // HP percentage text centered on bar (white, 11px)
    float pctFontSize = 11.0f;
    Color white = (style.textColor.a > 0.0f)
               ? style.textColor
               : Color{1.0f, 1.0f, 1.0f, 1.0f};
    char pctBuf[128];
    float pctVal = ratio * 100.0f;
    if (!bossName.empty()) {
        std::snprintf(pctBuf, sizeof(pctBuf), "%s \xe2\x80\x94 %.1f%%",
                      bossName.c_str(), static_cast<double>(pctVal));
    } else {
        std::snprintf(pctBuf, sizeof(pctBuf), "%.1f%%", static_cast<double>(pctVal));
    }
    Vec2 pctSize = sdf.measure(pctBuf, pctFontSize);
    float pctX = barX + (barW - pctSize.x) * 0.5f;
    float pctY = barY + (barHeight - pctSize.y) * 0.5f;
    sdf.drawScreen(batch, pctBuf, {pctX, pctY}, pctFontSize, white, d + 0.4f);

    renderChildren(batch, sdf);
}

} // namespace fate
