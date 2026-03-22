#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"

namespace fate {

class DPad : public UINode {
public:
    DPad(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;
    void onRelease(const Vec2& localPos) override;

    // Returns the cardinal direction for a local-space touch position.
    // localPos is relative to the widget's top-left corner.
    Direction directionFromLocal(const Vec2& localPos) const;

    float dpadSize        = 140.0f;  // total widget width/height
    float deadZoneRadius  = 15.0f;
    float opacity         = 0.6f;
    Direction activeDirection = Direction::None;

    UIClickCallback onDirectionChange;
};

} // namespace fate
