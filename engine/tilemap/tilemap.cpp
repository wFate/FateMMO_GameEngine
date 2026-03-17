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

        chunkManager_.buildFromLayers(layers_, mapWidth_, mapHeight_);

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

    // Update chunk states based on camera proximity
    chunkManager_.updateChunkStates(visible, origin, tileWidth_, tileHeight_);

    for (auto& cl : chunkManager_.layers()) {
        if (!cl.visible || cl.isCollisionLayer) continue;

        for (auto& chunk : cl.chunks) {
            if (chunk.state != ChunkState::Active) continue;

            // Frustum-cull the entire chunk
            float chunkWorldX = origin.x + chunk.chunkX * CHUNK_SIZE * tileWidth_;
            float chunkWorldY = origin.y + chunk.chunkY * CHUNK_SIZE * tileHeight_;
            float chunkWorldW = CHUNK_SIZE * (float)tileWidth_;
            float chunkWorldH = CHUNK_SIZE * (float)tileHeight_;
            Rect chunkBounds(chunkWorldX, chunkWorldY, chunkWorldW, chunkWorldH);

            if (!visible.overlaps(chunkBounds)) continue;

            // Render each tile in this active chunk
            for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
                for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                    int gid = chunk.tiles[ly * CHUNK_SIZE + lx];
                    if (gid <= 0) continue;

                    const Tileset* ts = findTileset(gid);
                    if (!ts || !ts->texture) continue;

                    int globalCol = chunk.chunkX * CHUNK_SIZE + lx;
                    int globalRow = chunk.chunkY * CHUNK_SIZE + ly;

                    int localId = gid - ts->firstGid;
                    Rect uv = ts->getTileUV(localId);
                    Vec2 worldPos = tileToWorld(globalCol, globalRow);

                    SpriteDrawParams params;
                    params.position = worldPos;
                    params.size = {(float)tileWidth_, (float)tileHeight_};
                    params.sourceRect = uv;
                    params.color = Color(1, 1, 1, cl.opacity);
                    params.depth = depth;

                    batch.draw(ts->texture, params);
                }
            }
        }

        depth += 0.1f; // each layer slightly in front
    }
}

bool Tilemap::checkCollision(const Rect& worldRect) const {
    return chunkManager_.checkCollision(worldRect, origin, tileWidth_, tileHeight_);
}

int Tilemap::getTileAt(const std::string& layerName, float worldX, float worldY) const {
    return chunkManager_.getTileAt(layerName, worldX, worldY, origin, tileWidth_, tileHeight_);
}

// ChunkManager::buildFromLayers — implemented here where TilemapLayer is fully defined
void ChunkManager::buildFromLayers(const std::vector<TilemapLayer>& layers,
                                    int mapWidth, int mapHeight) {
    chunkLayers_.clear();

    int wChunks = (mapWidth  + CHUNK_SIZE - 1) / CHUNK_SIZE;
    int hChunks = (mapHeight + CHUNK_SIZE - 1) / CHUNK_SIZE;

    for (int li = 0; li < (int)layers.size(); ++li) {
        const TilemapLayer& src = layers[li];

        ChunkLayer cl;
        cl.name             = src.name;
        cl.visible          = src.visible;
        cl.opacity          = src.opacity;
        cl.isCollisionLayer = src.isCollisionLayer;
        cl.widthInChunks    = wChunks;
        cl.heightInChunks   = hChunks;
        cl.chunks.resize(wChunks * hChunks);

        for (int cy = 0; cy < hChunks; ++cy) {
            for (int cx = 0; cx < wChunks; ++cx) {
                ChunkData& cd = cl.chunks[cy * wChunks + cx];
                cd.chunkX     = cx;
                cd.chunkY     = cy;
                cd.layerIndex = li;
                cd.state      = ChunkState::Active;
                cd.dirty      = false;
                cd.tiles.resize(CHUNK_SIZE * CHUNK_SIZE, 0);

                int tileStartX = cx * CHUNK_SIZE;
                int tileStartY = cy * CHUNK_SIZE;

                for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
                    int srcRow = tileStartY + ly;
                    if (srcRow >= src.height) break;
                    for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                        int srcCol = tileStartX + lx;
                        if (srcCol >= src.width) break;
                        int srcIdx = srcRow * src.width + srcCol;
                        if (srcIdx >= 0 && srcIdx < (int)src.data.size()) {
                            cd.tiles[ly * CHUNK_SIZE + lx] = src.data[srcIdx];
                        }
                    }
                }
            }
        }

        chunkLayers_.push_back(std::move(cl));
    }
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
