#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"

namespace fate {

class CharacterCreationScreen : public UINode {
public:
    CharacterCreationScreen(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;
    bool onTextInput(const std::string& input) override;
    bool onKeyInput(int scancode, bool pressed) override;

    int selectedClass = 0;     // 0=Warrior, 1=Magician, 2=Archer
    int selectedFaction = 0;   // 0=Xyros, 1=Fenor, 2=Zethos, 3=Solis

    // Name input (built-in, not a child TextInput for simplicity)
    std::string characterName;
    int cursorPos = 0;
    bool nameFieldFocused = false;
    static constexpr int MAX_NAME_LENGTH = 16;

    UIClickCallback onBack;
    UIClickCallback onNext;

    // Status message for errors
    std::string statusMessage;
    bool isError = false;

private:
    static constexpr const char* classNames[] = {"Warrior", "Magician", "Archer"};
    static constexpr const char* classDescs[] = {
        "A sturdy melee fighter with high defense and close-range attacks. Recommended stats: STR, CON.",
        "A powerful spellcaster who deals devastating magical damage from a distance. Recommended stats: INT, WIS.",
        "A swift ranged attacker with high critical rate and evasion. Recommended stats: DEX, STR."
    };
    static constexpr const char* factionNames[] = {"Xyros", "Fenor", "Zethos", "Solis"};

    Color factionColor(int faction) const;

    // Hit areas (absolute screen coords computed from computedRect_)
    Rect classButtonRect(int index) const;
    Rect factionButtonRect(int index) const;
    Rect nameFieldRect() const;
    Rect backButtonRect() const;
    Rect nextButtonRect() const;
};

} // namespace fate
