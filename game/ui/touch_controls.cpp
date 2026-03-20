#include "game/ui/touch_controls.h"
#include "game/ui/game_viewport.h"
#include "engine/core/logger.h"
#include "imgui.h"
#include <SDL.h>
#include <cmath>

namespace fate {

void TouchControls::initButtons() {
    // Attack button — large, closest to thumb
    buttons_[0] = {ActionId::Attack,     0.10f, 0.30f, 0.0f, -1, false};
    // Skill slots — arc around attack button
    buttons_[1] = {ActionId::SkillSlot1, 0.18f, 0.22f, 0.0f, -1, false};
    buttons_[2] = {ActionId::SkillSlot2, 0.22f, 0.35f, 0.0f, -1, false};
    buttons_[3] = {ActionId::SkillSlot3, 0.18f, 0.48f, 0.0f, -1, false};
    buttons_[4] = {ActionId::SkillSlot4, 0.10f, 0.55f, 0.0f, -1, false};
    buttons_[5] = {ActionId::SkillSlot5, 0.04f, 0.48f, 0.0f, -1, false};
    buttonsInitialized_ = true;
}

bool TouchControls::isInsideCircle(float tx, float ty, float cx, float cy, float r) const {
    float dx = tx - cx;
    float dy = ty - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

Direction TouchControls::classifyDpadDirection(float touchX, float touchY) const {
    float dx = touchX - dpadCenterX_;
    float dy = touchY - dpadCenterY_;
    float dist = std::sqrt(dx * dx + dy * dy);

    // Dead zone: 25% of radius
    if (dist < dpadRadius_ * 0.25f) return Direction::None;

    // Cardinal snapping: divide into 4 quadrants at 45-degree diagonals
    float angle = std::atan2(dy, dx); // radians, 0 = right, pi/2 = down
    // Normalize to [0, 2pi)
    if (angle < 0) angle += 2.0f * 3.14159265f;

    // Right: [-45, 45), Down: [45, 135), Left: [135, 225), Up: [225, 315)
    if (angle < 0.7854f || angle >= 5.4978f) return Direction::Right;
    if (angle < 2.3562f) return Direction::Down;
    if (angle < 3.9270f) return Direction::Left;
    return Direction::Up;
}

void TouchControls::gatherTouches(float vpX, float vpY, float vpW, float vpH) {
    activeTouchCount_ = 0;
    int numDevices = SDL_GetNumTouchDevices();
    for (int d = 0; d < numDevices; ++d) {
        SDL_TouchID touchId = SDL_GetTouchDevice(d);
        int numFingers = SDL_GetNumTouchFingers(touchId);
        for (int f = 0; f < numFingers && activeTouchCount_ < MAX_TOUCHES; ++f) {
            SDL_Finger* finger = SDL_GetTouchFinger(touchId, f);
            if (!finger) continue;
            // Convert normalized [0,1] coordinates to viewport-relative pixels
            float screenX = finger->x * (vpX + vpW); // approximate — SDL gives window-relative
            float screenY = finger->y * (vpY + vpH);
            activeTouches_[activeTouchCount_++] = {
                static_cast<int>(finger->id),
                screenX - vpX,  // viewport-relative
                screenY - vpY,
                false
            };
        }
    }
}

void TouchControls::update(ActionMap& actionMap, float vpX, float vpY, float vpW, float vpH) {
    if (!enabled_) return;
    if (!buttonsInitialized_) initButtons();

    gatherTouches(vpX, vpY, vpW, vpH);

    // --- Compute layout positions ---
    dpadRadius_ = vpH * 0.18f;  // 18% of viewport height
    dpadCenterX_ = dpadRadius_ + vpW * 0.04f;  // left edge + padding
    dpadCenterY_ = vpH - dpadRadius_ - vpH * 0.08f; // bottom edge + padding

    float attackRadius = vpH * 0.09f;  // attack button slightly larger
    float skillRadius  = vpH * 0.065f; // skill buttons smaller

    // Compute button radii
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        buttons_[i].radius = (i == 0) ? attackRadius : skillRadius;
    }

    // --- Process D-pad ---
    Direction prevDir = dpadDirection_;
    dpadDirection_ = Direction::None;
    dpadFingerId_ = -1;

    for (int t = 0; t < activeTouchCount_; ++t) {
        auto& touch = activeTouches_[t];
        if (isInsideCircle(touch.x, touch.y, dpadCenterX_, dpadCenterY_, dpadRadius_ * 1.3f)) {
            dpadFingerId_ = touch.fingerId;
            dpadDirection_ = classifyDpadDirection(touch.x, touch.y);
            break;
        }
    }

    // Inject D-pad into ActionMap
    actionMap.setActionHeld(ActionId::MoveUp,    dpadDirection_ == Direction::Up);
    actionMap.setActionHeld(ActionId::MoveDown,  dpadDirection_ == Direction::Down);
    actionMap.setActionHeld(ActionId::MoveLeft,  dpadDirection_ == Direction::Left);
    actionMap.setActionHeld(ActionId::MoveRight, dpadDirection_ == Direction::Right);

    // Fire pressed events on direction change
    if (dpadDirection_ != prevDir && dpadDirection_ != Direction::None) {
        switch (dpadDirection_) {
            case Direction::Up:    actionMap.setActionPressed(ActionId::MoveUp); break;
            case Direction::Down:  actionMap.setActionPressed(ActionId::MoveDown); break;
            case Direction::Left:  actionMap.setActionPressed(ActionId::MoveLeft); break;
            case Direction::Right: actionMap.setActionPressed(ActionId::MoveRight); break;
            default: break;
        }
    }

    // --- Process action buttons ---
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        float bx = vpW - buttons_[i].offsetX * vpW;
        float by = vpH - buttons_[i].offsetY * vpH;
        bool wasPressed = buttons_[i].pressed;
        buttons_[i].pressed = false;
        buttons_[i].fingerId = -1;

        for (int t = 0; t < activeTouchCount_; ++t) {
            auto& touch = activeTouches_[t];
            if (touch.fingerId == dpadFingerId_) continue; // skip D-pad finger
            if (isInsideCircle(touch.x, touch.y, bx, by, buttons_[i].radius)) {
                buttons_[i].pressed = true;
                buttons_[i].fingerId = touch.fingerId;
                break;
            }
        }

        if (buttons_[i].pressed && !wasPressed) {
            actionMap.setActionPressed(buttons_[i].action);
        } else if (!buttons_[i].pressed && wasPressed) {
            actionMap.setActionReleased(buttons_[i].action);
        }
        actionMap.setActionHeld(buttons_[i].action, buttons_[i].pressed);
    }

    // --- World tap detection ---
    // Any touch not claimed by D-pad or buttons is a world tap
    worldTapActive_ = false;
    for (int t = 0; t < activeTouchCount_; ++t) {
        auto& touch = activeTouches_[t];
        if (touch.fingerId == dpadFingerId_) continue;
        bool onButton = false;
        for (int i = 0; i < BUTTON_COUNT; ++i) {
            float bx = vpW - buttons_[i].offsetX * vpW;
            float by = vpH - buttons_[i].offsetY * vpH;
            if (isInsideCircle(touch.x, touch.y, bx, by, buttons_[i].radius * 1.2f)) {
                onButton = true;
                break;
            }
        }
        if (!onButton && !isInsideCircle(touch.x, touch.y, dpadCenterX_, dpadCenterY_, dpadRadius_ * 1.3f)) {
            worldTapActive_ = true;
            worldTapPos_ = {touch.x + vpX, touch.y + vpY}; // screen-space
        }
    }
}

void TouchControls::render(float vpX, float vpY, float vpW, float vpH) {
    if (!enabled_) return;
    if (!buttonsInitialized_) initButtons();

    auto* drawList = ImGui::GetForegroundDrawList();

    // --- D-pad background circle ---
    float cx = vpX + dpadCenterX_;
    float cy = vpY + dpadCenterY_;
    drawList->AddCircleFilled(ImVec2(cx, cy), dpadRadius_, IM_COL32(0, 0, 0, 60), 32);
    drawList->AddCircle(ImVec2(cx, cy), dpadRadius_, IM_COL32(200, 200, 200, 100), 32, 2.0f);

    // D-pad directional indicator
    if (dpadDirection_ != Direction::None) {
        float indicatorDist = dpadRadius_ * 0.5f;
        float ix = cx, iy = cy;
        switch (dpadDirection_) {
            case Direction::Up:    iy -= indicatorDist; break;
            case Direction::Down:  iy += indicatorDist; break;
            case Direction::Left:  ix -= indicatorDist; break;
            case Direction::Right: ix += indicatorDist; break;
            default: break;
        }
        drawList->AddCircleFilled(ImVec2(ix, iy), dpadRadius_ * 0.25f, IM_COL32(255, 255, 255, 120), 16);
    }

    // --- Action buttons ---
    ImU32 buttonColor     = IM_COL32(0, 0, 0, 60);
    ImU32 buttonBorder    = IM_COL32(200, 200, 200, 100);
    ImU32 buttonPressed   = IM_COL32(255, 255, 255, 80);
    ImU32 attackColor     = IM_COL32(180, 50, 50, 80);
    ImU32 attackBorder    = IM_COL32(255, 100, 100, 150);

    const char* buttonLabels[] = {"ATK", "S1", "S2", "S3", "S4", "S5"};

    for (int i = 0; i < BUTTON_COUNT; ++i) {
        float bx = vpX + vpW - buttons_[i].offsetX * vpW;
        float by = vpY + vpH - buttons_[i].offsetY * vpH;
        float r  = buttons_[i].radius;

        ImU32 bg = (i == 0) ? attackColor : buttonColor;
        ImU32 border = (i == 0) ? attackBorder : buttonBorder;
        if (buttons_[i].pressed) bg = buttonPressed;

        drawList->AddCircleFilled(ImVec2(bx, by), r, bg, 24);
        drawList->AddCircle(ImVec2(bx, by), r, border, 24, 2.0f);

        // Label
        ImVec2 textSize = ImGui::CalcTextSize(buttonLabels[i]);
        drawList->AddText(ImVec2(bx - textSize.x * 0.5f, by - textSize.y * 0.5f),
                          IM_COL32(255, 255, 255, 180), buttonLabels[i]);
    }
}

} // namespace fate
