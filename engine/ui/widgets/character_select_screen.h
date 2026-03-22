#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include <vector>

namespace fate {

struct CharacterSlot {
    std::string name;
    std::string className;
    int level = 0;
    bool empty = true;
};

class CharacterSelectScreen : public UINode {
public:
    CharacterSelectScreen(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;

    std::vector<CharacterSlot> slots;  // up to 7
    int selectedSlot = 0;
    float slotCircleSize = 52.0f;
    float entryButtonWidth = 120.0f;

    UIClickCallback onEntry;
    UIClickCallback onCreateNew;  // "+" empty slot pressed
    UIClickCallback onDelete;
    UIClickCallback onSwap;

private:
    // Layout helpers (positions relative to computedRect_)
    Vec2 slotCenter(int index) const;
    Rect entryButtonRect() const;
    Rect swapButtonRect() const;
    Rect deleteButtonRect() const;
};

} // namespace fate
