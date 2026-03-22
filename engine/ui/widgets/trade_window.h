#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"

namespace fate {

class TradeWindow : public UINode {
public:
    TradeWindow(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;

    struct TradeSlot {
        std::string itemId;
        int quantity = 0;
    };

    TradeSlot mySlots[9];
    TradeSlot theirSlots[9];
    int myGold = 0;
    int theirGold = 0;
    std::string partnerName;
    bool myLocked = false;
    bool theirLocked = false;

    UIClickCallback onLock;
    UIClickCallback onAccept;
    UIClickCallback onCancel;
};

} // namespace fate
