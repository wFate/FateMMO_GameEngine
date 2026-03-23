#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include "game/shared/npc_types.h"
#include <functional>
#include <vector>

namespace fate {

class ShopPanel : public UINode {
public:
    ShopPanel(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;
    bool onKeyInput(int scancode, bool pressed) override;
    void update(float dt);

    // Shop data (set by GameApp)
    struct ShopEntry {
        std::string itemId;
        std::string itemName;
        int64_t buyPrice = 0;
        int64_t sellPrice = 0;
        int16_t stock = -1;  // -1 = unlimited
    };
    std::vector<ShopEntry> shopItems;
    std::string shopName = "Shop";

    // Player inventory (set by GameApp each frame)
    struct InvSlot {
        std::string itemId;
        std::string displayName;
        int quantity = 0;
        int64_t sellPrice = 0;
        bool soulbound = false;
    };
    static constexpr int MAX_SLOTS = 16;
    InvSlot playerItems[MAX_SLOTS];
    int64_t playerGold = 0;

    // Error display
    std::string errorMessage;
    float errorTimer = 0.0f;

    // Callbacks
    std::function<void(uint32_t npcId, const std::string& itemId, uint16_t qty)> onBuy;
    std::function<void(uint32_t npcId, uint8_t slot, uint16_t qty)> onSell;
    UIClickCallback onClose;

    uint32_t npcId = 0;

    void open(uint32_t npcId, const std::string& name, const std::vector<ShopItem>& items);
    void close();
    bool isOpen() const { return visible(); }
    void rebuild();

private:
    // Quantity confirmation popup state
    bool showSellConfirm_ = false;
    uint8_t sellSlot_ = 0;
    int sellMaxQty_ = 0;
    int sellInputQty_ = 0;
    float scrollOffsetShop_ = 0.0f;

    // Double-click tracking
    int lastClickSlot_ = -1;
    float lastClickTime_ = 0.0f;
    float timeSinceStart_ = 0.0f;

    // Layout helpers
    Rect getShopListArea() const;
    Rect getInventoryGridArea() const;
    int hitTestInventorySlot(const Vec2& localPos) const;
    int hitTestShopBuyButton(const Vec2& localPos) const;

    void renderShopPane(SpriteBatch& batch, SDFText& sdf, float depth);
    void renderInventoryPane(SpriteBatch& batch, SDFText& sdf, float depth);
    void renderGoldBar(SpriteBatch& batch, SDFText& sdf, float depth);
    void renderSellConfirm(SpriteBatch& batch, SDFText& sdf, float depth);
    void renderError(SpriteBatch& batch, SDFText& sdf, float depth);
};

} // namespace fate
