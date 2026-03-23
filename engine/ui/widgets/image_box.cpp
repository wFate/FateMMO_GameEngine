#include "engine/ui/widgets/image_box.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <algorithm>

namespace fate {

ImageBox::ImageBox(const std::string& id) : UINode(id, "image_box") {}

void ImageBox::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect  = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Background (optional, from style)
    if (style.backgroundColor.a > 0.0f) {
        Color bg = style.backgroundColor;
        bg.a *= style.opacity;
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                       {rect.w, rect.h}, bg, d);
    }

    // Draw texture if available
    if (!textureKey.empty()) {
        auto tex = TextureCache::instance().get(textureKey);
        if (!tex) tex = TextureCache::instance().load(textureKey);

        if (tex && tex->width() > 0 && tex->height() > 0) {
            SpriteDrawParams params;
            params.sourceRect = sourceRect;
            params.depth = d + 0.1f;

            // Apply tint with style opacity
            params.color = tint;
            params.color.a *= style.opacity;

            if (fitMode == ImageFitMode::Stretch) {
                params.position = {rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
                params.size     = {rect.w, rect.h};
            } else {
                // Fit: preserve aspect ratio, center within widget
                float texW = static_cast<float>(tex->width())  * sourceRect.w;
                float texH = static_cast<float>(tex->height()) * sourceRect.h;
                float scaleX = rect.w / texW;
                float scaleY = rect.h / texH;
                float scale  = std::min(scaleX, scaleY);

                float drawW = texW * scale;
                float drawH = texH * scale;

                params.position = {rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
                params.size     = {drawW, drawH};
            }

            batch.draw(tex, params);
        }
    }

    renderChildren(batch, sdf);
}

} // namespace fate
