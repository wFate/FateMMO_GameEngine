// engine/editor/editor_tile.cpp
//
// Engine-pure tile painter implementation extracted from editor.cpp so the
// FateDemo (FATE_HAS_GAME undefined) build can paint tilemaps. All functions
// here operate only on engine-side types: Transform / SpriteComponent /
// TileLayerComponent (engine/components/) plus the existing tilemap, render,
// undo, and prefab subsystems. No game/ headers are required.
//
// Methods previously lived inside the giant FATE_HAS_GAME #ifdef block in
// editor.cpp; the corresponding no-op stubs in the !FATE_HAS_GAME branch
// have been deleted alongside this move.

#include "engine/editor/editor.h"
#include "engine/editor/tile_tools.h"
#include "engine/editor/undo.h"
#include "engine/components/transform.h"
#include "engine/components/sprite_component.h"
#include "engine/components/tile_layer_component.h"
#include "engine/render/texture.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/camera.h"
#include "engine/render/fullscreen_quad.h"
#include "engine/ecs/world.h"
#include "engine/ecs/entity.h"
#include "engine/ecs/prefab.h"
#include "engine/core/logger.h"
#ifdef FATE_HAS_GAME
#include "game/systems/spawn_system.h"  // SpawnZoneComponent — guarded inline below
#endif
#ifndef FATEMMO_METAL
#include "engine/render/gfx/backend/gl/gl_loader.h"
#endif

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace fate {

namespace fs = std::filesystem;

// ============================================================================
// Tile Palette — load
// ============================================================================

void Editor::loadTileset(const std::string& path, int tileSize) {
    paletteTexture_ = TextureCache::instance().load(path);
    if (!paletteTexture_) {
        LOG_ERROR("Editor", "Failed to load tileset: %s", path.c_str());
        return;
    }
    paletteTexturePath_ = path;
    paletteTileSize_ = tileSize;
    if (paletteTileSize_ < 1) paletteTileSize_ = 32;
    paletteColumns_ = paletteTexture_->width() / paletteTileSize_;
    paletteRows_ = paletteTexture_->height() / paletteTileSize_;
    if (paletteColumns_ < 1) paletteColumns_ = 1;
    if (paletteRows_ < 1) paletteRows_ = 1;
    selectedTileIndex_ = -1;
    currentTool_ = EditorTool::Move;
    LOG_INFO("Editor", "Loaded tileset: %s (%dx%d tiles, %dx%d px)",
             path.c_str(), paletteColumns_, paletteRows_,
             paletteTexture_->width(), paletteTexture_->height());
}

// ============================================================================
// Static helpers — paint one tile / paint collision / world snap
// ============================================================================

// Paint a single tile at world position, returning the undo command (caller manages undo stack).
// External linkage so editor.cpp's handleMouseUp (rect/line tool finalize) can call them.
std::unique_ptr<UndoCommand> paintOneTile(World* world,
    const Vec2& worldPos, int tileIndex, int paletteCols,
    int paletteTileSize, float gridSize,
    const std::shared_ptr<Texture>& paletteTexture,
    const std::string& paletteTexturePath,
    const std::string& tileLayer) {

    int col = tileIndex % paletteCols;
    int row = tileIndex / paletteCols;
    float texW = (float)paletteTexture->width();
    float texH = (float)paletteTexture->height();

    Rect srcRect = {
        (col * paletteTileSize) / texW,
        1.0f - ((row + 1) * paletteTileSize) / texH,
        paletteTileSize / texW,
        paletteTileSize / texH
    };

    // Check if there's already a tile from the SAME tileset + layer at this position.
    // Editable tile = has TileLayerComponent matching the target layer
    //               OR (legacy pre-TLC) entity tagged "ground" on the ground layer.
    std::unique_ptr<UndoCommand> result;
    bool replaced = false;
    world->forEach<Transform, SpriteComponent>(
        [&](Entity* entity, Transform* t, SpriteComponent* s) {
            if (replaced) return;
            auto* tlc = entity->getComponent<TileLayerComponent>();
            std::string entLayer = tlc ? tlc->layer : "ground";
            if (entLayer != tileLayer) return;
            if (!tlc && entity->tag() != "ground") return;
            if (std::abs(t->position.x - worldPos.x) > 1.0f ||
                std::abs(t->position.y - worldPos.y) > 1.0f) return;
            if (s->texturePath != paletteTexturePath) return;

            auto cmd = std::make_unique<PropertyCommand>();
            cmd->entityHandle = entity->handle();
            cmd->oldState = PrefabLibrary::entityToJson(entity);
            cmd->desc = "Paint Tile";
            s->sourceRect = srcRect;
            cmd->newState = PrefabLibrary::entityToJson(entity);
            result = std::move(cmd);
            replaced = true;
        }
    );

    if (!replaced) {
        float baseDepth = Editor::layerBaseDepth(tileLayer);
        float tileDepth = baseDepth;
        world->forEach<Transform, SpriteComponent>(
            [&](Entity* entity, Transform* t, SpriteComponent*) {
                auto* etc = entity->getComponent<TileLayerComponent>();
                std::string el = etc ? etc->layer : "ground";
                if (el != tileLayer) return;
                if (!etc && entity->tag() != "ground") return;
                if (std::abs(t->position.x - worldPos.x) < 1.0f &&
                    std::abs(t->position.y - worldPos.y) < 1.0f) {
                    if (t->depth >= tileDepth) tileDepth = t->depth + 1.0f;
                }
            }
        );

        Entity* tile = world->createEntity("Tile");
        tile->setTag("ground");

        auto* transform = tile->addComponent<Transform>(worldPos);
        transform->depth = tileDepth;

        auto* sprite = tile->addComponent<SpriteComponent>();
        sprite->texture = paletteTexture;
        sprite->texturePath = paletteTexturePath;
        sprite->sourceRect = srcRect;
        sprite->size = {(float)paletteTileSize, (float)paletteTileSize};

        auto* tlc = tile->addComponent<TileLayerComponent>();
        tlc->layer = tileLayer;

        auto cmd = std::make_unique<CreateCommand>();
        cmd->createdHandle = tile->handle();
        cmd->entityData = PrefabLibrary::entityToJson(tile);
        result = std::move(cmd);
    }
    return result;
}

// Convert tile col/row to snapped world position
Vec2 tileToWorldCenter(int col, int row, float gridSize) {
    float half = gridSize * 0.5f;
    return { col * gridSize + half, row * gridSize + half };
}

// Stamp a collision tile at worldPos if one isn't already there. Used by both
// the brush loop and the rect/line tools so collision-layer authoring isn't
// gated on a palette texture (collision tiles render as a tinted square, not
// a sprite source rect).
std::unique_ptr<UndoCommand> paintOneCollisionTile(World* world,
                                                          const Vec2& worldPos,
                                                          float gridSize) {
    bool exists = false;
    world->forEach<Transform, TileLayerComponent>(
        [&](Entity*, Transform* t, TileLayerComponent* tlc) {
            if (tlc->layer == "collision" &&
                std::abs(t->position.x - worldPos.x) < 1.0f &&
                std::abs(t->position.y - worldPos.y) < 1.0f) {
                exists = true;
            }
        }
    );
    if (exists) return nullptr;

    Entity* tile = world->createEntity("CollisionTile");
    tile->setTag("ground");
    auto* transform = tile->addComponent<Transform>(worldPos);
    transform->depth = -1.0f;
    auto* sprite = tile->addComponent<SpriteComponent>();
    sprite->size = {gridSize, gridSize};
    sprite->tint = Color(1.0f, 0.2f, 0.2f, 0.35f);
    auto* tlc = tile->addComponent<TileLayerComponent>();
    tlc->layer = "collision";

    auto cmd = std::make_unique<CreateCommand>();
    cmd->createdHandle = tile->handle();
    cmd->entityData = PrefabLibrary::entityToJson(tile);
    return cmd;
}

// ============================================================================
// Tile Palette — paint
// ============================================================================

void Editor::paintTileAt(World* world, Camera* camera, const Vec2& screenPos,
                         int windowWidth, int windowHeight) {
    bool isCollisionLayer = (selectedTileLayer_ == "collision");
    if (!world || !camera) return;

    // Surface why a tile click had no effect — previously these gates returned
    // silently and the user had no way to know whether their click was being
    // ignored or whether something else was wrong. Collision layer never needs
    // a palette texture, only the other layers do.
    if (!isCollisionLayer) {
        if (!paletteTexture_) {
            lastToolStatus_ = "Tile tool: no tileset loaded — open Tile Palette and pick one";
            return;
        }
        if (paletteColumns_ <= 0 || paletteRows_ <= 0 || paletteTileSize_ <= 0) {
            lastToolStatus_ = "Tile tool: palette has no tiles — check tile size";
            return;
        }
        if (selectedTileIndex_ < 0) {
            lastToolStatus_ = "Tile tool: select a tile in the Tile Palette first";
            return;
        }
        if (selectedTileIndex_ >= paletteColumns_ * paletteRows_) {
            lastToolStatus_ = "Tile tool: selected tile index out of range — re-select after changing tile size";
            return;
        }
    }
    // Successful tile tool entry — clear stale status so it doesn't linger.
    lastToolStatus_.clear();

    Vec2 worldPos = camera->screenToWorld(screenPos, windowWidth, windowHeight);

    // Snap to grid center
    float half = gridSize_ * 0.5f;
    worldPos.x = std::floor(worldPos.x / gridSize_) * gridSize_ + half;
    worldPos.y = std::floor(worldPos.y / gridSize_) * gridSize_ + half;

    int tileCol = (int)std::floor(worldPos.x / gridSize_);
    int tileRow = (int)std::floor(worldPos.y / gridSize_);

    // --- Fill tool: flood fill on click ---
    if (currentTool_ == EditorTool::Fill) {
        // Collision layer has no source rects --fill is not meaningful
        if (isCollisionLayer) return;
        // Build a lookup of occupied tile positions (same tileset, same GID)
        int srcCol = selectedTileIndex_ % paletteColumns_;
        int srcRow = selectedTileIndex_ / paletteColumns_;
        float texW = (float)paletteTexture_->width();
        float texH = (float)paletteTexture_->height();
        Rect selectedSrcRect = {
            (srcCol * paletteTileSize_) / texW,
            1.0f - ((srcRow + 1) * paletteTileSize_) / texH,
            paletteTileSize_ / texW,
            paletteTileSize_ / texH
        };

        // Collect existing ground tile positions and their source rects
        auto pack = [](int c, int r) -> int64_t { return ((int64_t)r << 32) | (uint32_t)c; };
        std::unordered_map<int64_t, Rect> tileMap;
        int minCol = tileCol, maxCol = tileCol;
        int minRow = tileRow, maxRow = tileRow;

        world->forEach<Transform, SpriteComponent>(
            [&](Entity* entity, Transform* t, SpriteComponent* s) {
                auto* tlc = entity->getComponent<TileLayerComponent>();
                std::string entLayer = tlc ? tlc->layer : "ground";
                if (entLayer != selectedTileLayer_) return;
                if (!tlc && entity->tag() != "ground") return;
                if (s->texturePath != paletteTexturePath_) return;
                int tc = (int)std::floor(t->position.x / gridSize_);
                int tr = (int)std::floor(t->position.y / gridSize_);
                tileMap[pack(tc, tr)] = s->sourceRect;
                if (tc < minCol) minCol = tc;
                if (tc > maxCol) maxCol = tc;
                if (tr < minRow) minRow = tr;
                if (tr > maxRow) maxRow = tr;
            }
        );
        // Pad bounds by 1 so fill can expand one tile beyond existing edges
        minCol--; minRow--; maxCol += 2; maxRow += 2;

        // Fill target: tiles that have the same source rect as the clicked tile,
        // or empty tiles if the clicked tile was empty
        auto it = tileMap.find(pack(tileCol, tileRow));
        bool clickedEmpty = (it == tileMap.end());
        Rect clickedRect = clickedEmpty ? Rect{} : it->second;

        auto matchesFillTarget = [&](int c, int r) -> bool {
            auto found = tileMap.find(pack(c, r));
            if (clickedEmpty) return found == tileMap.end();
            if (found == tileMap.end()) return false;
            // Match if same source rect
            auto& sr = found->second;
            return std::abs(sr.x - clickedRect.x) < 0.001f &&
                   std::abs(sr.y - clickedRect.y) < 0.001f &&
                   std::abs(sr.w - clickedRect.w) < 0.001f &&
                   std::abs(sr.h - clickedRect.h) < 0.001f;
        };

        auto coords = floodFill(tileCol, tileRow, minCol, minRow, maxCol, maxRow, matchesFillTarget);
        if (coords.empty()) return;

        // Don't fill if same tile already
        if (!clickedEmpty) {
            if (std::abs(clickedRect.x - selectedSrcRect.x) < 0.001f &&
                std::abs(clickedRect.y - selectedSrcRect.y) < 0.001f) return;
        }

        auto compound = std::make_unique<CompoundCommand>();
        compound->desc = "Fill (" + std::to_string(coords.size()) + " tiles)";
        for (auto& coord : coords) {
            Vec2 wp = tileToWorldCenter(coord.x, coord.y, gridSize_);
            auto cmd = paintOneTile(world, wp, selectedTileIndex_, paletteColumns_,
                                    paletteTileSize_, gridSize_, paletteTexture_, paletteTexturePath_,
                                    selectedTileLayer_);
            if (cmd) compound->commands.push_back(std::move(cmd));
        }
        if (!compound->empty()) UndoSystem::instance().push(std::move(compound));
        return;
    }

    // --- RectFill / LineTool: record drag start on mousedown, track end ---
    if (currentTool_ == EditorTool::RectFill || currentTool_ == EditorTool::LineTool) {
        if (!isToolDragging_) {
            toolDragStart_ = {tileCol, tileRow};
            isToolDragging_ = true;
        }
        toolDragEnd_ = {tileCol, tileRow};
        // Actual painting happens in handleMouseUp
        return;
    }

    // --- Paint tool: NxN brush stamp (accumulated into brush stroke compound) ---
    int bhalf = brushSize_ / 2;
    for (int dy = 0; dy < brushSize_; ++dy) {
        for (int dx = 0; dx < brushSize_; ++dx) {
            Vec2 stampPos = {
                worldPos.x + (dx - bhalf) * gridSize_,
                worldPos.y + (dy - bhalf) * gridSize_
            };

            if (isCollisionLayer) {
                auto cmd = paintOneCollisionTile(world, stampPos, gridSize_);
                if (cmd) {
                    if (!pendingBrushStroke_) {
                        pendingBrushStroke_ = std::make_unique<CompoundCommand>();
                        pendingBrushStroke_->desc = "Paint brush stroke";
                    }
                    pendingBrushStroke_->commands.push_back(std::move(cmd));
                }
            } else {
                // Normal tile painting
                auto cmd = paintOneTile(world, stampPos, selectedTileIndex_, paletteColumns_,
                                        paletteTileSize_, gridSize_, paletteTexture_, paletteTexturePath_,
                                        selectedTileLayer_);
                if (cmd) {
                    if (!pendingBrushStroke_) {
                        pendingBrushStroke_ = std::make_unique<CompoundCommand>();
                        pendingBrushStroke_->desc = "Paint brush stroke";
                    }
                    pendingBrushStroke_->commands.push_back(std::move(cmd));
                }
            }
        }
    }
}

// ============================================================================
// Tile Palette — UI panel
// ============================================================================

void Editor::drawTilePalette(World* world, Camera* camera) {
    (void)world;
    (void)camera;
    if (!ImGui::Begin("Tile Palette", nullptr, ImGuiWindowFlags_None)) {
        ImGui::End();
        return; // collapsed
    }

    // Tileset selector
    if (ImGui::BeginCombo("##Tileset", paletteTexturePath_.empty() ? "Select tileset..." :
                          fs::path(paletteTexturePath_).filename().string().c_str())) {
        // Main game sets assetRoot_ to .../assets so /tiles resolves directly;
        // the demo points assetRoot_ at the project root (per v2 release notes
        // letting users browse engine source), so fall back to /assets/tiles.
        std::string tilesDir = assetRoot_ + "/tiles";
        if (!fs::exists(tilesDir)) {
            tilesDir = assetRoot_ + "/assets/tiles";
        }
        if (fs::exists(tilesDir)) {
            for (auto& entry : fs::recursive_directory_iterator(tilesDir)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".png" && ext != ".jpg") continue;

                std::string path = entry.path().string();
                std::replace(path.begin(), path.end(), '\\', '/');
                // Show relative path from tiles dir for clarity
                std::string name = fs::relative(entry.path(), tilesDir).string();
                std::replace(name.begin(), name.end(), '\\', '/');

                if (ImGui::Selectable(name.c_str(), paletteTexturePath_ == path)) {
                    loadTileset(path, paletteTileSize_);
                }
            }
        }
        ImGui::EndCombo();
    }

    // Tile size with reload on change. Demo binds the grid to the tile size so
    // painting/snap/brush preview/grid lines all stay aligned to the user's
    // tileset — main game keeps gridSize_ at 32 because all its assets are 32px.
    int prevSize = paletteTileSize_;
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("Tile px", &paletteTileSize_, 8, 16);
    if (paletteTileSize_ < 8) paletteTileSize_ = 8;
    if (paletteTileSize_ > 256) paletteTileSize_ = 256;
    if (paletteTileSize_ != prevSize) {
        gridSize_ = (float)paletteTileSize_;
        if (paletteTexture_) {
            paletteColumns_ = paletteTexture_->width() / paletteTileSize_;
            paletteRows_ = paletteTexture_->height() / paletteTileSize_;
            if (paletteColumns_ < 1) paletteColumns_ = 1;
            if (paletteRows_ < 1) paletteRows_ = 1;
            selectedTileIndex_ = -1;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(grid=%.0fpx)", gridSize_);

    // Brush size — measured in tiles (each tile = paletteTileSize_ px).
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("Brush", &brushSize_, 1, 1);
    if (brushSize_ < 1) brushSize_ = 1;
    if (brushSize_ > 5) brushSize_ = 5;
    ImGui::SameLine();
    ImGui::TextDisabled("(%dx%d tiles = %dx%d px)",
                       brushSize_, brushSize_,
                       brushSize_ * paletteTileSize_,
                       brushSize_ * paletteTileSize_);

    // Layer selector
    const char* layerNames[] = {"Ground", "Detail", "Fringe", "Collision"};
    const char* layerValues[] = {"ground", "detail", "fringe", "collision"};
    int currentLayerIdx = layerIndex(selectedTileLayer_);
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("Layer", &currentLayerIdx, layerNames, 4)) {
        selectedTileLayer_ = layerValues[currentLayerIdx];
    }

    // Layer visibility toggles
    ImGui::Text("Visible:");
    ImGui::SameLine();
    for (int i = 0; i < 4; ++i) {
        ImGui::SameLine();
        ImGui::PushID(i);
        ImGui::Checkbox(layerNames[i], &showLayer_[i]);
        ImGui::PopID();
    }

    if (!paletteTexture_ || paletteColumns_ <= 0 || paletteRows_ <= 0) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Place .png tilesets in assets/tiles/");
        ImGui::End();
        return;
    }

    // Tool selector. Each button activates that tile tool; the active tool's
    // button is tinted green. Erase + Fill/Rect/Line are also bound to X/G/U/L
    // for the main editor's keyboard workflow, but the demo surfaces them as
    // explicit buttons so first-time users can find them.
    auto toolButton = [&](const char* label, EditorTool tool, bool requiresTile) {
        bool active = (currentTool_ == tool);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        bool clicked = ImGui::Button(label);
        if (active) ImGui::PopStyleColor();
        if (clicked) {
            // Erase has no per-tileset gate; everything else requires a tile pick
            // (collision layer is a special case — it ignores the gate).
            bool gateOk = !requiresTile || selectedTileLayer_ == "collision" || selectedTileIndex_ >= 0;
            if (gateOk) currentTool_ = tool;
        }
    };
    toolButton("Paint", EditorTool::Paint, true); ImGui::SameLine();
    toolButton("Erase", EditorTool::Erase, false); ImGui::SameLine();
    toolButton("Fill",  EditorTool::Fill,  true); ImGui::SameLine();
    toolButton("Rect",  EditorTool::RectFill, true); ImGui::SameLine();
    toolButton("Line",  EditorTool::LineTool, true); ImGui::SameLine();
    if (ImGui::Button("Stop")) { currentTool_ = EditorTool::Move; selectedTileIndex_ = -1; }

    if (selectedTileIndex_ >= 0 && paletteColumns_ > 0) {
        ImGui::SameLine();
        ImGui::Text("(%d,%d)", selectedTileIndex_ % paletteColumns_, selectedTileIndex_ / paletteColumns_);
    }

    // Show why a tile click was rejected (e.g., "select a tile first").
    if (!lastToolStatus_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "%s", lastToolStatus_.c_str());
    }

    ImGui::Separator();

    // Scrollable tile grid
    ImGui::BeginChild("TileGrid", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    float availW = ImGui::GetContentRegionAvail().x;
    float tileDisplaySize = availW / (float)paletteColumns_ - 2.0f;
    if (tileDisplaySize < 16.0f) tileDisplaySize = 16.0f;
    if (tileDisplaySize > 64.0f) tileDisplaySize = 64.0f;

    ImTextureID texId = (ImTextureID)(intptr_t)paletteTexture_->id();
    float texW = (float)paletteTexture_->width();
    float texH = (float)paletteTexture_->height();
    int totalTiles = paletteColumns_ * paletteRows_;

    for (int row = 0; row < paletteRows_; row++) {
        for (int col = 0; col < paletteColumns_; col++) {
            int index = row * paletteColumns_ + col;
            if (index >= totalTiles) break;

            if (col > 0) ImGui::SameLine(0, 2);

            float u0 = (float)(col * paletteTileSize_) / texW;
            float u1 = (float)((col + 1) * paletteTileSize_) / texW;
            float v0 = 1.0f - (float)((row + 1) * paletteTileSize_) / texH;
            float v1 = 1.0f - (float)(row * paletteTileSize_) / texH;

            char btnId[32];
            snprintf(btnId, sizeof(btnId), "##t%d", index);

            bool selected = (index == selectedTileIndex_);
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 0.8f));

            if (ImGui::ImageButton(btnId, texId, ImVec2(tileDisplaySize, tileDisplaySize),
                                   ImVec2(u0, v1), ImVec2(u1, v0))) {
                selectedTileIndex_ = index;
                // Preserve current tile tool if active; otherwise default to Paint
                if (currentTool_ != EditorTool::Fill &&
                    currentTool_ != EditorTool::RectFill &&
                    currentTool_ != EditorTool::LineTool) {
                    currentTool_ = EditorTool::Paint;
                }
            }

            if (selected) ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

// ============================================================================
// Tile Palette — erase
// ============================================================================

void Editor::eraseTileAt(World* world, Camera* camera, const Vec2& screenPos,
                         int windowWidth, int windowHeight) {
    if (!world || !camera) return;

    Vec2 worldPos = camera->screenToWorld(screenPos, windowWidth, windowHeight);

    // Snap to grid center
    float half = gridSize_ * 0.5f;
    worldPos.x = std::floor(worldPos.x / gridSize_) * gridSize_ + half;
    worldPos.y = std::floor(worldPos.y / gridSize_) * gridSize_ + half;

    auto compound = std::make_unique<CompoundCommand>();
    compound->desc = "Erase (" + std::to_string(brushSize_ * brushSize_) + " tiles)";

    int bhalf = brushSize_ / 2;
    for (int dy = 0; dy < brushSize_; ++dy) {
        for (int dx = 0; dx < brushSize_; ++dx) {
            Vec2 erasePos = {
                worldPos.x + (dx - bhalf) * gridSize_,
                worldPos.y + (dy - bhalf) * gridSize_
            };

            Entity* nearest = nullptr;
            float nearestDist = 999999.0f;

            world->forEach<Transform, SpriteComponent>(
                [&](Entity* entity, Transform* t, SpriteComponent*) {
                    auto* tlc = entity->getComponent<TileLayerComponent>();
                    std::string entLayer = tlc ? tlc->layer : "ground";
                    if (entLayer != selectedTileLayer_) return;
                    if (!tlc && entity->tag() != "ground") return;
                    float dist = erasePos.distance(t->position);
                    if (dist < gridSize_ * 0.6f && dist < nearestDist) {
                        nearest = entity;
                        nearestDist = dist;
                    }
                }
            );

            if (nearest) {
                auto cmd = std::make_unique<DeleteCommand>();
                cmd->entityData = PrefabLibrary::entityToJson(nearest);
                cmd->deletedHandle = nearest->handle();
                compound->commands.push_back(std::move(cmd));
                world->destroyEntity(nearest->handle());
            }
        }
    }

    if (!compound->empty()) {
        UndoSystem::instance().push(std::move(compound));
    }
}

// ============================================================================
// Layer visibility + scene render helpers (lifted from FATE_HAS_GAME branch
// so the demo build can render painted tiles + grid + selection overlays).
// ============================================================================

void Editor::applyLayerVisibility(World* world) {
    if (!world) return;
    // NOTE: do NOT use world->forEach<SpriteComponent, TileLayerComponent>() —
    // that iterator filters out components with enabled=false (world.h:207),
    // which means once we hide a layer we can never see those tiles again to
    // unhide them. Walk entities directly so we can re-enable a sprite that we
    // previously disabled.
    world->forEachEntity([&](Entity* entity) {
        auto* tlc = entity->getComponent<TileLayerComponent>();
        if (!tlc) return;
        auto* s = entity->getComponent<SpriteComponent>();
        if (!s) return;
        int idx = layerIndex(tlc->layer);
        s->enabled = showLayer_[idx];
    });
}

void Editor::drawSceneGridShader(Camera* camera) {
    // Lazy-load the grid shader (only attempt once)
    if (!gridShaderLoaded_ && !gridShaderAttempted_) {
        gridShaderAttempted_ = true;
        gridShaderLoaded_ = gridShader_.loadFromFile(
            "assets/shaders/fullscreen_quad.vert",
            "assets/shaders/grid.frag");
        if (!gridShaderLoaded_) {
            LOG_ERROR("Editor", "Failed to load grid shader --falling back to SpriteBatch grid");
            return;
        }
    }
    if (!gridShaderLoaded_) return;

    Mat4 vp = const_cast<Camera*>(camera)->getViewProjection();
    Mat4 invVP = vp.inverse();

    gridShader_.bind();
    gridShader_.setMat4("u_inverseVP", invVP);
    gridShader_.setFloat("u_gridSize", gridSize_);
    gridShader_.setFloat("u_zoom", camera->zoom());
    gridShader_.setVec4("u_gridColor", 1.0f, 1.0f, 1.0f, 0.2f);
    gridShader_.setVec2("u_cameraPos", camera->position());

    // Additive/transparent blend for grid overlay
#ifndef FATEMMO_METAL
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
#endif

    FullscreenQuad::instance().draw();

    gridShader_.unbind();
}

void Editor::drawSceneGrid(SpriteBatch* batch, Camera* camera) {
    Rect visible = camera->getVisibleBounds();
    Mat4 vp = camera->getViewProjection();

    batch->begin(vp);

    // Grid lines at tile edges: 0, 32, 64... (tile centers are at 16, 48, 80...)
    float startX = std::floor(visible.x / gridSize_) * gridSize_;
    float startY = std::floor(visible.y / gridSize_) * gridSize_;

    Color gridColor(1.0f, 1.0f, 1.0f, 0.08f);

    for (float x = startX; x <= visible.x + visible.w; x += gridSize_) {
        batch->drawRect({x, visible.y + visible.h * 0.5f},
                       {1.0f, visible.h}, gridColor, -5.0f);
    }
    for (float y = startY; y <= visible.y + visible.h; y += gridSize_) {
        batch->drawRect({visible.x + visible.w * 0.5f, y},
                       {visible.w, 1.0f}, gridColor, -5.0f);
    }

    // Origin crosshair
    batch->drawRect({0, visible.y + visible.h * 0.5f},
                   {2.0f, visible.h}, Color(1, 0, 0, 0.2f), -4.0f);
    batch->drawRect({visible.x + visible.w * 0.5f, 0},
                   {visible.w, 2.0f}, Color(0, 1, 0, 0.2f), -4.0f);

    // Selection highlight + resize handles
    if (selectedEntity_) {
        auto* t = selectedEntity_->getComponent<Transform>();
        auto* s = selectedEntity_->getComponent<SpriteComponent>();

        // Determine bounds from sprite OR spawn zone (spawn zones are game-only;
        // see undo.cpp for the same pattern of inline guarding).
        float hw = 0, hh = 0;
#ifdef FATE_HAS_GAME
        auto* szSel = selectedEntity_->getComponent<SpawnZoneComponent>();
        if (szSel) {
            hw = szSel->config.size.x * 0.5f + 2.0f;
            hh = szSel->config.size.y * 0.5f + 2.0f;
        } else
#endif
        if (s && t) {
            hw = s->size.x * t->scale.x * 0.5f + 2.0f;
            hh = s->size.y * t->scale.y * 0.5f + 2.0f;
        }

        bool hasBounds =
#ifdef FATE_HAS_GAME
            (s || selectedEntity_->getComponent<SpawnZoneComponent>());
#else
            (s != nullptr);
#endif

        if (t && hasBounds && hw > 0) {
            Color sel(0.2f, 0.6f, 1.0f, 0.6f);

            // Border
            batch->drawRect({t->position.x, t->position.y + hh}, {hw * 2, 2.0f}, sel, 99.0f);
            batch->drawRect({t->position.x, t->position.y - hh}, {hw * 2, 2.0f}, sel, 99.0f);
            batch->drawRect({t->position.x - hw, t->position.y}, {2.0f, hh * 2}, sel, 99.0f);
            batch->drawRect({t->position.x + hw, t->position.y}, {2.0f, hh * 2}, sel, 99.0f);

            // Corner resize handles (white squares)
            float handleSize = 10.0f;
            Color handleColor(1.0f, 1.0f, 1.0f, 0.9f);
            batch->drawRect({t->position.x - hw, t->position.y + hh}, {handleSize, handleSize}, handleColor, 100.0f);
            batch->drawRect({t->position.x + hw, t->position.y + hh}, {handleSize, handleSize}, handleColor, 100.0f);
            batch->drawRect({t->position.x - hw, t->position.y - hh}, {handleSize, handleSize}, handleColor, 100.0f);
            batch->drawRect({t->position.x + hw, t->position.y - hh}, {handleSize, handleSize}, handleColor, 100.0f);

            // Edge midpoint handles
            float midSize = 7.0f;
            batch->drawRect({t->position.x, t->position.y + hh}, {midSize, midSize}, handleColor, 100.0f);
            batch->drawRect({t->position.x, t->position.y - hh}, {midSize, midSize}, handleColor, 100.0f);
            batch->drawRect({t->position.x - hw, t->position.y}, {midSize, midSize}, handleColor, 100.0f);
            batch->drawRect({t->position.x + hw, t->position.y}, {midSize, midSize}, handleColor, 100.0f);
        }
    }

    batch->end();
}

void Editor::drawSelectionOutlines(SpriteBatch* batch, Camera* camera) {
    if (!selectedEntity_ || !batch || !camera) return;

    // Single selection = orange, multi-selection = blue
    bool isMulti = selectedEntities_.size() > 1;
    Color outlineColor = isMulti ? Color(0.2f, 0.4f, 1.0f, 0.9f) : Color(1.0f, 0.55f, 0.0f, 0.9f);

    Mat4 vp = const_cast<Camera*>(camera)->getViewProjection();

    auto drawOutlineFor = [&](Entity* entity, Color col) {
        auto* t = entity->getComponent<Transform>();
        auto* s = entity->getComponent<SpriteComponent>();
        if (!t || !s) return;

        float hw = s->size.x * t->scale.x * 0.5f + 3.0f;
        float hh = s->size.y * t->scale.y * 0.5f + 3.0f;

        // Draw border rects as outline
        batch->begin(vp);
        // Top
        batch->drawRect({t->position.x, t->position.y + hh}, {hw * 2.0f, 2.0f}, col, 200.0f);
        // Bottom
        batch->drawRect({t->position.x, t->position.y - hh}, {hw * 2.0f, 2.0f}, col, 200.0f);
        // Left
        batch->drawRect({t->position.x - hw, t->position.y}, {2.0f, hh * 2.0f}, col, 200.0f);
        // Right
        batch->drawRect({t->position.x + hw, t->position.y}, {2.0f, hh * 2.0f}, col, 200.0f);
        batch->end();
    };

    if (isMulti) {
        for (auto& handle : selectedEntities_) {
            Entity* e = nullptr;
            if (dockWorld_) {
                e = dockWorld_->getEntity(handle);
            }
            if (e) drawOutlineFor(e, outlineColor);
        }
    } else {
        drawOutlineFor(selectedEntity_, outlineColor);
    }
}

// Draw the green/red square that follows the mouse in paint/erase mode.
// Extracted from the FATE_HAS_GAME renderScene so the demo can show the same
// preview without duplicating the math.
void Editor::drawTileBrushPreview(SpriteBatch* batch, Camera* camera) {
    if (!batch || !camera) return;
    if (!paused_) return;
    if (currentTool_ != EditorTool::Paint && currentTool_ != EditorTool::Erase) return;
    if (brushSize_ <= 0) return;

    ImVec2 imMouse = ImGui::GetMousePos();
    Vec2 mouseScreen = {imMouse.x - viewportPos_.x, imMouse.y - viewportPos_.y};
    Vec2 mouseWorld = camera->screenToWorld(mouseScreen, (int)viewportSize_.x, (int)viewportSize_.y);
    float half = gridSize_ * 0.5f;
    mouseWorld.x = std::floor(mouseWorld.x / gridSize_) * gridSize_ + half;
    mouseWorld.y = std::floor(mouseWorld.y / gridSize_) * gridSize_ + half;

    int bhalf = brushSize_ / 2;
    float totalSize = brushSize_ * gridSize_;
    // Top-left of the brush stamp area (mirrors paintTileAt's stamp grid:
    // bhalf is integer-truncated so even brushes anchor toward upper-left).
    Vec2 origin = {
        mouseWorld.x + (-bhalf) * gridSize_ - half,
        mouseWorld.y + (-bhalf) * gridSize_ - half
    };
    // SpriteBatch::drawRect uses CENTER, not top-left. Convert.
    Vec2 center = {origin.x + totalSize * 0.5f, origin.y + totalSize * 0.5f};

    Color previewColor = (currentTool_ == EditorTool::Erase)
        ? Color(1.0f, 0.3f, 0.3f, 0.3f)
        : Color(0.3f, 1.0f, 0.3f, 0.3f);

    Mat4 vp = camera->getViewProjection();
    batch->begin(vp);
    batch->drawRect(center, {totalSize, totalSize}, previewColor);
    batch->end();
}

// Iterate Transform+SpriteComponent and draw all enabled sprites. Used by the
// demo build (the FATE_HAS_GAME path uses game/systems/render_system instead).
void Editor::renderTilemap(SpriteBatch* batch, Camera* camera) {
    if (!batch || !camera || !dockWorld_) return;

    Mat4 vp = camera->getViewProjection();
    Rect visible = camera->getVisibleBounds();

    batch->begin(vp);
    dockWorld_->forEach<Transform, SpriteComponent>(
        [&](Entity*, Transform* t, SpriteComponent* s) {
            if (!s->enabled) return;
            // Collision tiles render as a tinted square — they have no texture
            // but still need the brush-stamp visualization.
            if (!s->texture && s->tint.a <= 0.0f) return;

            float halfW = s->size.x * t->scale.x * 0.5f;
            float halfH = s->size.y * t->scale.y * 0.5f;
            Rect bounds = {
                t->position.x - halfW,
                t->position.y - halfH,
                s->size.x * t->scale.x,
                s->size.y * t->scale.y
            };
            if (!bounds.overlaps(visible)) return;

            SpriteDrawParams params;
            params.position = t->position + s->renderOffset;
            params.size = s->size * t->scale;
            params.sourceRect = s->sourceRect;
            params.color = s->tint;
            params.rotation = t->rotation;
            params.depth = t->depth;
            params.flipX = s->flipX;
            params.flipY = s->flipY;

            if (s->texture) {
                batch->draw(s->texture, params);
            } else {
                // Untextured (collision overlay) — drawRect uses the tint color.
                batch->drawRect(t->position, s->size * t->scale, s->tint, t->depth);
            }
        }
    );
    batch->end();
}

// Demo-side dispatchers that route a viewport click/drag/release to the
// appropriate tile painter helper. The FATE_HAS_GAME branch handles tile clicks
// inline because they must compete with selection/drag/spawn-zone handlers.
void Editor::dispatchTileSceneClick(World* world, Camera* camera, const Vec2& screenPos,
                                     int windowWidth, int windowHeight) {
    if (!world || !camera) return;
    if (currentTool_ == EditorTool::Paint ||
        currentTool_ == EditorTool::Fill ||
        currentTool_ == EditorTool::RectFill ||
        currentTool_ == EditorTool::LineTool) {
        paintTileAt(world, camera, screenPos, windowWidth, windowHeight);
    } else if (currentTool_ == EditorTool::Erase) {
        eraseTileAt(world, camera, screenPos, windowWidth, windowHeight);
    }
}

void Editor::dispatchTileSceneDrag(Camera* camera, const Vec2& screenPos,
                                    int windowWidth, int windowHeight) {
    if (!camera || !dockWorld_) return;
    if (currentTool_ == EditorTool::Paint) {
        paintTileAt(dockWorld_, camera, screenPos, windowWidth, windowHeight);
    } else if (currentTool_ == EditorTool::Erase) {
        eraseTileAt(dockWorld_, camera, screenPos, windowWidth, windowHeight);
    } else if (currentTool_ == EditorTool::RectFill ||
               currentTool_ == EditorTool::LineTool) {
        // paintTileAt's RectFill/LineTool branch updates toolDragEnd_ in place.
        paintTileAt(dockWorld_, camera, screenPos, windowWidth, windowHeight);
    }
}

// Commits the brush stroke (Paint tool) and finalizes RectFill/LineTool drag.
// Mirrors the rect/line finalize at the bottom of FATE_HAS_GAME's handleMouseUp.
void Editor::finishTileMouseUp(World* world) {
    // Commit pending Paint-tool brush stroke
    if (pendingBrushStroke_ && !pendingBrushStroke_->empty()) {
        UndoSystem::instance().push(std::move(pendingBrushStroke_));
    }
    pendingBrushStroke_.reset();

    // Finalize RectFill / LineTool
    if (isToolDragging_ && (currentTool_ == EditorTool::RectFill ||
                            currentTool_ == EditorTool::LineTool)) {
        bool isCollisionLayer = (selectedTileLayer_ == "collision");
        bool canPaint = isCollisionLayer || (paletteTexture_ && selectedTileIndex_ >= 0);
        if (canPaint && world) {
            TileCoordList coords;
            std::string toolName;
            if (currentTool_ == EditorTool::RectFill) {
                coords = rectangleFill(toolDragStart_.x, toolDragStart_.y,
                                       toolDragEnd_.x, toolDragEnd_.y);
                toolName = "Rect Fill";
            } else {
                coords = lineTool(toolDragStart_.x, toolDragStart_.y,
                                  toolDragEnd_.x, toolDragEnd_.y);
                toolName = "Line";
            }
            if (!coords.empty()) {
                auto compound = std::make_unique<CompoundCommand>();
                compound->desc = toolName + " (" + std::to_string(coords.size()) + " tiles)";
                for (auto& coord : coords) {
                    Vec2 wp = tileToWorldCenter(coord.x, coord.y, gridSize_);
                    auto cmd = isCollisionLayer
                        ? paintOneCollisionTile(world, wp, gridSize_)
                        : paintOneTile(world, wp, selectedTileIndex_, paletteColumns_,
                                       paletteTileSize_, gridSize_, paletteTexture_, paletteTexturePath_,
                                       selectedTileLayer_);
                    if (cmd) compound->commands.push_back(std::move(cmd));
                }
                if (!compound->empty()) {
                    UndoSystem::instance().push(std::move(compound));
                    lastToolStatus_.clear();
                }
            }
        }
    }
    isToolDragging_ = false;
    toolDragStart_ = {-1, -1};
    toolDragEnd_ = {-1, -1};
}

} // namespace fate
