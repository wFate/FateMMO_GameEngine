#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"

namespace fate {

class Slot : public UINode {
public:
    Slot(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;

    bool onPress(const Vec2& localPos) override;
    bool acceptsDrop(const DragPayload& payload) const override;
    void onDrop(const DragPayload& payload) override;

    DragPayload createDragPayload() const;

    std::string itemId;
    int quantity = 0;
    std::string icon;       // texture key
    std::string slotType;   // "item", "equipment", "skill"
    std::string acceptsDragType;  // what drag types this slot accepts

    UIClickCallback onSlotClick;
};

} // namespace fate
