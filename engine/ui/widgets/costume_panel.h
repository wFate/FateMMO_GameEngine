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
    float titleFontSize   = 18.0f;
    float bodyFontSize    = 13.0f;
    float infoFontSize    = 11.0f;
    int   gridCols        = 4;
    float slotSize        = 48.0f;
    float slotSpacing     = 6.0f;
    float buttonHeight    = 32.0f;
    float buttonSpacing   = 8.0f;
    float filterTabHeight = 24.0f;

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
