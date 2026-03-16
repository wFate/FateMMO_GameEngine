#pragma once
#include "engine/core/types.h"

namespace fate {

// 2D orthographic camera
// Virtual resolution: 960x540 (16:9 landscape) - the game world is always this size
// Actual window scales this to fit the display
class Camera {
public:
    // Virtual resolution (game world coordinates visible on screen)
    // 480x270 = pixel art sweet spot. Each art pixel = ~2.67 screen pixels at 1280x720.
    // Shows ~15 tiles across, ~8 tiles tall. Matches TWOM proportions.
    static constexpr float VIRTUAL_WIDTH = 480.0f;
    static constexpr float VIRTUAL_HEIGHT = 270.0f;

    Camera();

    void setPosition(const Vec2& pos) { position_ = pos; dirty_ = true; }
    void setZoom(float zoom) { zoom_ = zoom; dirty_ = true; }

    Vec2 position() const { return position_; }
    float zoom() const { return zoom_; }

    // Get the view-projection matrix for rendering
    Mat4 getViewProjection();

    // Convert screen coordinates to world coordinates
    Vec2 screenToWorld(const Vec2& screen, int windowWidth, int windowHeight) const;

    // Convert world coordinates to screen coordinates
    Vec2 worldToScreen(const Vec2& world, int windowWidth, int windowHeight) const;

    // Get visible world bounds
    Rect getVisibleBounds() const;

    // Smooth follow a target position
    void follow(const Vec2& target, float smoothing, float deltaTime);

private:
    Vec2 position_;
    float zoom_ = 1.0f;
    Mat4 viewProjection_;
    bool dirty_ = true;
};

} // namespace fate
