#include "engine/render/camera.h"
#include <algorithm>

namespace fate {

Camera::Camera() {
    position_ = Vec2::zero();
}

Mat4 Camera::getViewProjection() {
    if (dirty_) {
        float halfW = (virtualWidth_ * 0.5f) / zoom_;
        float halfH = (VIRTUAL_HEIGHT * 0.5f) / zoom_;

        Mat4 projection = Mat4::ortho(
            -halfW, halfW,
            -halfH, halfH,
            -1.0f, 1.0f
        );

        Mat4 view = Mat4::translate(-position_.x, -position_.y, 0.0f);

        viewProjection_ = projection * view;
        dirty_ = false;
    }
    return viewProjection_;
}

Vec2 Camera::screenToWorld(const Vec2& screen, int windowWidth, int windowHeight) const {
    // Normalize screen coords to -1..1
    float nx = (screen.x / windowWidth) * 2.0f - 1.0f;
    float ny = 1.0f - (screen.y / windowHeight) * 2.0f; // flip Y

    float halfW = (virtualWidth_ * 0.5f) / zoom_;
    float halfH = (VIRTUAL_HEIGHT * 0.5f) / zoom_;

    return {
        position_.x + nx * halfW,
        position_.y + ny * halfH
    };
}

Vec2 Camera::worldToScreen(const Vec2& world, int windowWidth, int windowHeight) const {
    float halfW = (virtualWidth_ * 0.5f) / zoom_;
    float halfH = (VIRTUAL_HEIGHT * 0.5f) / zoom_;

    float nx = (world.x - position_.x) / halfW;
    float ny = (world.y - position_.y) / halfH;

    return {
        (nx + 1.0f) * 0.5f * windowWidth,
        (1.0f - ny) * 0.5f * windowHeight
    };
}

Rect Camera::getVisibleBounds() const {
    float halfW = (virtualWidth_ * 0.5f) / zoom_;
    float halfH = (VIRTUAL_HEIGHT * 0.5f) / zoom_;
    return {
        position_.x - halfW,
        position_.y - halfH,
        halfW * 2.0f,
        halfH * 2.0f
    };
}

void Camera::follow(const Vec2& target, float smoothing, float deltaTime) {
    float t = 1.0f - std::pow(1.0f - smoothing, deltaTime * 60.0f);
    position_.x += (target.x - position_.x) * t;
    position_.y += (target.y - position_.y) * t;
    dirty_ = true;
}

} // namespace fate
