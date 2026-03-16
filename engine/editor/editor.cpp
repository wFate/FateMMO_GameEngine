#include "engine/editor/editor.h"
#include "engine/core/logger.h"
#include "engine/render/gl_loader.h"
#include "engine/input/input.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/player_controller.h"
#include "game/components/box_collider.h"
#include "game/components/polygon_collider.h"
#include "game/components/animator.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

namespace fate {

// ============================================================================
// Init / Shutdown
// ============================================================================

bool Editor::init(SDL_Window* window, SDL_GLContext glContext) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.FontGlobalScale = 1.2f;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.WindowBorderSize = 1.0f;

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330");

    scanAssets();

    LOG_INFO("Editor", "Editor initialized (F3 to toggle)");
    return true;
}

void Editor::shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void Editor::processEvent(const SDL_Event& event) {
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void Editor::beginFrame() {
    frameStarted_ = false;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    frameStarted_ = true;

    ImGuiIO& io = ImGui::GetIO();
    wantsKeyboard_ = io.WantCaptureKeyboard;
    wantsMouse_ = io.WantCaptureMouse;
}

// ============================================================================
// Render
// ============================================================================

void Editor::render(World* world, Camera* camera, SpriteBatch* batch) {
    if (!frameStarted_) return;

    // HUD always visible
    drawHUD(world);

    if (open_) {
        drawMenuBar(world);
        drawToolbar(world);
        drawHierarchy(world);
        drawInspector();
        drawAssetBrowser(world, camera);

        // Draw grid in scene when editor is open
        if (gridSnap_ && batch && camera) {
            drawSceneGrid(batch, camera);
        }

        if (showDemoWindow_) {
            ImGui::ShowDemoWindow(&showDemoWindow_);
        }
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ============================================================================
// Scene Interaction (called from App)
// ============================================================================

void Editor::handleSceneClick(World* world, Camera* camera, const Vec2& screenPos,
                              int windowWidth, int windowHeight) {
    if (!world || !camera || !open_) return;

    Vec2 worldPos = camera->screenToWorld(screenPos, windowWidth, windowHeight);

    // Asset placement mode: click to place
    if (isDraggingAsset_ && !draggedAssetPath_.empty()) {
        Vec2 placePos = worldPos;
        if (gridSnap_) {
            // Snap to tile center (half-grid offset): 16, 48, 80...
            float half = gridSize_ * 0.5f;
            placePos.x = std::floor(placePos.x / gridSize_) * gridSize_ + half;
            placePos.y = std::floor(placePos.y / gridSize_) * gridSize_ + half;
        }

        std::string name = fs::path(draggedAssetPath_).stem().string();
        Entity* entity = world->createEntity(name);

        auto* transform = entity->addComponent<Transform>(placePos);
        transform->depth = 1.0f;

        auto* sprite = entity->addComponent<SpriteComponent>();
        sprite->texturePath = draggedAssetPath_;
        sprite->texture = TextureCache::instance().load(draggedAssetPath_);
        if (sprite->texture) {
            sprite->size = {(float)sprite->texture->width(), (float)sprite->texture->height()};
        } else {
            sprite->size = {32.0f, 32.0f};
        }

        selectedEntity_ = entity;
        // Stay in placement mode so you can keep clicking to place more
        // (ESC or clicking another asset exits placement mode)

        LOG_INFO("Editor", "Placed '%s' at (%.0f, %.0f)", name.c_str(), placePos.x, placePos.y);
        return;
    }

    // Entity selection: highest depth first, then closest center to click
    Entity* best = nullptr;
    float bestDepth = -99999.0f;
    float bestDist = 99999.0f;

    world->forEach<Transform, SpriteComponent>(
        [&](Entity* entity, Transform* t, SpriteComponent* s) {
            float hw = s->size.x * t->scale.x * 0.5f;
            float hh = s->size.y * t->scale.y * 0.5f;
            Rect bounds = {t->position.x - hw, t->position.y - hh, hw * 2.0f, hh * 2.0f};

            if (bounds.contains(worldPos)) {
                float dist = worldPos.distance(t->position);
                // Prefer higher depth; at same depth, prefer closer center
                if (t->depth > bestDepth ||
                    (t->depth == bestDepth && dist < bestDist)) {
                    best = entity;
                    bestDepth = t->depth;
                    bestDist = dist;
                }
            }
        }
    );

    if (best) {
        selectedEntity_ = best;
        isDraggingEntity_ = true;
        auto* t = best->getComponent<Transform>();
        dragStartWorldPos_ = worldPos;
        dragStartEntityPos_ = t->position;
    } else {
        selectedEntity_ = nullptr;
        isDraggingEntity_ = false;
    }
}

void Editor::handleSceneDrag(Camera* camera, const Vec2& screenPos,
                             int windowWidth, int windowHeight) {
    if (!isDraggingEntity_ || !selectedEntity_ || !camera) return;

    auto* t = selectedEntity_->getComponent<Transform>();
    if (!t) return;

    // Convert current screen pos to world, compute offset from drag start
    Vec2 worldPos = camera->screenToWorld(screenPos, windowWidth, windowHeight);
    Vec2 newPos = dragStartEntityPos_ + (worldPos - dragStartWorldPos_);

    // Snap to tile center (half-grid offset)
    if (gridSnap_) {
        float half = gridSize_ * 0.5f;
        newPos.x = std::floor(newPos.x / gridSize_) * gridSize_ + half;
        newPos.y = std::floor(newPos.y / gridSize_) * gridSize_ + half;
    }

    t->position = newPos;
}

void Editor::handleMouseUp() {
    isDraggingEntity_ = false;
}

// ============================================================================
// Asset Browser
// ============================================================================

static AssetType classifyFile(const std::string& ext) {
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") return AssetType::Sprite;
    if (ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".c") return AssetType::Script;
    if (ext == ".json") return AssetType::Scene;
    if (ext == ".vert" || ext == ".frag" || ext == ".glsl") return AssetType::Shader;
    return AssetType::Other;
}

void Editor::scanAssets() {
    assets_.clear();

    // Scan all directories: assets/, engine/, game/
    std::vector<std::string> scanRoots = {assetRoot_, "engine", "game"};

    for (auto& root : scanRoots) {
        if (!fs::exists(root)) continue;
        for (auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;

            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            AssetType type = classifyFile(ext);
            if (type == AssetType::Other) continue; // skip unknown types

            AssetEntry ae;
            ae.name = entry.path().filename().string();
            ae.fullPath = entry.path().string();
            std::replace(ae.fullPath.begin(), ae.fullPath.end(), '\\', '/');
            ae.relativePath = ae.fullPath;
            ae.type = type;
            assets_.push_back(std::move(ae));
        }
    }

    // Sort: sprites first, then scripts, then scenes, then shaders
    std::sort(assets_.begin(), assets_.end(), [](const AssetEntry& a, const AssetEntry& b) {
        if (a.type != b.type) return (int)a.type < (int)b.type;
        return a.name < b.name;
    });

    LOG_INFO("Editor", "Found %zu project files", assets_.size());
}

void Editor::drawAssetBrowser(World* world, Camera* camera) {
    ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetIO().DisplaySize.y - 220), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 220), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Project")) {
        if (ImGui::Button("Refresh")) scanAssets();
        ImGui::SameLine();
        ImGui::Text("(%zu files)", assets_.size());

        if (isDraggingAsset_) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "| PLACING: %s (click scene | ESC cancel)",
                              fs::path(draggedAssetPath_).stem().string().c_str());
        }

        // Tabs for each asset type
        if (ImGui::BeginTabBar("AssetTabs")) {
            struct TabDef { const char* label; AssetType type; };
            TabDef tabs[] = {
                {"Sprites", AssetType::Sprite},
                {"Scripts", AssetType::Script},
                {"Scenes", AssetType::Scene},
                {"Shaders", AssetType::Shader}
            };

            for (auto& tab : tabs) {
                if (ImGui::BeginTabItem(tab.label)) {
                    float panelWidth = ImGui::GetContentRegionAvail().x;

                    if (tab.type == AssetType::Sprite) {
                        // Grid view with thumbnails for sprites
                        float itemSize = 80.0f;
                        int columns = (int)(panelWidth / (itemSize + 8.0f));
                        if (columns < 1) columns = 1;
                        int col = 0;

                        for (auto& asset : assets_) {
                            if (asset.type != AssetType::Sprite) continue;

                            ImGui::PushID(asset.fullPath.c_str());
                            ImGui::BeginGroup();

                            // Load thumbnail on demand
                            if (!asset.thumbnail) {
                                asset.thumbnail = TextureCache::instance().load(asset.fullPath);
                            }

                            if (asset.thumbnail) {
                                ImTextureID texId = (ImTextureID)(intptr_t)asset.thumbnail->id();
                                bool selected = (draggedAssetPath_ == asset.relativePath);
                                if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 1.0f, 0.7f));

                                char btnId[64];
                                snprintf(btnId, sizeof(btnId), "##asset_%s", asset.name.c_str());
                                if (ImGui::ImageButton(btnId, texId, ImVec2(itemSize - 16, itemSize - 16),
                                                       ImVec2(0, 1), ImVec2(1, 0))) {
                                    isDraggingAsset_ = true;
                                    draggedAssetPath_ = asset.relativePath;
                                }
                                if (selected) ImGui::PopStyleColor();
                            } else {
                                if (ImGui::Button(asset.name.c_str(), ImVec2(itemSize - 16, itemSize - 16))) {
                                    isDraggingAsset_ = true;
                                    draggedAssetPath_ = asset.relativePath;
                                }
                            }

                            std::string dn = asset.name;
                            if (dn.size() > 11) dn = dn.substr(0, 9) + "..";
                            ImGui::TextWrapped("%s", dn.c_str());
                            ImGui::EndGroup();

                            col++;
                            if (col < columns) ImGui::SameLine();
                            else col = 0;

                            ImGui::PopID();
                        }
                    } else {
                        // List view for scripts/scenes/shaders
                        for (auto& asset : assets_) {
                            if (asset.type != tab.type) continue;

                            ImGui::PushID(asset.fullPath.c_str());

                            // Color-code by type
                            ImVec4 color = {0.8f, 0.8f, 0.8f, 1.0f};
                            if (tab.type == AssetType::Script) color = {0.4f, 0.8f, 1.0f, 1.0f};
                            else if (tab.type == AssetType::Scene) color = {0.4f, 1.0f, 0.6f, 1.0f};
                            else if (tab.type == AssetType::Shader) color = {1.0f, 0.7f, 0.4f, 1.0f};

                            ImGui::PushStyleColor(ImGuiCol_Text, color);
                            if (ImGui::Selectable(asset.name.c_str())) {
                                LOG_INFO("Editor", "File: %s", asset.fullPath.c_str());
                            }
                            ImGui::PopStyleColor();

                            // Tooltip with full path
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", asset.fullPath.c_str());
                            }

                            ImGui::PopID();
                        }
                    }

                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

// ============================================================================
// Scene Grid Overlay
// ============================================================================

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

    // Selection highlight
    if (selectedEntity_) {
        auto* t = selectedEntity_->getComponent<Transform>();
        auto* s = selectedEntity_->getComponent<SpriteComponent>();
        if (t && s) {
            float hw = s->size.x * 0.5f + 2.0f;
            float hh = s->size.y * 0.5f + 2.0f;
            Color sel(0.2f, 0.6f, 1.0f, 0.6f);
            batch->drawRect({t->position.x, t->position.y + hh}, {hw * 2, 2.0f}, sel, 99.0f);
            batch->drawRect({t->position.x, t->position.y - hh}, {hw * 2, 2.0f}, sel, 99.0f);
            batch->drawRect({t->position.x - hw, t->position.y}, {2.0f, hh * 2}, sel, 99.0f);
            batch->drawRect({t->position.x + hw, t->position.y}, {2.0f, hh * 2}, sel, 99.0f);
        }
    }

    batch->end();
}

// ============================================================================
// Scene Save / Load
// ============================================================================

void Editor::saveScene(World* world, const std::string& path) {
    if (!world) return;

    nlohmann::json root;
    root["version"] = 1;
    root["gridSize"] = gridSize_;

    nlohmann::json entitiesJson = nlohmann::json::array();

    world->forEachEntity([&](Entity* entity) {
        nlohmann::json ej;
        ej["name"] = entity->name();
        ej["tag"] = entity->tag();
        ej["active"] = entity->isActive();

        nlohmann::json comps;

        if (auto* t = entity->getComponent<Transform>()) {
            comps["Transform"] = {
                {"position", {t->position.x, t->position.y}},
                {"scale", {t->scale.x, t->scale.y}},
                {"rotation", t->rotation},
                {"depth", t->depth}
            };
        }

        if (auto* s = entity->getComponent<SpriteComponent>()) {
            comps["Sprite"] = {
                {"texture", s->texturePath},
                {"size", {s->size.x, s->size.y}},
                {"tint", {s->tint.r, s->tint.g, s->tint.b, s->tint.a}},
                {"flipX", s->flipX},
                {"flipY", s->flipY}
            };
        }

        if (auto* c = entity->getComponent<BoxCollider>()) {
            comps["BoxCollider"] = {
                {"size", {c->size.x, c->size.y}},
                {"offset", {c->offset.x, c->offset.y}},
                {"isTrigger", c->isTrigger},
                {"isStatic", c->isStatic}
            };
        }

        if (auto* pc = entity->getComponent<PolygonCollider>()) {
            nlohmann::json pts = nlohmann::json::array();
            for (auto& p : pc->points) {
                pts.push_back({p.x, p.y});
            }
            comps["PolygonCollider"] = {
                {"points", pts},
                {"isTrigger", pc->isTrigger},
                {"isStatic", pc->isStatic}
            };
        }

        if (auto* p = entity->getComponent<PlayerController>()) {
            comps["PlayerController"] = {
                {"moveSpeed", p->moveSpeed}
            };
        }

        ej["components"] = comps;
        entitiesJson.push_back(ej);
    });

    root["entities"] = entitiesJson;

    std::ofstream file(path);
    file << root.dump(2);
    file.close();

    LOG_INFO("Editor", "Scene saved to %s (%zu entities)", path.c_str(), world->entityCount());
}

void Editor::loadScene(World* world, const std::string& path) {
    if (!world) return;

    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Editor", "Cannot open scene: %s", path.c_str());
        return;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(file);
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("Editor", "Scene parse error: %s", e.what());
        return;
    }

    // Clear existing entities
    world->forEachEntity([&](Entity* entity) {
        world->destroyEntity(entity->id());
    });
    world->processDestroyQueue();

    if (root.contains("gridSize")) {
        gridSize_ = root["gridSize"].get<float>();
    }

    if (!root.contains("entities")) return;

    for (auto& ej : root["entities"]) {
        std::string name = ej.value("name", "Entity");
        Entity* entity = world->createEntity(name);
        entity->setTag(ej.value("tag", ""));
        entity->setActive(ej.value("active", true));

        auto& comps = ej["components"];

        if (comps.contains("Transform")) {
            auto& tj = comps["Transform"];
            auto* t = entity->addComponent<Transform>();
            auto pos = tj["position"];
            t->position = {pos[0].get<float>(), pos[1].get<float>()};
            if (tj.contains("scale")) {
                auto sc = tj["scale"];
                t->scale = {sc[0].get<float>(), sc[1].get<float>()};
            }
            t->rotation = tj.value("rotation", 0.0f);
            t->depth = tj.value("depth", 0.0f);
        }

        if (comps.contains("Sprite")) {
            auto& sj = comps["Sprite"];
            auto* s = entity->addComponent<SpriteComponent>();
            s->texturePath = sj.value("texture", "");
            if (!s->texturePath.empty()) {
                s->texture = TextureCache::instance().load(s->texturePath);
            }
            if (sj.contains("size")) {
                auto sz = sj["size"];
                s->size = {sz[0].get<float>(), sz[1].get<float>()};
            }
            if (sj.contains("tint")) {
                auto tn = sj["tint"];
                s->tint = {tn[0].get<float>(), tn[1].get<float>(),
                           tn[2].get<float>(), tn[3].get<float>()};
            }
            s->flipX = sj.value("flipX", false);
            s->flipY = sj.value("flipY", false);
        }

        if (comps.contains("BoxCollider")) {
            auto& cj = comps["BoxCollider"];
            auto* c = entity->addComponent<BoxCollider>();
            auto sz = cj["size"];
            c->size = {sz[0].get<float>(), sz[1].get<float>()};
            auto off = cj["offset"];
            c->offset = {off[0].get<float>(), off[1].get<float>()};
            c->isTrigger = cj.value("isTrigger", false);
            c->isStatic = cj.value("isStatic", true);
        }

        if (comps.contains("PolygonCollider")) {
            auto& pj = comps["PolygonCollider"];
            auto* pc = entity->addComponent<PolygonCollider>();
            if (pj.contains("points")) {
                for (auto& pt : pj["points"]) {
                    pc->points.push_back({pt[0].get<float>(), pt[1].get<float>()});
                }
            }
            pc->isTrigger = pj.value("isTrigger", false);
            pc->isStatic = pj.value("isStatic", true);
        }

        if (comps.contains("PlayerController")) {
            auto& pj = comps["PlayerController"];
            auto* p = entity->addComponent<PlayerController>();
            p->moveSpeed = pj.value("moveSpeed", 96.0f);
        }
    }

    selectedEntity_ = nullptr;
    LOG_INFO("Editor", "Scene loaded from %s (%zu entities)", path.c_str(), world->entityCount());
}

// ============================================================================
// HUD (always visible)
// ============================================================================

void Editor::drawHUD(World* world) {
    if (!world) return;

    Entity* player = world->findByTag("player");
    if (!player) return;

    auto* t = player->getComponent<Transform>();
    if (!t) return;

    char buf[64];
    snprintf(buf, sizeof(buf), "(%d, %d)", Coords::tileX(t->position.x), Coords::tileY(t->position.y));

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 textSize = ImGui::CalcTextSize(buf);
    ImVec2 padding(12.0f, 6.0f);
    float winWidth = textSize.x + padding.x * 2.0f;
    float x = (io.DisplaySize.x - winWidth) * 0.5f;
    float y = open_ ? 60.0f : 6.0f;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::Begin("##HUD_Pos", nullptr, flags);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.9f), "%s", buf);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ============================================================================
// Menu Bar
// ============================================================================

void Editor::drawMenuBar(World* world) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                saveScene(world, "assets/scenes/scene.json");
            }
            if (ImGui::MenuItem("Load Scene", "Ctrl+L")) {
                loadScene(world, "assets/scenes/scene.json");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close Editor", "F3")) {
                open_ = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Grid Snap", nullptr, &gridSnap_);
            ImGui::DragFloat("Grid Size", &gridSize_, 1.0f, 8.0f, 128.0f);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &showDemoWindow_);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Entity")) {
            if (ImGui::MenuItem("Create Empty")) {
                if (world) {
                    auto* e = world->createEntity("New Entity");
                    e->addComponent<Transform>();
                    selectedEntity_ = e;
                }
            }
            if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D", false, selectedEntity_ != nullptr)) {
                // TODO: deep copy
                LOG_INFO("Editor", "Duplicate TODO");
            }
            if (ImGui::MenuItem("Delete Selected", "Delete", false, selectedEntity_ != nullptr)) {
                if (world && selectedEntity_) {
                    world->destroyEntity(selectedEntity_->id());
                    selectedEntity_ = nullptr;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

// ============================================================================
// Toolbar
// ============================================================================

void Editor::drawToolbar(World* world) {
    ImGui::SetNextWindowPos(ImVec2(0, 20), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 36), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("##Toolbar", nullptr, flags)) {
        if (paused_) {
            if (ImGui::Button(" PLAY  ")) { paused_ = false; }
        } else {
            if (ImGui::Button(" PAUSE ")) { paused_ = true; }
        }

        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();

        if (world) ImGui::Text("Entities: %zu", world->entityCount());

        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();

        ImGui::Checkbox("Grid", &gridSnap_);

        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();

        if (isDraggingAsset_) {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "PLACING MODE (click scene | ESC cancel)");
        } else {
            ImGui::Text(paused_ ? "PAUSED" : "RUNNING");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ============================================================================
// Hierarchy
// ============================================================================

void Editor::drawHierarchy(World* world) {
    ImGui::SetNextWindowPos(ImVec2(0, 56), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Hierarchy")) {
        if (!world) {
            ImGui::Text("No active scene");
            ImGui::End();
            return;
        }

        static char searchBuf[128] = "";
        ImGui::InputText("Filter", searchBuf, sizeof(searchBuf));
        ImGui::Separator();

        std::string filter(searchBuf);

        world->forEachEntity([&](Entity* entity) {
            if (!entity) return;

            if (!filter.empty()) {
                std::string name = entity->name();
                std::string tag = entity->tag();
                std::string combined = name + " " + tag;
                std::string filterLower = filter;
                std::transform(combined.begin(), combined.end(), combined.begin(), ::tolower);
                std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
                if (combined.find(filterLower) == std::string::npos) return;
            }

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (entity == selectedEntity_) flags |= ImGuiTreeNodeFlags_Selected;

            bool hasTag = !entity->tag().empty();
            if (hasTag) {
                if (entity->tag() == "player")
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
                else if (entity->tag() == "obstacle")
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.3f, 1.0f));
                else if (entity->tag() == "ground")
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 0.5f, 1.0f));
                else
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            }

            char label[256];
            if (hasTag)
                snprintf(label, sizeof(label), "%s [%u] (%s)", entity->name().c_str(), entity->id(), entity->tag().c_str());
            else
                snprintf(label, sizeof(label), "%s [%u]", entity->name().c_str(), entity->id());

            ImGui::TreeNodeEx((void*)(intptr_t)entity->id(), flags, "%s", label);

            if (ImGui::IsItemClicked()) {
                selectedEntity_ = entity;
            }

            if (hasTag) ImGui::PopStyleColor();
        });
    }
    ImGui::End();
}

// ============================================================================
// Inspector
// ============================================================================

void Editor::drawInspector() {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 320, 56), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Inspector")) {
        if (!selectedEntity_) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select an entity");
            ImGui::End();
            return;
        }

        char nameBuf[128];
        strncpy(nameBuf, selectedEntity_->name().c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
            selectedEntity_->setName(nameBuf);
        }

        char tagBuf[64];
        strncpy(tagBuf, selectedEntity_->tag().c_str(), sizeof(tagBuf) - 1);
        tagBuf[sizeof(tagBuf) - 1] = '\0';
        if (ImGui::InputText("Tag", tagBuf, sizeof(tagBuf))) {
            selectedEntity_->setTag(tagBuf);
        }

        bool active = selectedEntity_->isActive();
        if (ImGui::Checkbox("Active", &active)) {
            selectedEntity_->setActive(active);
        }

        ImGui::Separator();

        // Transform
        if (auto* t = selectedEntity_->getComponent<Transform>()) {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat2("Position (px)", &t->position.x, 1.0f);
                Vec2 tile = Coords::toTile(t->position);
                ImGui::Text("Tile: (%d, %d)", (int)tile.x, (int)tile.y);
                ImGui::DragFloat2("Scale", &t->scale.x, 0.01f, 0.01f, 10.0f);
                float degrees = t->rotation * 57.2957795f;
                if (ImGui::DragFloat("Rotation", &degrees, 1.0f, -360.0f, 360.0f)) {
                    t->rotation = degrees * 0.0174532925f;
                }
                ImGui::DragFloat("Depth", &t->depth, 0.1f);
            }
        }

        // Sprite
        if (auto* s = selectedEntity_->getComponent<SpriteComponent>()) {
            if (ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Texture: %s", s->texturePath.empty() ? "(procedural)" : s->texturePath.c_str());
                if (s->texture) {
                    ImGui::Text("Tex Size: %dx%d", s->texture->width(), s->texture->height());
                    // Show thumbnail
                    ImTextureID texId = (ImTextureID)(intptr_t)s->texture->id();
                    ImGui::Image(texId, ImVec2(64, 64), ImVec2(0, 1), ImVec2(1, 0));
                }
                ImGui::DragFloat2("Render Size", &s->size.x, 1.0f, 1.0f, 512.0f);
                ImGui::ColorEdit4("Tint", &s->tint.r);
                ImGui::Checkbox("Flip X", &s->flipX);
                ImGui::SameLine();
                ImGui::Checkbox("Flip Y", &s->flipY);
            }
        }

        // BoxCollider
        if (auto* c = selectedEntity_->getComponent<BoxCollider>()) {
            if (ImGui::CollapsingHeader("Box Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat2("Size", &c->size.x, 1.0f, 1.0f, 256.0f);
                ImGui::DragFloat2("Offset", &c->offset.x, 1.0f);
                ImGui::Checkbox("Is Trigger", &c->isTrigger);
                ImGui::Checkbox("Is Static", &c->isStatic);
            }
        }

        // PolygonCollider
        if (auto* pc = selectedEntity_->getComponent<PolygonCollider>()) {
            if (ImGui::CollapsingHeader("Polygon Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Is Trigger##poly", &pc->isTrigger);
                ImGui::Checkbox("Is Static##poly", &pc->isStatic);
                ImGui::Text("Vertices: %zu", pc->points.size());

                int removeIdx = -1;
                for (int i = 0; i < (int)pc->points.size(); i++) {
                    ImGui::PushID(i);
                    char label[16];
                    snprintf(label, sizeof(label), "V%d", i);
                    ImGui::DragFloat2(label, &pc->points[i].x, 1.0f);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) removeIdx = i;
                    ImGui::PopID();
                }
                if (removeIdx >= 0) {
                    pc->points.erase(pc->points.begin() + removeIdx);
                }

                if (ImGui::Button("+ Vertex")) {
                    if (pc->points.empty()) pc->points.push_back({-16, -16});
                    else { Vec2 last = pc->points.back(); pc->points.push_back({last.x + 16, last.y}); }
                }
                ImGui::SameLine();
                if (ImGui::Button("Box")) { *pc = PolygonCollider::makeBox(32, 32); }
                ImGui::SameLine();
                if (ImGui::Button("Circle")) { *pc = PolygonCollider::makeCircleApprox(16, 8); }
            }
        }

        // PlayerController
        if (auto* p = selectedEntity_->getComponent<PlayerController>()) {
            if (ImGui::CollapsingHeader("Player Controller", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat("Move Speed", &p->moveSpeed, 1.0f, 0.0f, 500.0f);
                const char* dirs[] = {"None", "Up", "Down", "Left", "Right"};
                ImGui::Text("Facing: %s | Moving: %s", dirs[(int)p->facing], p->isMoving ? "Yes" : "No");
            }
        }

        // Animator
        if (auto* a = selectedEntity_->getComponent<Animator>()) {
            if (ImGui::CollapsingHeader("Animator")) {
                ImGui::Text("Current: %s", a->currentAnimation.c_str());
                ImGui::Text("Playing: %s | Timer: %.2f", a->playing ? "Yes" : "No", a->timer);
            }
        }

        // Add Component
        ImGui::Separator();
        if (ImGui::Button("+ Add Component")) ImGui::OpenPopup("AddComponent");
        if (ImGui::BeginPopup("AddComponent")) {
            if (!selectedEntity_->hasComponent<Transform>() && ImGui::MenuItem("Transform"))
                selectedEntity_->addComponent<Transform>();
            if (!selectedEntity_->hasComponent<SpriteComponent>() && ImGui::MenuItem("Sprite"))
                selectedEntity_->addComponent<SpriteComponent>();
            if (!selectedEntity_->hasComponent<BoxCollider>() && ImGui::MenuItem("Box Collider"))
                selectedEntity_->addComponent<BoxCollider>();
            if (!selectedEntity_->hasComponent<PolygonCollider>() && ImGui::MenuItem("Polygon Collider")) {
                auto* pc = selectedEntity_->addComponent<PolygonCollider>();
                *pc = PolygonCollider::makeBox(32, 32);
            }
            if (!selectedEntity_->hasComponent<PlayerController>() && ImGui::MenuItem("Player Controller"))
                selectedEntity_->addComponent<PlayerController>();
            if (!selectedEntity_->hasComponent<Animator>() && ImGui::MenuItem("Animator"))
                selectedEntity_->addComponent<Animator>();
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

} // namespace fate
