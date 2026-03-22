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
    Vec2 size;            // explicit size in pixels (0 = inherit/stretch)
    Vec2 offsetPercent;   // offset as fraction of parent (0-1), added to pixel offset
    Vec2 sizePercent;     // size as fraction of parent (0-1), overrides pixel size when > 0
    Vec4 margin;          // x=top, y=right, z=bottom, w=left
    Vec4 padding;         // x=top, y=right, z=bottom, w=left (for containers)
};

} // namespace fate
