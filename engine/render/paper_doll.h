#pragma once
#include "engine/render/sprite_batch.h"
#include "engine/render/texture.h"
#include "engine/core/types.h"
#include <array>
#include <memory>
#include <string>

namespace fate {

// Visual layers in draw order (south-facing default)
// Matches EquipmentSlot enum in game_types.h for the visible slots
enum class EquipLayer : uint8_t {
    Cloak = 0, Shoes, Armor, Body, Gloves, Hat, Weapon, COUNT
};

struct EquipVisual {
    std::string spritesheetPath;
    int paletteIndex = 0;
};

struct CharacterAppearance {
    uint8_t bodyType = 0;
    uint8_t direction = 0; // 0=south, 1=west, 2=north, 3=east
    std::array<EquipVisual, static_cast<size_t>(EquipLayer::COUNT)> layers;
};

// Draw order varies by facing direction
inline const EquipLayer* getDrawOrder(uint8_t direction, int& count) {
    static const EquipLayer southOrder[] = {
        EquipLayer::Cloak, EquipLayer::Shoes, EquipLayer::Armor,
        EquipLayer::Body, EquipLayer::Gloves, EquipLayer::Hat, EquipLayer::Weapon
    };
    static const EquipLayer northOrder[] = {
        EquipLayer::Weapon, EquipLayer::Body, EquipLayer::Armor,
        EquipLayer::Shoes, EquipLayer::Cloak, EquipLayer::Gloves, EquipLayer::Hat
    };
    count = 7;
    return (direction == 2) ? northOrder : southOrder;
}

// Renders all equipment layers for a character
// Each layer's spritesheet must share the same frame layout as the base body
inline void drawPaperDoll(SpriteBatch& batch,
                          const CharacterAppearance& appearance,
                          Vec2 position, Vec2 size,
                          int currentFrame, bool flipX,
                          float baseDepth) {
    int orderCount = 0;
    const EquipLayer* order = getDrawOrder(appearance.direction, orderCount);

    for (int i = 0; i < orderCount; ++i) {
        auto layerIdx = static_cast<size_t>(order[i]);
        const auto& visual = appearance.layers[layerIdx];
        if (visual.spritesheetPath.empty()) continue;

        auto tex = TextureCache::instance().load(visual.spritesheetPath);
        if (!tex) continue;

        SpriteDrawParams params;
        params.position = position;
        params.size = size;
        params.flipX = flipX;
        params.depth = baseDepth + i * 0.001f;

        // TODO: compute sourceRect from currentFrame + spritesheet metadata
        // For now, full texture (single-frame placeholder)

        batch.draw(tex, params);
    }
}

} // namespace fate
