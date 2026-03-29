#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace fate {

struct CostumeEntry {
    std::string costumeDefId;
    std::string displayName;
    uint8_t     slotType    = 0;
    uint16_t    visualIndex = 0;
    uint8_t     rarity      = 0;  // 0=Common, 1=Uncommon, 2=Rare, 3=Epic, 4=Legendary
};

class CostumePanel : public UINode {
public:
    CostumePanel(const std::string& id);

    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;
    bool onKeyInput(int scancode, bool pressed) override;

    // --- Serializable layout properties (editor-tweakable) ---
    float titleFontSize      = 18.0f;
    float bodyFontSize       = 13.0f;
    float infoFontSize       = 11.0f;
    int   gridCols           = 4;
    float slotSize           = 48.0f;
    float slotSpacing        = 6.0f;
    float buttonHeight       = 32.0f;
    float buttonSpacing      = 8.0f;
    float filterTabHeight    = 24.0f;
    float borderWidth        = 2.0f;
    float headerHeight       = 28.0f;
    float bottomReserveHeight = 60.0f;

    // --- Serializable colors (editor-tweakable) ---
    Color backgroundColor      = {0.08f, 0.08f, 0.12f, 0.95f};
    Color borderColor           = {0.25f, 0.25f, 0.35f, 1.0f};
    Color titleBarColor         = {0.12f, 0.12f, 0.18f, 1.0f};
    Color titleColor            = {0.9f, 0.9f, 0.85f, 1.0f};
    Color closeBtnColor         = {0.3f, 0.15f, 0.15f, 0.9f};
    Color tabColor              = {0.14f, 0.14f, 0.20f, 0.9f};
    Color tabActiveColor        = {0.22f, 0.28f, 0.45f, 0.9f};
    Color tabTextColor          = {0.7f, 0.7f, 0.65f, 1.0f};
    Color tabActiveTextColor    = {0.95f, 0.95f, 0.9f, 1.0f};
    Color slotColor             = {0.12f, 0.12f, 0.18f, 0.9f};
    Color slotSelectedColor     = {0.20f, 0.24f, 0.38f, 0.9f};
    Color equippedIndicatorColor = {0.3f, 0.8f, 0.3f, 1.0f};
    Color nameColor             = {0.85f, 0.85f, 0.8f, 1.0f};
    Color equipBtnColor         = {0.18f, 0.22f, 0.35f, 0.9f};
    Color unequipBtnColor       = {0.35f, 0.15f, 0.15f, 0.9f};
    Color buttonTextColor       = {0.9f, 0.9f, 0.85f, 1.0f};
    Color hintColor             = {0.5f, 0.5f, 0.45f, 0.8f};

    // --- Position offsets (unscaled, multiplied by layoutScale_ at render) ---
    Vec2 titleOffset = {0.0f, 0.0f};   // "Costumes" title
    Vec2 toggleOffset = {0.0f, 0.0f};  // ON/OFF toggle
    Vec2 gridOffset = {0.0f, 0.0f};    // costume grid area
    Vec2 infoOffset = {0.0f, 0.0f};    // selected costume info

    // --- Runtime state (set by GameApp, NOT serialized) ---
    std::vector<CostumeEntry> ownedCostumes;
    std::unordered_map<uint8_t, std::string> equippedBySlot; // slotType -> costumeDefId
    bool showCostumes = true;
    int  selectedIndex = -1;  // index into filteredIndices_
    uint8_t filterSlot = 0;   // 0 = all

    // --- Callbacks ---
    std::function<void(const std::string& costumeDefId)> onEquipCostume;
    std::function<void(uint8_t slotType)> onUnequipCostume;
    std::function<void(bool show)> onToggleCostumes;
    UIClickCallback onClose;

    void open();
    void close();
    bool isOpen() const { return isOpen_; }

private:
    bool isOpen_ = false;

    std::vector<int> filteredIndices_;
    void rebuildFilter();

    Rect toggleBtnRect_;
    Rect closeBtnRect_;
    Rect equipBtnRect_;
    std::vector<Rect> filterTabRects_;
    std::vector<Rect> gridSlotRects_;
};

} // namespace fate
