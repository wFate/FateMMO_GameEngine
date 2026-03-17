#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/core/types.h"
#include "engine/ecs/reflect.h"
#include "engine/render/texture.h"
#include <memory>
#include <string>

namespace fate {

// Sprite component - renders a textured quad
struct SpriteComponent {
    FATE_COMPONENT(SpriteComponent)

    std::shared_ptr<Texture> texture;
    std::string texturePath; // for serialization/debugging

    Rect sourceRect = {0, 0, 1, 1}; // UV coords (0-1), default = full texture
    Vec2 size = {32.0f, 32.0f};     // render size in world pixels
    Color tint = Color::white();
    bool flipX = false;
    bool flipY = false;

    // Sprite sheet animation
    int frameWidth = 0;   // pixel width per frame (0 = use full texture)
    int frameHeight = 0;  // pixel height per frame
    int currentFrame = 0;
    int totalFrames = 1;
    int columns = 1;      // frames per row in spritesheet

    // Sets source rect based on current frame in a spritesheet
    void updateSourceRect() {
        if (frameWidth <= 0 || frameHeight <= 0 || !texture) return;

        int col = currentFrame % columns;
        int row = currentFrame / columns;
        float texW = (float)texture->width();
        float texH = (float)texture->height();

        sourceRect.x = (col * frameWidth) / texW;
        sourceRect.y = (row * frameHeight) / texH;
        sourceRect.w = frameWidth / texW;
        sourceRect.h = frameHeight / texH;
    }
};

} // namespace fate

FATE_REFLECT(fate::SpriteComponent,
    FATE_FIELD(texturePath, String),
    FATE_FIELD(sourceRect, Rect),
    FATE_FIELD(size, Vec2),
    FATE_FIELD(tint, Color),
    FATE_FIELD(flipX, Bool),
    FATE_FIELD(flipY, Bool),
    FATE_FIELD(frameWidth, Int),
    FATE_FIELD(frameHeight, Int),
    FATE_FIELD(currentFrame, Int),
    FATE_FIELD(totalFrames, Int),
    FATE_FIELD(columns, Int)
)
