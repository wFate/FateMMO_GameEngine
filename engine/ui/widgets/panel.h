#pragma once
#include "engine/ui/ui_node.h"

namespace fate {

class Panel : public UINode {
public:
    Panel(const std::string& id);

    void render(SpriteBatch& batch, SDFText& text) override;

    bool draggable = false;
    bool closeable = false;
    std::string title;
};

} // namespace fate
