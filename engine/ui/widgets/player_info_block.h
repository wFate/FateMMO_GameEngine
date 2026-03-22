#pragma once
#include "engine/ui/ui_node.h"

namespace fate {

class PlayerInfoBlock : public UINode {
public:
    PlayerInfoBlock(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;

    float portraitSize = 48.0f;
    float barWidth = 120.0f;
    float barHeight = 16.0f;
    float barSpacing = 2.0f;

    // Values set by data binding or direct code
    float hp = 0, maxHp = 1, mp = 0, maxMp = 1;
    int level = 1;
    std::string playerName;
    std::string goldText;
};

} // namespace fate
