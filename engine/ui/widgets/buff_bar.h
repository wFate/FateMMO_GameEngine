#pragma once
#include "engine/ui/ui_node.h"
#include <vector>
#include <cstdint>

namespace fate {

struct BuffDisplayData {
    uint8_t effectType = 0;   // maps to EffectType enum
    float remainingTime = 0.0f;
    float totalDuration = 0.0f;
    int stacks = 1;
};

class BuffBar : public UINode {
public:
    BuffBar(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;

    // Returns the background color for a given effect type index.
    // Debuffs (0-5) = reddish, Buffs (6-9,16) = greenish,
    // Shields (10-13) = bluish, Utility (14-15) = yellowish.
    static Color colorForEffectType(uint8_t effectType);

    std::vector<BuffDisplayData> buffs;
    float iconSize = 24.0f;
    float spacing = 3.0f;
    int maxVisible = 12;
};

} // namespace fate
