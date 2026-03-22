#pragma once
#include <cstdint>
#include <unordered_map>

namespace fate {

// Wraps GL_TEXTURE_2D_ARRAY -- one layer per tile, zero bleeding
class TileTextureArray {
public:
    // Create the array texture with given tile dimensions and max layer count
    bool create(int tileWidth, int tileHeight, int maxLayers);
    void destroy();

    // Upload a single tile's RGBA pixels as a new layer. Returns the layer index.
    int addTile(const uint8_t* rgbaPixels);

    // Bind to a texture unit for rendering
    void bind(int unit = 0) const;
    void unbind(int unit = 0) const;

#ifndef FATEMMO_METAL
    unsigned int glId() const { return texId_; }
#else
    unsigned int glId() const { return 0; }
    void* metalTexture() const { return metalTex_; }
#endif

    int layerCount() const { return nextLayer_; }
    int tileWidth() const { return tileW_; }
    int tileHeight() const { return tileH_; }
    int maxLayers() const { return maxLayers_; }

    // Map a global tile ID (GID) to a layer index (-1 if not mapped)
    int gidToLayer(int gid) const;
    void setGidMapping(int gid, int layer);

private:
#ifndef FATEMMO_METAL
    unsigned int texId_ = 0;
#else
    void* metalTex_ = nullptr;
#endif
    int tileW_ = 0, tileH_ = 0;
    int maxLayers_ = 0;
    int nextLayer_ = 0;
    std::unordered_map<int, int> gidToLayer_; // GID -> array layer index
};

} // namespace fate
