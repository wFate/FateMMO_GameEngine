#pragma once
#include "engine/ui/ui_node.h"

namespace fate {

class TargetFrame : public UINode {
public:
    TargetFrame(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;

    std::string targetName;
    float hp = 0, maxHp = 1;
};

} // namespace fate
