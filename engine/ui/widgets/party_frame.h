#pragma once
#include "engine/ui/ui_node.h"
#include <vector>

namespace fate {

struct PartyFrameMemberInfo {
    std::string name;
    float hp = 0, maxHp = 1;
    float mp = 0, maxMp = 1;
    int level = 1;
    bool isLeader = false;
};

class PartyFrame : public UINode {
public:
    PartyFrame(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;

    std::vector<PartyFrameMemberInfo> members;  // max 2 (excludes self, party max 3)
    float cardWidth  = 170.0f;
    float cardHeight = 48.0f;
    float cardSpacing = 4.0f;

    // Position offsets (unscaled, multiplied by layoutScale_ at render)
    Vec2 nameOffset = {0.0f, 0.0f};      // member name text
    Vec2 levelOffset = {0.0f, 0.0f};     // level text
    Vec2 portraitOffset = {0.0f, 0.0f};   // portrait circle
    Vec2 barOffset = {0.0f, 0.0f};       // HP/MP bars

    // Font sizes (unscaled)
    float nameFontSize = 11.0f;
    float levelFontSize = 10.0f;

    // Layout
    float portraitRadius = 10.0f;
    float hpBarHeight = 8.0f;
    float mpBarHeight = 6.0f;
    float borderWidth = 1.0f;
    float portraitPadLeft = 6.0f;
    float portraitRimWidth = 1.5f;
    float crownSize = 5.0f;
    float textGapAfterPortrait = 6.0f;
    float textPadRight = 4.0f;
    float namePadTop = 6.0f;
    float levelPadRight = 5.0f;
    float barOffsetY = 13.0f;
    float barGap = 2.0f;

    // Colors
    Color cardBgColor = {0.08f, 0.08f, 0.12f, 0.85f};
    Color cardBorderColor = {0.25f, 0.25f, 0.35f, 0.70f};
    Color portraitFillColor = {0.20f, 0.22f, 0.30f, 0.95f};
    Color portraitRimColor = {0.45f, 0.45f, 0.60f, 0.80f};
    Color crownColor = {1.0f, 0.82f, 0.0f, 1.0f};
    Color nameColor = {1.0f, 1.0f, 1.0f, 0.95f};
    Color levelColor = {0.65f, 0.65f, 0.70f, 0.85f};
    Color hpBarBgColor = {0.15f, 0.08f, 0.08f, 0.85f};
    Color hpFillColor = {0.80f, 0.18f, 0.18f, 1.0f};
    Color mpBarBgColor = {0.08f, 0.08f, 0.18f, 0.85f};
    Color mpFillColor = {0.18f, 0.40f, 0.85f, 1.0f};
};

} // namespace fate
