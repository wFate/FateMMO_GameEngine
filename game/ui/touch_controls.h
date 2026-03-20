#pragma once
#include "engine/core/types.h"
#include "engine/input/action_map.h"
#include <SDL.h>
#include <cstdint>

namespace fate {

struct TouchRegion {
    float cx, cy;       // center position (viewport-relative pixels)
    float radius;       // hit radius
};

class TouchControls {
public:
    static TouchControls& instance() {
        static TouchControls s;
        return s;
    }

    // Enable/disable (auto-enabled on FATEMMO_MOBILE, toggle on desktop for testing)
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    // Call each frame to process touch state and inject actions
    void update(ActionMap& actionMap, float viewportX, float viewportY,
                float viewportW, float viewportH);

    // Render the touch overlay via ImGui ForegroundDrawList
    void render(float viewportX, float viewportY,
                float viewportW, float viewportH);

    // Query: did the user tap on the game world (not on a control)?
    bool hasWorldTap() const { return worldTapActive_; }
    Vec2 worldTapPosition() const { return worldTapPos_; }
    void consumeWorldTap() { worldTapActive_ = false; }

    // Public for testing
    Direction classifyDpadDirection(float touchX, float touchY) const;
    bool isInsideCircle(float tx, float ty, float cx, float cy, float r) const;

    // Expose layout for testing (set by update)
    float dpadCenterX() const { return dpadCenterX_; }
    float dpadCenterY() const { return dpadCenterY_; }
    float dpadRadius() const { return dpadRadius_; }

private:
    TouchControls() = default;

    bool enabled_ =
#ifdef FATEMMO_MOBILE
        true;
#else
        false;
#endif

    // --- D-pad state ---
    int dpadFingerId_ = -1;       // which finger is on the D-pad (-1 = none)
    Direction dpadDirection_ = Direction::None;
    float dpadCenterX_ = 0;
    float dpadCenterY_ = 0;
    float dpadRadius_ = 0;

    // --- Action buttons ---
    struct ButtonDef {
        ActionId action;
        float offsetX;   // offset from bottom-right corner (fraction of viewport width)
        float offsetY;   // offset from bottom (fraction of viewport height)
        float radius;    // hit radius (viewport pixels)
        int fingerId = -1;
        bool pressed = false;
    };
    static constexpr int BUTTON_COUNT = 6; // Attack + 5 skills
    ButtonDef buttons_[BUTTON_COUNT];
    bool buttonsInitialized_ = false;

    void initButtons();

    // --- World tap ---
    bool worldTapActive_ = false;
    Vec2 worldTapPos_{0, 0};

    // --- Touch tracking ---
    struct ActiveTouch {
        int fingerId;
        float x, y;     // viewport-relative position
        bool isNew;      // just started this frame
    };
    static constexpr int MAX_TOUCHES = 10;
    ActiveTouch activeTouches_[MAX_TOUCHES];
    int activeTouchCount_ = 0;

    void gatherTouches(float viewportX, float viewportY, float viewportW, float viewportH);
};

} // namespace fate
