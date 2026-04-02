#pragma once

namespace fate {

// ============================================================================
// GameViewport — single source of truth for the active game render rect
//
// Set once per frame by the application before any UI rendering. All game UI
// systems query this instead of using ImGui::GetIO().DisplaySize, which gives
// the full window size (wrong when the editor letterboxes/pillarboxes the game
// into a device aspect ratio).
//
// Usage:
//   // In your app, before rendering any UI:
//   GameViewport::set(vp.x, vp.y, vs.x, vs.y);
//
//   // In any UI system:
//   float x = GameViewport::x();         // viewport left edge (screen coords)
//   float y = GameViewport::y();         // viewport top edge
//   float w = GameViewport::width();     // viewport width
//   float h = GameViewport::height();    // viewport height
//   float cx = GameViewport::centerX();  // horizontal center
//   float cy = GameViewport::centerY();  // vertical center
// ============================================================================

class GameViewport {
public:
    static void set(float x, float y, float w, float h) {
        x_ = x; y_ = y; w_ = w; h_ = h;
    }

    static float x()       { return x_; }
    static float y()       { return y_; }
    static float width()   { return w_; }
    static float height()  { return h_; }
    static float centerX() { return x_ + w_ * 0.5f; }
    static float centerY() { return y_ + h_ * 0.5f; }
    static float right()   { return x_ + w_; }
    static float bottom()  { return y_ + h_; }

private:
    static inline float x_ = 0;
    static inline float y_ = 0;
    static inline float w_ = 1280;
    static inline float h_ = 720;
};

} // namespace fate
