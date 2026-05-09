/**************************************************************************/
/*  camera.h                                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                          FateMMO Game Engine                           */
/*                       https://www.FateMMO.com                          */
/**************************************************************************/
/* Copyright (c) 2026-present FateMMO Game Engine contributors.           */
/* Copyright (c) 2026-present Caleb Kious.                                */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/
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

    // Set viewport pixel dimensions — adjusts visible width to match aspect ratio
    // while keeping VIRTUAL_HEIGHT fixed. Call each frame before getViewProjection().
    void setViewportSize(int w, int h) {
        if (w > 0 && h > 0) {
            float aspect = (float)w / (float)h;
            float newVW = VIRTUAL_HEIGHT * aspect;
            if (newVW != virtualWidth_) { virtualWidth_ = newVW; dirty_ = true; }
        }
    }

    Vec2 position() const { return position_; }
    float zoom() const { return zoom_; }
    float virtualWidth() const { return virtualWidth_; }

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
    float virtualWidth_ = VIRTUAL_WIDTH;  // adjusted by setViewportSize()
    Mat4 viewProjection_;
    bool dirty_ = true;
};

} // namespace fate
