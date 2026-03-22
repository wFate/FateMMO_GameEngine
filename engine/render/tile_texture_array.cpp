#include "engine/render/tile_texture_array.h"
#ifndef FATEMMO_METAL
#include "engine/render/gfx/backend/gl/gl_loader.h"
#endif
#ifdef FATEMMO_METAL
#import <Metal/Metal.h>
#endif
#include "engine/core/logger.h"

namespace fate {

bool TileTextureArray::create(int tileWidth, int tileHeight, int maxLayers) {
#ifndef FATEMMO_METAL
    if (texId_) destroy();
#else
    if (metalTex_) destroy();
#endif

    tileW_ = tileWidth;
    tileH_ = tileHeight;
    maxLayers_ = maxLayers;
    nextLayer_ = 0;

#ifndef FATEMMO_METAL
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
#else
    // Metal: create a 2D array texture
    id<MTLDevice> mtlDevice = MTLCreateSystemDefaultDevice();
    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                          width:(NSUInteger)tileW_
                                                         height:(NSUInteger)tileH_
                                                      mipmapped:NO];
    desc.textureType = MTLTextureType2DArray;
    desc.arrayLength = (NSUInteger)maxLayers_;
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [mtlDevice newTextureWithDescriptor:desc];
    metalTex_ = (__bridge_retained void*)tex;
#endif

    LOG_INFO("TileTextureArray", "Created %dx%d array texture with %d max layers",
             tileW_, tileH_, maxLayers_);
    return true;
}

void TileTextureArray::destroy() {
#ifndef FATEMMO_METAL
    if (texId_) {
        glDeleteTextures(1, &texId_);
        texId_ = 0;
    }
#else
    if (metalTex_) {
        CFRelease(metalTex_);
        metalTex_ = nullptr;
    }
#endif
    tileW_ = 0;
    tileH_ = 0;
    maxLayers_ = 0;
    nextLayer_ = 0;
    gidToLayer_.clear();
}

int TileTextureArray::addTile(const uint8_t* rgbaPixels) {
#ifndef FATEMMO_METAL
    if (!texId_ || nextLayer_ >= maxLayers_) {
        LOG_ERROR("TileTextureArray", "Cannot add tile: %s",
                  !texId_ ? "not created" : "max layers reached");
        return -1;
    }
#else
    if (!metalTex_ || nextLayer_ >= maxLayers_) {
        LOG_ERROR("TileTextureArray", "Cannot add tile: %s",
                  !metalTex_ ? "not created" : "max layers reached");
        return -1;
    }
#endif

    int layer = nextLayer_++;

#ifndef FATEMMO_METAL
    glBindTexture(GL_TEXTURE_2D_ARRAY, texId_);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                    0, 0, layer,          // x, y, z offsets
                    tileW_, tileH_, 1,    // width, height, depth
                    GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
#else
    id<MTLTexture> tex = (__bridge id<MTLTexture>)metalTex_;
    MTLRegion region = MTLRegionMake2D(0, 0, (NSUInteger)tileW_, (NSUInteger)tileH_);
    [tex replaceRegion:region
           mipmapLevel:0
                 slice:(NSUInteger)layer
             withBytes:rgbaPixels
           bytesPerRow:(NSUInteger)(tileW_ * 4)
         bytesPerImage:(NSUInteger)(tileW_ * tileH_ * 4)];
#endif

    return layer;
}

void TileTextureArray::bind(int unit) const {
#ifndef FATEMMO_METAL
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texId_);
#endif
    // Metal: texture bound per-encoder in ChunkRenderer -- no-op here
}

void TileTextureArray::unbind(int unit) const {
#ifndef FATEMMO_METAL
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
#endif
    // Metal: no-op here
}

int TileTextureArray::gidToLayer(int gid) const {
    auto it = gidToLayer_.find(gid);
    return (it != gidToLayer_.end()) ? it->second : -1;
}

void TileTextureArray::setGidMapping(int gid, int layer) {
    gidToLayer_[gid] = layer;
}

} // namespace fate
