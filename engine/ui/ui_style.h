#pragma once
#include "engine/core/types.h"
#include "engine/render/nine_slice.h"
#include <string>

namespace fate {

struct UIStyle {
    std::string styleName;
    Color backgroundColor   = Color::clear();
    Color borderColor       = Color::clear();
    float borderWidth       = 0.0f;
    std::string backgroundTexture;   // texture atlas key for 9-slice
    NineSlice nineSlice;
    std::string fontName;
    float fontSize          = 14.0f;
    Color textColor         = Color::white();
    Color hoverColor        = Color::clear();   // clear = no override
    Color pressedColor      = Color::clear();
    Color disabledColor     = Color::clear();
    float opacity           = 1.0f;
};

} // namespace fate
