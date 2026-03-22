#pragma once
#include "engine/ui/ui_node.h"

namespace fate {

enum class BarDirection : uint8_t { LeftToRight, RightToLeft, BottomToTop, TopToBottom };

class ProgressBar : public UINode {
public:
    ProgressBar(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;

    float fillRatio() const;

    float value = 0.0f;
    float maxValue = 100.0f;
    Color fillColor = Color(0.2f, 0.8f, 0.2f, 1.0f);
    BarDirection direction = BarDirection::LeftToRight;
    bool showText = false;  // show "value/max" overlay
};

} // namespace fate
