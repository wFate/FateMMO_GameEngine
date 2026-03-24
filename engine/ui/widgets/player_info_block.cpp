#include "engine/ui/widgets/player_info_block.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <algorithm>

namespace fate {

PlayerInfoBlock::PlayerInfoBlock(const std::string& id)
    : UINode(id, "player_info_block") {}

void PlayerInfoBlock::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Dark background panel behind the entire info block (grounding panel)
    Color panelBg = (style.backgroundColor.a > 0.0f)
                  ? style.backgroundColor
                  : Color{0.1f, 0.1f, 0.15f, 0.75f};
    panelBg.a *= style.opacity;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, panelBg, d - 0.2f);

    // Portrait circle (dark fill + lighter border)
    float portraitR = portraitSize * 0.5f;
    Vec2 portraitCenter = {rect.x + 4.0f + portraitR, rect.y + 4.0f + portraitR};

    Color portraitFill   = {0.15f, 0.15f, 0.2f, 0.9f};
    Color portraitBorder = {0.55f, 0.55f, 0.75f, 1.0f};
    batch.drawCircle(portraitCenter, portraitR, portraitFill, d, 24);
    batch.drawRing(portraitCenter, portraitR, 3.0f, portraitBorder, d + 0.1f, 24);

    // HP bar (right of portrait)
    float barX = rect.x + 4.0f + portraitSize + 8.0f;
    float hpY  = rect.y + 6.0f;

    Color barBg   = {0.1f, 0.1f, 0.1f, 0.85f};
    Color barBorder = {0.35f, 0.35f, 0.45f, 0.9f};
    Color hpColor = {0.85f, 0.15f, 0.15f, 1.0f};
    Color mpColor = {0.15f, 0.40f, 0.85f,  1.0f};
    float borderW = 1.5f;

    // HP border (slightly larger rect behind) + background
    batch.drawRect({barX + barWidth * 0.5f, hpY + barHeight * 0.5f},
                   {barWidth + borderW * 2, barHeight + borderW * 2}, barBorder, d - 0.05f);
    batch.drawRect({barX + barWidth * 0.5f, hpY + barHeight * 0.5f},
                   {barWidth, barHeight}, barBg, d);

    // HP fill
    float hpRatio = (maxHp > 0.0f) ? std::clamp(hp / maxHp, 0.0f, 1.0f) : 0.0f;
    if (hpRatio > 0.0f) {
        float fw = barWidth * hpRatio;
        batch.drawRect({barX + fw * 0.5f, hpY + barHeight * 0.5f},
                       {fw, barHeight}, hpColor, d + 0.1f);
    }

    // HP text overlay (shadow + white)
    char hpBuf[32];
    snprintf(hpBuf, sizeof(hpBuf), "%.0f / %.0f", hp, maxHp);
    float fontSize = 12.0f;
    Vec2 hpTs = sdf.measure(hpBuf, fontSize);
    Color white = (style.textColor.a > 0.0f)
               ? style.textColor
               : Color{1.0f, 1.0f, 1.0f, 1.0f};
    Color shadow = {0.0f, 0.0f, 0.0f, 0.8f};
    float tx = barX + (barWidth - hpTs.x) * 0.5f;
    float ty = hpY + (barHeight - hpTs.y) * 0.5f;
    sdf.drawScreen(batch, hpBuf, {tx + 1.0f, ty + 1.0f}, fontSize, shadow, d + 0.15f);
    sdf.drawScreen(batch, hpBuf, {tx, ty}, fontSize, white, d + 0.2f);

    // MP bar (below HP bar)
    float mpY = hpY + barHeight + 4.0f;

    // MP border (slightly larger rect behind) + background
    batch.drawRect({barX + barWidth * 0.5f, mpY + barHeight * 0.5f},
                   {barWidth + borderW * 2, barHeight + borderW * 2}, barBorder, d - 0.05f);
    batch.drawRect({barX + barWidth * 0.5f, mpY + barHeight * 0.5f},
                   {barWidth, barHeight}, barBg, d);

    // MP fill
    float mpRatio = (maxMp > 0.0f) ? std::clamp(mp / maxMp, 0.0f, 1.0f) : 0.0f;
    if (mpRatio > 0.0f) {
        float fw = barWidth * mpRatio;
        batch.drawRect({barX + fw * 0.5f, mpY + barHeight * 0.5f},
                       {fw, barHeight}, mpColor, d + 0.1f);
    }

    // MP text overlay (shadow + white)
    char mpBuf[32];
    snprintf(mpBuf, sizeof(mpBuf), "%.0f / %.0f", mp, maxMp);
    Vec2 mpTs = sdf.measure(mpBuf, fontSize);
    float mx = barX + (barWidth - mpTs.x) * 0.5f;
    float my = mpY + (barHeight - mpTs.y) * 0.5f;
    sdf.drawScreen(batch, mpBuf, {mx + 1.0f, my + 1.0f}, fontSize, shadow, d + 0.15f);
    sdf.drawScreen(batch, mpBuf, {mx, my}, fontSize, white, d + 0.2f);

    // Level text (below portrait)
    float levelY = rect.y + 4.0f + portraitSize + 4.0f;
    char lvBuf[16];
    snprintf(lvBuf, sizeof(lvBuf), "LV %d", level);
    float lvFontSize = 12.0f;
    Vec2 lvTs = sdf.measure(lvBuf, lvFontSize);
    sdf.drawScreen(batch, lvBuf,
        {rect.x + 4.0f + (portraitSize - lvTs.x) * 0.5f, levelY},
        lvFontSize, white, d + 0.2f);

    // Gold (below level)
    if (!goldText.empty()) {
        float goldY = levelY + lvTs.y + 3.0f;
        Color goldColor = {1.0f, 0.85f, 0.2f, 1.0f};
        float goldFontSize = 11.0f;
        // Gold circle placeholder
        batch.drawCircle({rect.x + 10.0f, goldY + 6.0f}, 5.0f, goldColor, d + 0.1f, 12);
        sdf.drawScreen(batch, goldText.c_str(),
            {rect.x + 19.0f, goldY},
            goldFontSize, goldColor, d + 0.2f);
    }

    renderChildren(batch, sdf);
}

} // namespace fate
