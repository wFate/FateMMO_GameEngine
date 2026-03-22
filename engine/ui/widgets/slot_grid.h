#pragma once
#include "engine/ui/ui_node.h"

namespace fate {

class SlotGrid : public UINode {
public:
    SlotGrid(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;

    void generateSlots();

    int columns = 5;
    int rows = 3;
    float slotSize = 48.0f;
    float slotPadding = 4.0f;
    std::string acceptsDragType;  // passed to each slot
};

} // namespace fate
