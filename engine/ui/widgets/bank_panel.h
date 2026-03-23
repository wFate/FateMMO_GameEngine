#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include "game/shared/bank_storage.h"
#include <functional>
#include <vector>

namespace fate {

class BankPanel : public UINode {
public:
    BankPanel(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;
    bool onKeyInput(int scancode, bool pressed) override;
    void update(float dt);

    // Bank data (set by GameApp each frame)
    struct BankItem {
        std::string itemId;
        std::string displayName;
        uint16_t count = 0;
    };
    std::vector<BankItem> bankItems;
    int64_t bankGold = 0;

    // Player inventory (set by GameApp each frame)
    struct InvSlot {
        std::string itemId;
        std::string displayName;
        int quantity = 0;
    };
    static constexpr int MAX_SLOTS = 16;
    InvSlot playerItems[MAX_SLOTS];
    int64_t playerGold = 0;

    // Error display
    std::string errorMessage;
    float errorTimer = 0.0f;

    // Callbacks
    std::function<void(uint32_t npcId, uint8_t slot)> onDepositItem;
    std::function<void(uint32_t npcId, uint16_t itemIndex)> onWithdrawItem;
    std::function<void(uint32_t npcId, int64_t amount)> onDepositGold;
    std::function<void(uint32_t npcId, int64_t amount)> onWithdrawGold;
    UIClickCallback onClose;

    uint32_t npcId = 0;

    void open(uint32_t npcId);
    void close();
    bool isOpen() const { return visible(); }
    void rebuild();

private:
    int64_t goldInputAmount_ = 0;
    float scrollOffsetBank_ = 0.0f;
};

} // namespace fate
