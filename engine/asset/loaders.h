#pragma once
#include "engine/asset/asset_registry.h"

namespace fate {

AssetLoader makeTextureLoader();
AssetLoader makeJsonLoader();
AssetLoader makeShaderLoader();

} // namespace fate
