#pragma once
#include "engine/ecs/world.h"
#include "engine/ecs/entity.h"
#include "engine/core/types.h"
#include "imgui.h"
#include <cstdint>

namespace fate {

// HUD Bars — TWOM-style: HP top-left, MP top-right, XP bottom-center
// Positions adjustable via editor inspector
class HudBarsUI {
public:
    static HudBarsUI& instance() {
        static HudBarsUI s;
        return s;
    }

    bool isVisible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }
    void toggle() { visible_ = !visible_; }

    // Draw HUD bars (call every frame)
    void draw(World* world);

    // Draw settings panel in editor (call when F3 editor is open)
    void drawSettings();

    // Bar layout config (adjustable in editor)
    struct BarConfig {
        float x, y;        // top-left corner (screen pixels)
        float width;        // bar width (screen pixels)
        float height;       // bar height (screen pixels)
    };

    BarConfig hpBar{};
    BarConfig mpBar{};
    BarConfig xpBar{};

private:
    HudBarsUI() = default;

    bool visible_ = true;
    bool layoutInitialized_ = false;

    void initDefaultLayout();
    Entity* findPlayer(World* world);
    void drawBarAt(ImDrawList* dl, const BarConfig& cfg,
                   float fillPct, ImU32 fillColor, ImU32 bgColor,
                   const char* text);
};

} // namespace fate
