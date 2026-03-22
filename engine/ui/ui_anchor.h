#pragma once
#include "engine/core/types.h"
#include "engine/render/nine_slice.h"

namespace fate {

enum class AnchorPreset : uint8_t {
    TopLeft,      TopCenter,    TopRight,
    CenterLeft,   Center,       CenterRight,
    BottomLeft,   BottomCenter, BottomRight,
    StretchX,     // stretch horizontally, fixed height
    StretchY,     // stretch vertically, fixed width
    StretchAll    // fill parent
};

struct UIAnchor {
    AnchorPreset preset = AnchorPreset::TopLeft;
    Vec2 offset;          // pixel offset from anchor point
    Vec2 size;            // explicit size (0 = inherit/stretch)
    Vec4 margin;          // x=top, y=right, z=bottom, w=left
    Vec4 padding;         // x=top, y=right, z=bottom, w=left (for containers)
};

} // namespace fate
