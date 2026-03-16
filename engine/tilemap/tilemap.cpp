#include "engine/tilemap/tilemap.h"
#include "engine/core/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

namespace fate {

bool Tilemap::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Tilemap", "Cannot open: %s", path.c_str());
        return false;
    }

    // Extract base directory for relative image paths
    size_t lastSlash = path.find_last_of("/\\");
    basePath_ = (lastSlash != std::string::npos) ? path.substr(0, lastSlash + 1) : "";

    try {
        nlohmann::json root = nlohmann::json::parse(file);

        mapWidth_ = root.value("width", 0);
        mapHeight_ = root.value("height", 0);
        tileWidth_ = root.value("tilewidth", 32);
        tileHeight_ = root.value("tileheight", 32);

        // Load tilesets
        if (root.contains("tilesets")) {
            for (auto& ts : root["tilesets"]) {
                Tileset tileset;
                tileset.name = ts.value("name", "");
                tileset.firstGid = ts.value("firstgid", 1);
                tileset.tileWidth = ts.value("tilewidth", tileWidth_);
                tileset.tileHeight = ts.value("tileheight", tileHeight_);
                tileset.columns = ts.value("columns", 1);
                tileset.tileCount = ts.value("tilecount", 0);

                // Image path (relative to the JSON file)
                std::string imagePath = ts.value("image", "");
                if (!imagePath.empty()) {
                    tileset.imagePath = basePath_ + imagePath;
                    tileset.texture = TextureCache::instance().load(tileset.imagePath);
                    if (!tileset.texture) {
                        LOG_WARN("Tilemap", "Failed to load tileset image: %s", tileset.imagePath.c_str());
                    }
                }

                tilesets_.push_back(std::move(tileset));
            }
        }

        // Sort tilesets by firstGid descending (for lookup)
        std::sort(tilesets_.begin(), tilesets_.end(),
            [](const Tileset& a, const Tileset& b) { return a.firstGid > b.firstGid; });

        // Load layers
        if (root.contains("layers")) {
            for (auto& layer : root["layers"]) {
                std::string type = layer.value("type", "");

                if (type == "tilelayer") {
                    TilemapLayer tl;
                    tl.name = layer.value("name", "");
                    tl.width = layer.value("width", mapWidth_);
                    tl.height = layer.value("height", mapHeight_);
                    tl.visible = layer.value("visible", true);
                    tl.opacity = layer.value("opacity", 1.0f);

                    // Check for collision layer (by name convention)
                    std::string nameLower = tl.name;
                    for (auto& c : nameLower) c = (char)tolower(c);
                    tl.isCollisionLayer = (nameLower.find("collision") != std::string::npos ||
                                           nameLower.find("wall") != std::string::npos ||
                                           nameLower.find("block") != std::string::npos);

                    if (layer.contains("data") && layer["data"].is_array()) {
                        for (auto& val : layer["data"]) {
                            tl.data.push_back(val.get<int>());
                        }
                    }

                    layers_.push_back(std::move(tl));

                } else if (type == "objectgroup") {
                    if (layer.contains("objects")) {
                        for (auto& obj : layer["objects"]) {
                            TilemapObject to;
                            to.name = obj.value("name", "");
                            to.type = obj.value("type", "");
                            to.x = obj.value("x", 0.0f);
                            to.y = obj.value("y", 0.0f);
                            to.width = obj.value("width", 0.0f);
                            to.height = obj.value("height", 0.0f);

                            if (obj.contains("properties")) {
                                for (auto& prop : obj["properties"]) {
                                    std::string pname = prop.value("name", "");
                                    std::string pval = prop.value("value", "");
                                    to.properties[pname] = pval;
                                }
                            }

                            objects_.push_back(std::move(to));
                        }
                    }
                }
            }
        }

        LOG_INFO("Tilemap", "Loaded '%s': %dx%d tiles, %zu layers, %zu tilesets, %zu objects",
                 path.c_str(), mapWidth_, mapHeight_, layers_.size(),
                 tilesets_.size(), objects_.size());
        return true;

    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("Tilemap", "JSON parse error: %s", e.what());
        return false;
    }
}

void Tilemap::render(SpriteBatch& batch, Camera& camera, float depth) {
    Rect visible = camera.getVisibleBounds();

    for (auto& layer : layers_) {
        if (!layer.visible || layer.isCollisionLayer) continue;

        // Calculate visible tile range
        int startCol = (int)((visible.x - origin.x) / tileWidth_) - 1;
        int startRow = (int)((visible.y - origin.y) / tileHeight_) - 1;
        int endCol = (int)((visible.x + visible.w - origin.x) / tileWidth_) + 1;
        int endRow = (int)((visible.y + visible.h - origin.y) / tileHeight_) + 1;

        if (startCol < 0) startCol = 0;
        if (startRow < 0) startRow = 0;
        if (endCol >= layer.width) endCol = layer.width - 1;
        if (endRow >= layer.height) endRow = layer.height - 1;

        for (int row = startRow; row <= endRow; row++) {
            for (int col = startCol; col <= endCol; col++) {
                int index = row * layer.width + col;
                if (index < 0 || index >= (int)layer.data.size()) continue;

                int gid = layer.data[index];
                if (gid <= 0) continue;

                const Tileset* ts = findTileset(gid);
                if (!ts || !ts->texture) continue;

                int localId = gid - ts->firstGid;
                Rect uv = ts->getTileUV(localId);
                Vec2 worldPos = tileToWorld(col, row);

                SpriteDrawParams params;
                params.position = worldPos;
                params.size = {(float)tileWidth_, (float)tileHeight_};
                params.sourceRect = uv;
                params.color = Color(1, 1, 1, layer.opacity);
                params.depth = depth;

                batch.draw(ts->texture, params);
            }
        }

        depth += 0.1f; // each layer slightly in front
    }
}

bool Tilemap::checkCollision(const Rect& worldRect) const {
    for (auto& layer : layers_) {
        if (!layer.isCollisionLayer) continue;

        // Convert world rect to tile range
        int startCol = (int)((worldRect.x - origin.x) / tileWidth_);
        int startRow = (int)((worldRect.y - origin.y) / tileHeight_);
        int endCol = (int)((worldRect.x + worldRect.w - origin.x) / tileWidth_);
        int endRow = (int)((worldRect.y + worldRect.h - origin.y) / tileHeight_);

        if (startCol < 0) startCol = 0;
        if (startRow < 0) startRow = 0;
        if (endCol >= layer.width) endCol = layer.width - 1;
        if (endRow >= layer.height) endRow = layer.height - 1;

        for (int row = startRow; row <= endRow; row++) {
            for (int col = startCol; col <= endCol; col++) {
                int index = row * layer.width + col;
                if (index >= 0 && index < (int)layer.data.size() && layer.data[index] > 0) {
                    return true; // hit a collision tile
                }
            }
        }
    }
    return false;
}

int Tilemap::getTileAt(const std::string& layerName, float worldX, float worldY) const {
    for (auto& layer : layers_) {
        if (layer.name != layerName) continue;
        int col = (int)((worldX - origin.x) / tileWidth_);
        int row = (int)((worldY - origin.y) / tileHeight_);
        if (col < 0 || col >= layer.width || row < 0 || row >= layer.height) return 0;
        int index = row * layer.width + col;
        return (index < (int)layer.data.size()) ? layer.data[index] : 0;
    }
    return 0;
}

const Tileset* Tilemap::findTileset(int gid) const {
    // Tilesets are sorted by firstGid descending
    for (auto& ts : tilesets_) {
        if (gid >= ts.firstGid) return &ts;
    }
    return nullptr;
}

Vec2 Tilemap::tileToWorld(int col, int row) const {
    // Tile center position in world space
    return {
        origin.x + col * tileWidth_ + tileWidth_ * 0.5f,
        origin.y + row * tileHeight_ + tileHeight_ * 0.5f
    };
}

} // namespace fate
