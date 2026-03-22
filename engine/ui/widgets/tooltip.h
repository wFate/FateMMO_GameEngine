#pragma once
#include "engine/ui/ui_node.h"

namespace fate {

class Tooltip : public UINode {
public:
    Tooltip(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;

    std::string tooltipText;
    float maxWidth = 250.0f;
};

} // namespace fate
