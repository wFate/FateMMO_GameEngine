#pragma once
#include "engine/core/types.h"
#include "engine/render/texture.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/camera.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace fate {

// A tileset loaded from a Tiled JSON tileset reference
struct Tileset {
    std::string name;
    std::string imagePath;
    std::shared_ptr<Texture> texture;
    int firstGid = 1;         // first global tile ID in this tileset
    int tileWidth = 32;
    int tileHeight = 32;
    int columns = 1;          // tiles per row in the tileset image
    int tileCount = 0;

    // Get UV rect for a tile given its local index (0-based)
    Rect getTileUV(int localIndex) const {
        if (!texture || columns <= 0) return {0, 0, 1, 1};
        int col = localIndex % columns;
        int row = localIndex / columns;
        float texW = (float)texture->width();
        float texH = (float)texture->height();
        return {
            (col * tileWidth) / texW,
            (row * tileHeight) / texH,
            tileWidth / texW,
            tileHeight / texH
        };
    }
};

// A single layer in the tilemap
struct TilemapLayer {
    std::string name;
    int width = 0;            // tiles wide
    int height = 0;           // tiles tall
    std::vector<int> data;    // tile GIDs (0 = empty)
    bool visible = true;
    float opacity = 1.0f;
    bool isCollisionLayer = false; // if true, non-zero tiles block movement
};

// Object from Tiled's object layer (spawn points, triggers, etc.)
struct TilemapObject {
    std::string name;
    std::string type;
    float x = 0, y = 0;
    float width = 0, height = 0;
    std::unordered_map<std::string, std::string> properties;
};

// A complete tilemap loaded from Tiled JSON
class Tilemap {
public:
    bool loadFromFile(const std::string& path);

    void render(SpriteBatch& batch, Camera& camera, float depth = -10.0f);

    // Collision: check if a world-space rect overlaps any collision tile
    bool checkCollision(const Rect& worldRect) const;

    // Get tile at world position (returns GID, 0 = empty)
    int getTileAt(const std::string& layerName, float worldX, float worldY) const;

    // Accessors
    int widthInTiles() const { return mapWidth_; }
    int heightInTiles() const { return mapHeight_; }
    int tileWidth() const { return tileWidth_; }
    int tileHeight() const { return tileHeight_; }
    float worldWidth() const { return mapWidth_ * (float)tileWidth_; }
    float worldHeight() const { return mapHeight_ * (float)tileHeight_; }
    const std::vector<TilemapObject>& objects() const { return objects_; }

    // World origin offset (tilemap (0,0) in world coords)
    Vec2 origin;

private:
    int mapWidth_ = 0;
    int mapHeight_ = 0;
    int tileWidth_ = 32;
    int tileHeight_ = 32;

    std::vector<Tileset> tilesets_;
    std::vector<TilemapLayer> layers_;
    std::vector<TilemapObject> objects_;
    std::string basePath_; // directory containing the .json file

    const Tileset* findTileset(int gid) const;
    Vec2 tileToWorld(int col, int row) const;
};

} // namespace fate
