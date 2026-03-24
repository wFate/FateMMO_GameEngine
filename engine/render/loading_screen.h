#pragma once
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include "engine/render/texture.h"
#include <string>
#include <memory>

namespace fate {

class LoadingScreen {
public:
    void begin(const std::string& sceneName, int screenWidth, int screenHeight);
    void render(SpriteBatch& batch, SDFText& sdf, float progress,
                int screenWidth, int screenHeight);
    void end();

private:
    std::string sceneName_;
    std::string displayName_;
    std::shared_ptr<Texture> backgroundTex_;
};

} // namespace fate
