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

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Portrait circle (dark fill + lighter border)
    float portraitR = portraitSize * 0.5f;
    Vec2 portraitCenter = {rect.x + portraitR, rect.y + portraitR};

    Color portraitFill   = {0.15f, 0.15f, 0.2f, 0.9f};
    Color portraitBorder = {0.55f, 0.55f, 0.75f, 1.0f};
    batch.drawCircle(portraitCenter, portraitR, portraitFill, d, 24);
    batch.drawRing(portraitCenter, portraitR, 2.0f, portraitBorder, d + 0.1f, 24);

    // HP bar (right of portrait)
    float barX = rect.x + portraitSize + 6.0f;
    float hpY  = rect.y;

    Color barBg   = {0.1f, 0.1f, 0.1f, 0.85f};
    Color hpColor = {0.75f, 0.15f, 0.15f, 1.0f};
    Color mpColor = {0.15f, 0.35f, 0.8f,  1.0f};

    // HP background
    batch.drawRect({barX + barWidth * 0.5f, hpY + barHeight * 0.5f},
                   {barWidth, barHeight}, barBg, d);

    // HP fill
    float hpRatio = (maxHp > 0.0f) ? std::clamp(hp / maxHp, 0.0f, 1.0f) : 0.0f;
    if (hpRatio > 0.0f) {
        float fw = barWidth * hpRatio;
        batch.drawRect({barX + fw * 0.5f, hpY + barHeight * 0.5f},
                       {fw, barHeight}, hpColor, d + 0.1f);
    }

    // HP text overlay
    char hpBuf[32];
    snprintf(hpBuf, sizeof(hpBuf), "%.0f/%.0f", hp, maxHp);
    float fontSize = 10.0f;
    Vec2 hpTs = sdf.measure(hpBuf, fontSize);
    Color white = {1.0f, 1.0f, 1.0f, 1.0f};
    sdf.drawScreen(batch, hpBuf,
        {barX + (barWidth - hpTs.x) * 0.5f, hpY + (barHeight - hpTs.y) * 0.5f},
        fontSize, white, d + 0.2f);

    // MP bar (below HP bar)
    float mpY = hpY + barHeight + barSpacing;

    // MP background
    batch.drawRect({barX + barWidth * 0.5f, mpY + barHeight * 0.5f},
                   {barWidth, barHeight}, barBg, d);

    // MP fill
    float mpRatio = (maxMp > 0.0f) ? std::clamp(mp / maxMp, 0.0f, 1.0f) : 0.0f;
    if (mpRatio > 0.0f) {
        float fw = barWidth * mpRatio;
        batch.drawRect({barX + fw * 0.5f, mpY + barHeight * 0.5f},
                       {fw, barHeight}, mpColor, d + 0.1f);
    }

    // MP text overlay
    char mpBuf[32];
    snprintf(mpBuf, sizeof(mpBuf), "%.0f/%.0f", mp, maxMp);
    Vec2 mpTs = sdf.measure(mpBuf, fontSize);
    sdf.drawScreen(batch, mpBuf,
        {barX + (barWidth - mpTs.x) * 0.5f, mpY + (barHeight - mpTs.y) * 0.5f},
        fontSize, white, d + 0.2f);

    // Level text (below portrait)
    float levelY = rect.y + portraitSize + 2.0f;
    char lvBuf[16];
    snprintf(lvBuf, sizeof(lvBuf), "LV %d", level);
    float lvFontSize = 10.0f;
    Vec2 lvTs = sdf.measure(lvBuf, lvFontSize);
    sdf.drawScreen(batch, lvBuf,
        {rect.x + (portraitSize - lvTs.x) * 0.5f, levelY},
        lvFontSize, white, d + 0.2f);

    // Gold (below level)
    if (!goldText.empty()) {
        float goldY = levelY + lvTs.y + 2.0f;
        Color goldColor = {1.0f, 0.85f, 0.2f, 1.0f};
        float goldFontSize = 10.0f;
        // Gold circle placeholder
        batch.drawCircle({rect.x + 5.0f, goldY + 5.0f}, 4.0f, goldColor, d + 0.1f, 12);
        sdf.drawScreen(batch, goldText.c_str(),
            {rect.x + 12.0f, goldY},
            goldFontSize, goldColor, d + 0.2f);
    }

    renderChildren(batch, sdf);
}

} // namespace fate
