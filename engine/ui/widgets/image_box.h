#pragma once
#include "engine/ui/ui_node.h"
#include "engine/render/texture.h"
#include <memory>

namespace fate {

enum class ImageFitMode : uint8_t {
    Stretch,  // fill entire widget rect (may distort)
    Fit       // preserve aspect ratio, letterbox within rect
};

class ImageBox : public UINode {
public:
    ImageBox(const std::string& id);

    void render(SpriteBatch& batch, SDFText& text) override;

    std::string textureKey;              // TextureCache path / key
    ImageFitMode fitMode = ImageFitMode::Fit;
    Color tint = Color::white();
    Rect sourceRect = {0, 0, 1, 1};     // UV rect (normalized 0-1)
};

} // namespace fate
