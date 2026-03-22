#pragma once
#include "engine/ui/ui_node.h"

namespace fate {

class EXPBar : public UINode {
public:
    EXPBar(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;

    float xp = 0, xpToLevel = 1;
};

} // namespace fate
