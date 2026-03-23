#pragma once
#include "engine/ui/ui_node.h"
#include <string>

namespace fate {

class BossHPBar : public UINode {
public:
    BossHPBar(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;

    // Returns HP ratio clamped to [0, 1].
    float hpRatio() const;

    std::string bossName;
    int currentHP = 0;
    int maxHP = 1;
    float barHeight = 20.0f;
    float barPadding = 12.0f;
};

} // namespace fate
