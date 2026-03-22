#include "engine/ui/widgets/slot_grid.h"
#include "engine/ui/widgets/slot.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <string>

namespace fate {

SlotGrid::SlotGrid(const std::string& id) : UINode(id, "slot_grid") {}

void SlotGrid::generateSlots() {
    children_.clear();
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < columns; col++) {
            int idx = row * columns + col;
            auto slot = std::make_unique<Slot>(id_ + "_slot_" + std::to_string(idx));
            float x = col * (slotSize + slotPadding);
            float y = row * (slotSize + slotPadding);
            slot->setAnchor({AnchorPreset::TopLeft, {x, y}, {slotSize, slotSize}, {}, {}});
            slot->acceptsDragType = acceptsDragType;
            addChild(std::move(slot));
        }
    }
}

void SlotGrid::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;
    // Grid has no visual itself — just renders slot children
    renderChildren(batch, sdf);
}

} // namespace fate
