#include "engine/render/tile_texture_array.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"
#include "engine/core/logger.h"

namespace fate {

bool TileTextureArray::create(int tileWidth, int tileHeight, int maxLayers) {
    if (texId_) destroy();

    tileW_ = tileWidth;
    tileH_ = tileHeight;
    maxLayers_ = maxLayers;
    nextLayer_ = 0;

    glGenTextures(1, &texId_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texId_);

    // Allocate storage for all layers (no pixel data yet)
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8,
                 tileW_, tileH_, maxLayers_,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // Pixel-perfect filtering for pixel art
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Clamp to edge -- each layer is independent, no bleeding possible
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    LOG_INFO("TileTextureArray", "Created %dx%d array texture with %d max layers",
             tileW_, tileH_, maxLayers_);
    return true;
}

void TileTextureArray::destroy() {
    if (texId_) {
        glDeleteTextures(1, &texId_);
        texId_ = 0;
    }
    tileW_ = 0;
    tileH_ = 0;
    maxLayers_ = 0;
    nextLayer_ = 0;
    gidToLayer_.clear();
}

int TileTextureArray::addTile(const uint8_t* rgbaPixels) {
    if (!texId_ || nextLayer_ >= maxLayers_) {
        LOG_ERROR("TileTextureArray", "Cannot add tile: %s",
                  !texId_ ? "not created" : "max layers reached");
        return -1;
    }

    int layer = nextLayer_++;

    glBindTexture(GL_TEXTURE_2D_ARRAY, texId_);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                    0, 0, layer,          // x, y, z offsets
                    tileW_, tileH_, 1,    // width, height, depth
                    GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    return layer;
}

void TileTextureArray::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texId_);
}

void TileTextureArray::unbind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

int TileTextureArray::gidToLayer(int gid) const {
    auto it = gidToLayer_.find(gid);
    return (it != gidToLayer_.end()) ? it->second : -1;
}

void TileTextureArray::setGidMapping(int gid, int layer) {
    gidToLayer_[gid] = layer;
}

} // namespace fate
