#include "engine/editor/editor.h"
#include "engine/core/logger.h"
#include "engine/render/gl_loader.h"
#include "engine/input/input.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/player_controller.h"
#include "game/components/box_collider.h"
#include "game/components/polygon_collider.h"
#include "game/components/animator.h"
#include "game/components/zone_component.h"
#include "game/components/game_components.h"
#include "game/systems/spawn_system.h"
#include "engine/ecs/prefab.h"
#include "engine/scene/scene_manager.h"
#include "engine/editor/undo.h"
#include "engine/editor/log_viewer.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
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
    io.FontGlobalScale = 1.0f;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    // Sharp, tight, professional
    style.WindowRounding = 0.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 0.0f;
    style.ScrollbarRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2(6.0f, 6.0f);
    style.FramePadding = ImVec2(4.0f, 3.0f);
    style.CellPadding = ImVec2(4.0f, 2.0f);
    style.ItemSpacing = ImVec2(6.0f, 3.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 3.0f);
    style.IndentSpacing = 14.0f;
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 6.0f;
    style.TabBorderSize = 1.0f;
    style.DockingSeparatorSize = 2.0f;

    // Color scheme — dark charcoal with subtle blue accents
    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]             = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.10f, 0.10f, 0.12f, 0.96f);
    c[ImGuiCol_Border]               = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.16f, 0.16f, 0.19f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.28f, 0.28f, 0.33f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.09f, 0.09f, 0.11f, 0.80f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.42f, 0.42f, 0.48f, 1.00f);
    c[ImGuiCol_CheckMark]            = ImVec4(0.40f, 0.65f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.40f, 0.65f, 1.00f, 0.80f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.50f, 0.72f, 1.00f, 1.00f);
    c[ImGuiCol_Button]               = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.28f, 0.28f, 0.34f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.35f, 0.35f, 0.42f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.28f, 0.32f, 0.42f, 1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.30f, 0.38f, 0.52f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.40f, 0.55f, 0.80f, 0.80f);
    c[ImGuiCol_SeparatorActive]      = ImVec4(0.40f, 0.55f, 0.80f, 1.00f);
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.30f, 0.30f, 0.35f, 0.40f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.40f, 0.55f, 0.80f, 0.60f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0.40f, 0.55f, 0.80f, 0.90f);
    c[ImGuiCol_Tab]                  = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.28f, 0.32f, 0.42f, 1.00f);
    c[ImGuiCol_TabSelected]          = ImVec4(0.20f, 0.24f, 0.33f, 1.00f);
    c[ImGuiCol_TabDimmed]            = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.16f, 0.18f, 0.24f, 1.00f);
    c[ImGuiCol_DockingPreview]       = ImVec4(0.40f, 0.55f, 0.80f, 0.40f);
    c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    c[ImGuiCol_TextSelectedBg]       = ImVec4(0.30f, 0.45f, 0.70f, 0.40f);
    c[ImGuiCol_NavHighlight]         = ImVec4(0.40f, 0.55f, 0.80f, 1.00f);

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330");

    SDL_SetWindowTitle(window, "FateMMO Engine | Editor");

    scanAssets();

    LOG_INFO("Editor", "Editor initialized");
    return true;
}

void Editor::shutdown() {
    viewportFbo_.destroy();
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

void Editor::renderScene(SpriteBatch* batch, Camera* camera) {
    // Called while FBO is bound — draw in-viewport overlays via SpriteBatch
    if (!open_ || !batch || !camera) return;

    if (showGrid_ && paused_) {
        drawSceneGrid(batch, camera);
    }
}

void Editor::renderUI(World* world, Camera* camera, SpriteBatch* batch) {
    if (!frameStarted_) return;

    dockWorld_ = world;
    dockCamera_ = camera;
    drawDockSpace();
    drawMenuBar(world);
    drawSceneViewport();
    drawViewportHUD(world);
    drawHierarchy(world);
    drawInspector();
    drawConsole(world);
    LogViewer::instance().draw();
    drawTilePalette(world, camera);
    drawAssetBrowser(world, camera);
    drawDebugInfoPanel(world);

    if (showDemoWindow_) {
        ImGui::ShowDemoWindow(&showDemoWindow_);
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Editor::drawDockSpace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                  ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("##DockSpaceHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    // ---- Main menu bar (File / Edit / View / Entity) ----
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene")) {
                if (dockWorld_) {
                    dockWorld_->forEachEntity([&](Entity* e) {
                        dockWorld_->destroyEntity(e->handle());
                    });
                    dockWorld_->processDestroyQueue();
                    selectedEntity_ = nullptr;
                    currentScenePath_.clear();
                    LOG_INFO("Editor", "New scene");
                }
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Open Scene")) {
                std::string scenesDir = "assets/scenes";
                if (fs::exists(scenesDir)) {
                    for (auto& entry : fs::directory_iterator(scenesDir)) {
                        if (!entry.is_regular_file()) continue;
                        if (entry.path().extension() != ".json") continue;
                        std::string name = entry.path().stem().string();
                        if (ImGui::MenuItem(name.c_str())) {
                            loadScene(dockWorld_, entry.path().string());
                        }
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            // Save — overwrites current scene file (grayed out if no scene loaded)
            if (ImGui::MenuItem("Save", "Ctrl+S", false, !currentScenePath_.empty())) {
                saveScene(dockWorld_, currentScenePath_);
            }
            // Save As — always prompts for a new name
            if (ImGui::BeginMenu("Save As...")) {
                static char saveNameBuf[64] = "scene";
                ImGui::InputText("Name", saveNameBuf, sizeof(saveNameBuf));
                if (ImGui::Button("Save")) {
                    std::string path = std::string("assets/scenes/") + saveNameBuf + ".json";
                    saveScene(dockWorld_, path);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            bool canUndo = UndoSystem::instance().canUndo();
            bool canRedo = UndoSystem::instance().canRedo();
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo)) UndoSystem::instance().undo(dockWorld_);
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo)) UndoSystem::instance().redo(dockWorld_);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Grid Snap", nullptr, &gridSnap_);
            ImGui::DragFloat("Grid Size", &gridSize_, 1.0f, 8.0f, 128.0f);
            ImGui::Separator();
            ImGui::MenuItem("Show Grid", nullptr, &showGrid_);
            ImGui::MenuItem("Show Colliders", nullptr, &showCollisionDebug_);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) { resetLayout_ = true; }
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &showDemoWindow_);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Entity")) {
            if (ImGui::MenuItem("Create Empty")) {
                if (dockWorld_) {
                    auto* e = dockWorld_->createEntity("New Entity");
                    e->addComponent<Transform>();
                    selectedEntity_ = e;
                }
            }
            if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D", false, selectedEntity_ != nullptr)) {
                if (dockWorld_ && selectedEntity_) {
                    auto json = PrefabLibrary::entityToJson(selectedEntity_);
                    Entity* copy = PrefabLibrary::jsonToEntity(json, *dockWorld_);
                    auto* t = copy->getComponent<Transform>();
                    if (t) t->position += Vec2(32.0f, 0.0f);
                    selectedEntity_ = copy;
                }
            }
            if (ImGui::MenuItem("Save as Prefab", nullptr, false, selectedEntity_ != nullptr)) {
                openSavePrefab_ = true;
            }
            if (ImGui::MenuItem("Delete Selected", "Delete", false, selectedEntity_ != nullptr)) {
                if (dockWorld_ && selectedEntity_) {
                    dockWorld_->destroyEntity(selectedEntity_->handle());
                    selectedEntity_ = nullptr;
                }
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");

    if (resetLayout_ || ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        resetLayout_ = false;
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID dockMain = dockspaceId;
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.15f, nullptr, &dockMain);
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.22f, nullptr, &dockMain);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.22f, nullptr, &dockMain);

        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Scene", dockMain);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);
        ImGui::DockBuilderDockWindow("Project", dockBottom);
        ImGui::DockBuilderDockWindow("Console", dockBottom);
        ImGui::DockBuilderDockWindow("Log", dockBottom);
        ImGui::DockBuilderDockWindow("Debug Info", dockBottom);
        ImGui::DockBuilderDockWindow("Tile Palette", dockRight);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();
}

void Editor::drawSceneViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("Scene")) {
        // ---- Viewport toolbar bar (Unity-style compact) ----
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 3.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.12f, 1.00f));

            float toolbarHeight = ImGui::GetFrameHeight() + 6.0f;
            ImGui::BeginChild("##ViewportToolbar", ImVec2(0, toolbarHeight), false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            float btnH = ImGui::GetFrameHeight();
            float btnSq = btnH; // square buttons

            // Left side: tool buttons
            auto toolBtn = [&](const char* label, EditorTool tool) {
                bool active = (currentTool_ == tool);
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.80f, 1.00f));
                if (ImGui::Button(label, ImVec2(0, btnH))) currentTool_ = tool;
                if (active) ImGui::PopStyleColor();
                ImGui::SameLine();
            };
            toolBtn("Move", EditorTool::Move);
            toolBtn("Resize", EditorTool::Resize);
            toolBtn("Paint", EditorTool::Paint);
            toolBtn("Erase", EditorTool::Erase);

            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            // Toggle buttons
            auto toggleBtn = [&](const char* label, bool* val) {
                if (*val) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.80f, 1.00f));
                if (ImGui::Button(label, ImVec2(0, btnH))) *val = !(*val);
                if (*val) ImGui::PopStyleColor();
                ImGui::SameLine();
            };
            toggleBtn("Grid", &showGrid_);
            toggleBtn("Snap", &gridSnap_);
            toggleBtn("Colliders", &showCollisionDebug_);

            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            // Play/Pause
            {
                float playBtnW = 50.0f;
                if (paused_) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.50f, 0.20f, 1.00f));
                    if (ImGui::Button("|>", ImVec2(playBtnW, btnH))) {
                        // Save editor camera state, reset to default zoom for play
                        if (dockCamera_) {
                            savedCamPos_ = dockCamera_->position();
                            savedCamZoom_ = dockCamera_->zoom();
                            dockCamera_->setZoom(1.0f);
                        }
                        paused_ = false;
                        clearSelection();
                    }
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.20f, 0.20f, 1.00f));
                    if (ImGui::Button("||", ImVec2(playBtnW, btnH))) {
                        // Restore editor camera state
                        if (dockCamera_) {
                            dockCamera_->setPosition(savedCamPos_);
                            dockCamera_->setZoom(savedCamZoom_);
                        }
                        paused_ = true;
                    }
                    ImGui::PopStyleColor();
                }
            }

            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            // Display resolution dropdown
            {
                const auto& preset = kDisplayPresets[displayPresetIdx_];
                char label[64];
                if (preset.width == 0)
                    snprintf(label, sizeof(label), "%s", preset.name);
                else
                    snprintf(label, sizeof(label), "%s (%dx%d)", preset.name, preset.width, preset.height);

                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::BeginCombo("##Display", label, ImGuiComboFlags_HeightLarge)) {
                    for (int i = 0; i < kDisplayPresetCount; i++) {
                        char itemLabel[64];
                        if (kDisplayPresets[i].width == 0)
                            snprintf(itemLabel, sizeof(itemLabel), "%s", kDisplayPresets[i].name);
                        else
                            snprintf(itemLabel, sizeof(itemLabel), "%s  %dx%d", kDisplayPresets[i].name,
                                     kDisplayPresets[i].width, kDisplayPresets[i].height);

                        if (ImGui::Selectable(itemLabel, i == displayPresetIdx_))
                            displayPresetIdx_ = i;
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::SameLine();

            // Right-aligned: FPS stats in muted gray
            {
                ImGuiIO& io = ImGui::GetIO();
                char stats[64];
                snprintf(stats, sizeof(stats), "%.0f FPS | %zu ent",
                         io.Framerate, dockWorld_ ? dockWorld_->entityCount() : 0u);
                float textW = ImGui::CalcTextSize(stats).x;
                float regionW = ImGui::GetContentRegionAvail().x;
                if (regionW > textW + 4.0f) {
                    ImGui::SameLine(ImGui::GetCursorPosX() + regionW - textW);
                    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.50f, 1.0f), "%s", stats);
                }
            }

            ImGui::EndChild();

            // Subtle bottom border line
            {
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                p0.y -= 1.0f;
                ImVec2 p1 = ImVec2(p0.x + ImGui::GetContentRegionAvail().x, p0.y);
                ImGui::GetWindowDrawList()->AddLine(p0, p1, IM_COL32(60, 60, 68, 255));
            }

            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }

        // ---- FBO viewport image fills the rest ----
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
        viewportHovered_ = ImGui::IsWindowHovered();

        int panelW = (int)avail.x;
        int panelH = (int)avail.y;

        const auto& preset = kDisplayPresets[displayPresetIdx_];
        bool useDeviceRes = !paused_ && preset.width > 0 && preset.height > 0;

        int fbW, fbH;
        if (useDeviceRes) {
            // Play mode with device preset — FBO at device resolution
            fbW = preset.width;
            fbH = preset.height;
        } else {
            // Edit mode or Free Aspect — FBO fills the panel
            fbW = panelW;
            fbH = panelH;
        }

        if (fbW > 0 && fbH > 0 && panelW > 0 && panelH > 0) {
            viewportFbo_.resize(fbW, fbH);

            if (viewportFbo_.isValid()) {
                if (useDeviceRes) {
                    // Letterbox/pillarbox: fit device resolution into panel with black bars
                    float scaleX = (float)panelW / (float)fbW;
                    float scaleY = (float)panelH / (float)fbH;
                    float scale = (scaleX < scaleY) ? scaleX : scaleY;
                    float dispW = fbW * scale;
                    float dispH = fbH * scale;
                    float offsetX = (avail.x - dispW) * 0.5f;
                    float offsetY = (avail.y - dispH) * 0.5f;

                    // Black background for letterbox bars
                    ImVec2 bgMin = cursorScreen;
                    ImVec2 bgMax = ImVec2(cursorScreen.x + avail.x, cursorScreen.y + avail.y);
                    ImGui::GetWindowDrawList()->AddRectFilled(bgMin, bgMax, IM_COL32(0, 0, 0, 255));

                    // Position the image centered
                    if (offsetX > 0 || offsetY > 0) {
                        ImGui::SetCursorPos(ImVec2(
                            ImGui::GetCursorPos().x + offsetX,
                            ImGui::GetCursorPos().y + offsetY));
                    }

                    // Track viewport to the actual displayed image area
                    ImVec2 imgPos = ImGui::GetCursorScreenPos();
                    viewportPos_ = {imgPos.x, imgPos.y};
                    viewportSize_ = {dispW, dispH};

                    ImGui::Image(
                        (ImTextureID)(intptr_t)viewportFbo_.textureId(),
                        ImVec2(dispW, dispH),
                        ImVec2(0, 1), ImVec2(1, 0)
                    );
                } else {
                    // Free aspect — image fills the panel
                    viewportPos_ = {cursorScreen.x, cursorScreen.y};
                    viewportSize_ = {avail.x, avail.y};

                    ImGui::Image(
                        (ImTextureID)(intptr_t)viewportFbo_.textureId(),
                        avail,
                        ImVec2(0, 1), ImVec2(1, 0)
                    );
                }
            }
        } else {
            viewportSize_ = {0, 0};
        }
    } else {
        viewportSize_ = {0, 0};
        viewportHovered_ = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Editor::drawViewportHUD(World* world) {
    if (!world || viewportSize_.x <= 0 || viewportSize_.y <= 0) return;

    Entity* player = world->findByTag("player");
    if (!player) return;

    auto* t = player->getComponent<Transform>();
    if (!t) return;

    char buf[64];
    snprintf(buf, sizeof(buf), "(%d, %d)", Coords::tileX(t->position.x), Coords::tileY(t->position.y));

    ImVec2 textSize = ImGui::CalcTextSize(buf);
    ImVec2 padding(12.0f, 6.0f);
    float winWidth = textSize.x + padding.x * 2.0f;
    float x = viewportPos_.x + (viewportSize_.x - winWidth) * 0.5f;
    float y = viewportPos_.y + 6.0f;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::Begin("##HUD_Viewport", nullptr, flags);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.9f), "%s", buf);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void Editor::drawDebugInfoPanel(World* world) {
    if (ImGui::Begin("Debug Info")) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("FPS: %.1f (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
        ImGui::Separator();

        if (world) {
            ImGui::Text("Entities: %zu", world->entityCount());
        }

        ImGui::Separator();
        ImGui::Text("Viewport: %dx%d", (int)viewportSize_.x, (int)viewportSize_.y);
        ImGui::Text("FBO: %dx%d", viewportFbo_.width(), viewportFbo_.height());

        ImGui::Separator();
        ImGui::Text("Paused: %s", paused_ ? "Yes" : "No");
        ImGui::Text("Tool: %s", currentTool_ == EditorTool::Move ? "Move" :
                                 currentTool_ == EditorTool::Resize ? "Resize" :
                                 currentTool_ == EditorTool::Paint ? "Paint" : "Erase");
    }
    ImGui::End();
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

        Entity* entity = nullptr;

        // Check if placing a prefab or a sprite
        if (draggedAssetPath_.substr(0, 7) == "prefab:") {
            std::string prefabName = draggedAssetPath_.substr(7);
            entity = PrefabLibrary::instance().spawn(prefabName, *world, placePos);
        } else {
            // Placing a sprite asset
            std::string name = fs::path(draggedAssetPath_).stem().string();
            entity = world->createEntity(name);

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
        }

        if (entity) {
            selectedEntity_ = entity;
            LOG_INFO("Editor", "Placed at (%.0f, %.0f)", placePos.x, placePos.y);
        }
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

    // If we already have a selection, check resize handles and keep selection
    // BEFORE looking at other entities (prevents accidental deselection)
    if (selectedEntity_) {
        auto* t = selectedEntity_->getComponent<Transform>();
        auto* s = selectedEntity_->getComponent<SpriteComponent>();

        // Spawn zones use config.size instead of sprite size
        auto* szComp = selectedEntity_->getComponent<SpawnZoneComponent>();
        float hw, hh;
        if (szComp && t) {
            hw = szComp->config.size.x * 0.5f + 2.0f;
            hh = szComp->config.size.y * 0.5f + 2.0f;
        } else if (s) {
            hw = s->size.x * 0.5f + 2.0f;
            hh = s->size.y * 0.5f + 2.0f;
        } else {
            hw = hh = 0.0f;
        }

        if (t && (s || szComp)) {
            // Resize handles only in Resize tool mode (E key)
            float minDim = (hw < hh ? hw : hh) * 2.0f;
            bool allowResize = (currentTool_ == EditorTool::Resize);
            // Spawn zones always allow resize (they're invisible, no sprite to click on)
            if (szComp) allowResize = true;
            float handleZone = 6.0f / camera->zoom();
            float distToCenter = worldPos.distance(t->position);
            float edgeThreshold = szComp ? 0.0f : (minDim * 0.5f) * 0.6f;

            if (allowResize && distToCenter > edgeThreshold) {
                Vec2 handles[8] = {
                    {t->position.x - hw, t->position.y + hh},
                    {t->position.x + hw, t->position.y + hh},
                    {t->position.x - hw, t->position.y - hh},
                    {t->position.x + hw, t->position.y - hh},
                    {t->position.x,      t->position.y + hh},
                    {t->position.x,      t->position.y - hh},
                    {t->position.x - hw, t->position.y},
                    {t->position.x + hw, t->position.y},
                };

                for (int i = 0; i < 8; i++) {
                    if (worldPos.distance(handles[i]) < handleZone + 4.0f / camera->zoom()) {
                        isResizingEntity_ = true;
                        isDraggingEntity_ = false;
                        resizeHandle_ = i;
                        dragStartWorldPos_ = worldPos;
                        dragStartEntityPos_ = t->position;
                        if (szComp) dragStartEntitySize_ = szComp->config.size;
                        else if (s) dragStartEntitySize_ = s->size;
                        return;
                    }
                }
            }

            // If click is inside the selected entity's bounds, drag it (don't reselect)
            Rect selBounds = {t->position.x - hw, t->position.y - hh, hw * 2.0f, hh * 2.0f};
            if (selBounds.contains(worldPos)) {
                isDraggingEntity_ = true;
                isResizingEntity_ = false;
                dragStartWorldPos_ = worldPos;
                dragStartEntityPos_ = t->position;
                if (szComp) dragStartEntitySize_ = szComp->config.size;
                else if (s) dragStartEntitySize_ = s->size;
                return;
            }
        }
    }

    // Also check spawn zones (no sprite, but have bounds)
    if (!best) {
        world->forEach<Transform, SpawnZoneComponent>(
            [&](Entity* entity, Transform* t, SpawnZoneComponent* sz) {
                Rect bounds = sz->getBounds(t->position);
                if (bounds.contains(worldPos)) {
                    // Prefer spawn zones at lower depth (they're background objects)
                    if (!best || t->depth > bestDepth) {
                        best = entity;
                        bestDepth = t->depth;
                    }
                }
            }
        );
    }

    // Click was outside the selected entity — select a new one
    if (best) {
        selectedEntity_ = best;
        isDraggingEntity_ = true;
        isResizingEntity_ = false;
        auto* t = best->getComponent<Transform>();
        dragStartWorldPos_ = worldPos;
        dragStartEntityPos_ = t->position;
        auto* s = best->getComponent<SpriteComponent>();
        auto* szBest = best->getComponent<SpawnZoneComponent>();
        if (szBest) dragStartEntitySize_ = szBest->config.size;
        else if (s) dragStartEntitySize_ = s->size;
    } else {
        selectedEntity_ = nullptr;
        isDraggingEntity_ = false;
    }
}

void Editor::handleSceneDrag(Camera* camera, const Vec2& screenPos,
                             int windowWidth, int windowHeight) {
    if (!selectedEntity_ || !camera) return;

    Vec2 worldPos = camera->screenToWorld(screenPos, windowWidth, windowHeight);
    Vec2 delta = worldPos - dragStartWorldPos_;

    // Resize mode
    if (isResizingEntity_) {
        Vec2 newSize = dragStartEntitySize_;
        // 0-3: corners (TL,TR,BL,BR), 4-7: edges (T,B,L,R)
        switch (resizeHandle_) {
            case 0: newSize.x -= delta.x; newSize.y += delta.y; break; // TL
            case 1: newSize.x += delta.x; newSize.y += delta.y; break; // TR
            case 2: newSize.x -= delta.x; newSize.y -= delta.y; break; // BL
            case 3: newSize.x += delta.x; newSize.y -= delta.y; break; // BR
            case 4: newSize.y += delta.y; break; // Top edge
            case 5: newSize.y -= delta.y; break; // Bottom edge
            case 6: newSize.x -= delta.x; break; // Left edge
            case 7: newSize.x += delta.x; break; // Right edge
        }
        if (newSize.x < 4.0f) newSize.x = 4.0f;
        if (newSize.y < 4.0f) newSize.y = 4.0f;

        // Apply to spawn zone or sprite
        auto* szComp = selectedEntity_->getComponent<SpawnZoneComponent>();
        if (szComp) {
            szComp->config.size = newSize;
        } else {
            auto* s = selectedEntity_->getComponent<SpriteComponent>();
            if (s) s->size = newSize;
        }
        return;
    }

    // Move mode
    if (!isDraggingEntity_) return;

    auto* t = selectedEntity_->getComponent<Transform>();
    if (!t) return;

    Vec2 newPos = dragStartEntityPos_ + delta;

    // Only grid-snap ground tiles; other entities move freely
    if (gridSnap_ && selectedEntity_->tag() == "ground") {
        float half = gridSize_ * 0.5f;
        newPos.x = std::floor(newPos.x / gridSize_) * gridSize_ + half;
        newPos.y = std::floor(newPos.y / gridSize_) * gridSize_ + half;
    }

    t->position = newPos;
}

void Editor::handleMouseUp() {
    // Record undo for completed drag/resize
    if (selectedEntity_) {
        if (isDraggingEntity_) {
            auto* t = selectedEntity_->getComponent<Transform>();
            if (t && t->position != dragStartEntityPos_) {
                auto cmd = std::make_unique<MoveCommand>();
                cmd->entityHandle = selectedEntity_->handle();
                cmd->oldPos = dragStartEntityPos_;
                cmd->newPos = t->position;
                UndoSystem::instance().push(std::move(cmd));
            }
        }
        if (isResizingEntity_) {
            Vec2 currentSize;
            auto* szComp = selectedEntity_->getComponent<SpawnZoneComponent>();
            auto* s = selectedEntity_->getComponent<SpriteComponent>();
            if (szComp) currentSize = szComp->config.size;
            else if (s) currentSize = s->size;

            if (currentSize != dragStartEntitySize_) {
                auto cmd = std::make_unique<ResizeCommand>();
                cmd->entityHandle = selectedEntity_->handle();
                cmd->oldSize = dragStartEntitySize_;
                cmd->newSize = currentSize;
                UndoSystem::instance().push(std::move(cmd));
            }
        }
    }
    isDraggingEntity_ = false;
    isResizingEntity_ = false;
    resizeHandle_ = -1;
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

                    // Helper lambda for right-click context menu on any file
                    auto drawFileContextMenu = [this](AssetEntry& asset) {
                        char menuId[128];
                        snprintf(menuId, sizeof(menuId), "ctx_%s", asset.name.c_str());
                        if (ImGui::BeginPopupContextItem(menuId)) {
                            ImGui::TextDisabled("%s", asset.name.c_str());
                            ImGui::Separator();

                            if (asset.type == AssetType::Sprite) {
                                if (ImGui::MenuItem("Place in Scene")) {
                                    isDraggingAsset_ = true;
                                    draggedAssetPath_ = asset.relativePath;
                                }
                            }
                            if (asset.type == AssetType::Script || asset.type == AssetType::Shader) {
                                if (ImGui::MenuItem("Open in VS Code")) {
                                    std::string cmd = "code \"" + asset.fullPath + "\"";
                                    system(cmd.c_str());
                                }
                            }
                            if (ImGui::MenuItem("Show in Explorer")) {
                                std::string dir = asset.fullPath;
                                size_t lastSlash = dir.find_last_of("/\\");
                                if (lastSlash != std::string::npos) dir = dir.substr(0, lastSlash);
                                std::string cmd = "explorer \"" + dir + "\"";
                                // Convert forward slashes to backslashes for Windows
                                for (auto& c : cmd) if (c == '/') c = '\\';
                                system(cmd.c_str());
                            }
                            if (ImGui::MenuItem("Copy Path")) {
                                SDL_SetClipboardText(asset.fullPath.c_str());
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Delete File")) {
                                if (fs::exists(asset.fullPath)) {
                                    fs::remove(asset.fullPath);
                                    LOG_INFO("Editor", "Deleted: %s", asset.fullPath.c_str());
                                    scanAssets(); // refresh
                                }
                            }
                            ImGui::EndPopup();
                        }
                    };

                    if (tab.type == AssetType::Sprite) {
                        // Grid view with thumbnails
                        float itemSize = 80.0f;
                        int columns = (int)(panelWidth / (itemSize + 8.0f));
                        if (columns < 1) columns = 1;
                        int col = 0;

                        for (auto& asset : assets_) {
                            if (asset.type != AssetType::Sprite) continue;

                            ImGui::PushID(asset.fullPath.c_str());
                            ImGui::BeginGroup();

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

                            // Right-click context menu
                            drawFileContextMenu(asset);

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

                            ImVec4 color = {0.8f, 0.8f, 0.8f, 1.0f};
                            if (tab.type == AssetType::Script) color = {0.4f, 0.8f, 1.0f, 1.0f};
                            else if (tab.type == AssetType::Scene) color = {0.4f, 1.0f, 0.6f, 1.0f};
                            else if (tab.type == AssetType::Shader) color = {1.0f, 0.7f, 0.4f, 1.0f};

                            ImGui::PushStyleColor(ImGuiCol_Text, color);
                            if (ImGui::Selectable(asset.name.c_str())) {
                                LOG_INFO("Editor", "File: %s", asset.fullPath.c_str());
                            }
                            ImGui::PopStyleColor();

                            // Right-click context menu
                            drawFileContextMenu(asset);

                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", asset.fullPath.c_str());
                            }

                            ImGui::PopID();
                        }
                    }

                    ImGui::EndTabItem();
                }
            }
            // Prefabs tab
            if (ImGui::BeginTabItem("Prefabs")) {
                auto& lib = PrefabLibrary::instance();
                auto prefabNames = lib.names();

                if (ImGui::Button("Refresh")) {
                    lib.loadAll();
                }
                ImGui::SameLine();
                ImGui::Text("(%zu prefabs)", prefabNames.size());

                ImGui::Separator();

                for (auto& pname : prefabNames) {
                    ImGui::PushID(pname.c_str());

                    bool selected = (draggedAssetPath_ == "prefab:" + pname);
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.6f, 1.0f, 0.5f));

                    if (ImGui::Selectable(pname.c_str(), selected)) {
                        isDraggingAsset_ = true;
                        draggedAssetPath_ = "prefab:" + pname;
                    }

                    if (selected) ImGui::PopStyleColor();

                    // Right-click context menu for prefabs
                    if (ImGui::BeginPopupContextItem()) {
                        ImGui::TextDisabled("%s", pname.c_str());
                        ImGui::Separator();
                        if (ImGui::MenuItem("Place in Scene")) {
                            isDraggingAsset_ = true;
                            draggedAssetPath_ = "prefab:" + pname;
                        }
                        if (ImGui::MenuItem("Copy Path")) {
                            std::string path = "assets/prefabs/" + pname + ".json";
                            SDL_SetClipboardText(path.c_str());
                        }
                        if (ImGui::MenuItem("Delete Prefab")) {
                            // Delete from runtime dir
                            std::string path = "assets/prefabs/" + pname + ".json";
                            if (fs::exists(path)) fs::remove(path);
                            // Delete from source dir if set
                            auto& plib = PrefabLibrary::instance();
                            // The library knows both paths, just reload
                            lib.loadAll();
                            LOG_INFO("Editor", "Deleted prefab: %s", pname.c_str());
                            ImGui::EndPopup();
                            ImGui::PopID();
                            break; // list changed, stop iterating
                        }
                        ImGui::EndPopup();
                    }

                    if (ImGui::IsItemHovered()) {
                        auto* json = lib.getJson(pname);
                        if (json && json->contains("components")) {
                            std::string tooltip = "Components:";
                            for (auto& [key, _] : (*json)["components"].items()) {
                                tooltip += "\n  - " + key;
                            }
                            ImGui::SetTooltip("%s", tooltip.c_str());
                        }
                    }

                    ImGui::PopID();
                }

                if (prefabNames.empty()) {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1),
                        "No prefabs. Select an entity and use Entity > Save as Prefab");
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

// ============================================================================
// Scene Grid Overlay
// ============================================================================

// ============================================================================
// Tile Palette
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

void Editor::paintTileAt(World* world, Camera* camera, const Vec2& screenPos,
                         int windowWidth, int windowHeight) {
    if (!world || !camera || selectedTileIndex_ < 0 || !paletteTexture_) return;
    if (paletteColumns_ <= 0 || paletteRows_ <= 0 || paletteTileSize_ <= 0) return;
    if (selectedTileIndex_ >= paletteColumns_ * paletteRows_) return;

    Vec2 worldPos = camera->screenToWorld(screenPos, windowWidth, windowHeight);

    // Snap to grid center
    float half = gridSize_ * 0.5f;
    worldPos.x = std::floor(worldPos.x / gridSize_) * gridSize_ + half;
    worldPos.y = std::floor(worldPos.y / gridSize_) * gridSize_ + half;

    // Check if there's already a ground tile at this position (avoid stacking)
    bool occupied = false;
    world->forEach<Transform, SpriteComponent>(
        [&](Entity* entity, Transform* t, SpriteComponent* s) {
            if (entity->tag() == "ground" &&
                std::abs(t->position.x - worldPos.x) < 1.0f &&
                std::abs(t->position.y - worldPos.y) < 1.0f) {
                // Update existing tile's texture region instead of creating new
                int col = selectedTileIndex_ % paletteColumns_;
                int row = selectedTileIndex_ / paletteColumns_;
                float texW = (float)paletteTexture_->width();
                float texH = (float)paletteTexture_->height();

                s->texture = paletteTexture_;
                s->texturePath = paletteTexturePath_;
                s->sourceRect = {
                    (col * paletteTileSize_) / texW,
                    1.0f - ((row + 1) * paletteTileSize_) / texH,
                    paletteTileSize_ / texW,
                    paletteTileSize_ / texH
                };
                s->size = {(float)paletteTileSize_, (float)paletteTileSize_};
                occupied = true;
            }
        }
    );

    if (!occupied) {
        // Create new tile entity
        int col = selectedTileIndex_ % paletteColumns_;
        int row = selectedTileIndex_ / paletteColumns_;
        float texW = (float)paletteTexture_->width();
        float texH = (float)paletteTexture_->height();

        Entity* tile = world->createEntity("Tile");
        tile->setTag("ground");

        auto* transform = tile->addComponent<Transform>(worldPos);
        transform->depth = 0.0f;

        auto* sprite = tile->addComponent<SpriteComponent>();
        sprite->texture = paletteTexture_;
        sprite->texturePath = paletteTexturePath_;
        sprite->sourceRect = {
            (col * paletteTileSize_) / texW,
            1.0f - ((row + 1) * paletteTileSize_) / texH,
            paletteTileSize_ / texW,
            paletteTileSize_ / texH
        };
        sprite->size = {(float)paletteTileSize_, (float)paletteTileSize_};
    }
}

void Editor::drawTilePalette(World* world, Camera* camera) {
    if (!ImGui::Begin("Tile Palette", nullptr, ImGuiWindowFlags_None)) {
        ImGui::End();
        return; // collapsed
    }

    // Tileset selector
    if (ImGui::BeginCombo("##Tileset", paletteTexturePath_.empty() ? "Select tileset..." :
                          fs::path(paletteTexturePath_).filename().string().c_str())) {
        std::string tilesDir = assetRoot_ + "/tiles";
        if (fs::exists(tilesDir)) {
            for (auto& entry : fs::directory_iterator(tilesDir)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".png" && ext != ".jpg") continue;

                std::string path = entry.path().string();
                std::replace(path.begin(), path.end(), '\\', '/');
                std::string name = entry.path().filename().string();

                if (ImGui::Selectable(name.c_str(), paletteTexturePath_ == path)) {
                    loadTileset(path, paletteTileSize_);
                }
            }
        }
        ImGui::EndCombo();
    }

    // Tile size with reload on change
    int prevSize = paletteTileSize_;
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("Tile px", &paletteTileSize_, 8, 16);
    if (paletteTileSize_ < 8) paletteTileSize_ = 8;
    if (paletteTileSize_ > 256) paletteTileSize_ = 256;
    if (paletteTileSize_ != prevSize && paletteTexture_) {
        paletteColumns_ = paletteTexture_->width() / paletteTileSize_;
        paletteRows_ = paletteTexture_->height() / paletteTileSize_;
        if (paletteColumns_ < 1) paletteColumns_ = 1;
        if (paletteRows_ < 1) paletteRows_ = 1;
        selectedTileIndex_ = -1;
    }

    if (!paletteTexture_ || paletteColumns_ <= 0 || paletteRows_ <= 0) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Place .png tilesets in assets/tiles/");
        ImGui::End();
        return;
    }

    // Paint mode toggle
    if (currentTool_ == EditorTool::Paint) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        if (ImGui::Button("PAINTING")) currentTool_ = EditorTool::Move;
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button("Paint") && selectedTileIndex_ >= 0) currentTool_ = EditorTool::Paint;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) { currentTool_ = EditorTool::Move; selectedTileIndex_ = -1; }

    if (selectedTileIndex_ >= 0 && paletteColumns_ > 0) {
        ImGui::SameLine();
        ImGui::Text("(%d,%d)", selectedTileIndex_ % paletteColumns_, selectedTileIndex_ / paletteColumns_);
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
                currentTool_ = EditorTool::Paint;
            }

            if (selected) ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

// ============================================================================
// Scene Grid
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

    // Selection highlight + resize handles
    if (selectedEntity_) {
        auto* t = selectedEntity_->getComponent<Transform>();
        auto* s = selectedEntity_->getComponent<SpriteComponent>();
        auto* szSel = selectedEntity_->getComponent<SpawnZoneComponent>();

        // Determine bounds from sprite OR spawn zone
        float hw = 0, hh = 0;
        if (szSel) {
            hw = szSel->config.size.x * 0.5f + 2.0f;
            hh = szSel->config.size.y * 0.5f + 2.0f;
        } else if (s) {
            hw = s->size.x * 0.5f + 2.0f;
            hh = s->size.y * 0.5f + 2.0f;
        }

        if (t && (s || szSel) && hw > 0) {
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

// ============================================================================
// Scene Save / Load
// ============================================================================

void Editor::saveScene(World* world, const std::string& path) {
    if (!world) return;
    currentScenePath_ = path;

    nlohmann::json root;
    root["version"] = 1;
    root["gridSize"] = gridSize_;
    root["sceneName"] = SceneManager::instance().currentSceneName();

    nlohmann::json entitiesJson = nlohmann::json::array();

    world->forEachEntity([&](Entity* entity) {
        // Skip transient entities — mobs/bosses are spawned at runtime by SpawnSystem
        std::string tag = entity->tag();
        if (tag == "mob" || tag == "boss") return;

        nlohmann::json ej;
        ej["name"] = entity->name();
        ej["tag"] = tag;
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
                {"sourceRect", {s->sourceRect.x, s->sourceRect.y, s->sourceRect.w, s->sourceRect.h}},
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
                {"moveSpeed", p->moveSpeed},
                {"isLocalPlayer", p->isLocalPlayer}
            };
        }

        if (auto* z = entity->getComponent<ZoneComponent>()) {
            comps["Zone"] = {
                {"zoneName", z->zoneName},
                {"displayName", z->displayName},
                {"size", {z->size.x, z->size.y}},
                {"zoneType", z->zoneType},
                {"minLevel", z->minLevel},
                {"maxLevel", z->maxLevel},
                {"pvpEnabled", z->pvpEnabled}
            };
        }

        if (auto* p = entity->getComponent<PortalComponent>()) {
            comps["Portal"] = {
                {"triggerSize", {p->triggerSize.x, p->triggerSize.y}},
                {"targetScene", p->targetScene},
                {"targetZone", p->targetZone},
                {"targetSpawnPos", {p->targetSpawnPos.x, p->targetSpawnPos.y}},
                {"useFadeTransition", p->useFadeTransition},
                {"fadeDuration", p->fadeDuration},
                {"showLabel", p->showLabel},
                {"label", p->label}
            };
        }

        ej["components"] = comps;
        entitiesJson.push_back(ej);
    });

    root["entities"] = entitiesJson;

    std::string jsonStr = root.dump(2);

    // Save to runtime dir (where exe runs)
    {
        auto parentDir = fs::path(path).parent_path();
        if (!fs::exists(parentDir)) fs::create_directories(parentDir);
        std::ofstream file(path);
        file << jsonStr;
    }

    // Also save to source dir (persists across rebuilds)
    if (!sourceDir_.empty()) {
        std::string srcPath = sourceDir_ + "/" + fs::path(path).filename().string();
        auto parentDir = fs::path(srcPath).parent_path();
        if (!fs::exists(parentDir)) fs::create_directories(parentDir);
        std::ofstream srcFile(srcPath);
        if (srcFile.is_open()) {
            srcFile << jsonStr;
            LOG_INFO("Editor", "Scene also saved to source: %s", srcPath.c_str());
        }
    }

    LOG_INFO("Editor", "Scene saved to %s (%zu entities)", path.c_str(), world->entityCount());
}

void Editor::loadScene(World* world, const std::string& path) {
    if (!world) return;
    currentScenePath_ = path;

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
        world->destroyEntity(entity->handle());
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
            // Placeholder for missing textures so entities are still visible
            if (!s->texture) {
                unsigned char px[] = {255, 0, 255, 255, 200, 0, 200, 255,
                                      200, 0, 200, 255, 255, 0, 255, 255};
                auto tex = std::make_shared<Texture>();
                tex->loadFromMemory(px, 2, 2, 4);
                s->texture = tex;
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
            if (sj.contains("sourceRect")) {
                auto sr = sj["sourceRect"];
                s->sourceRect = {sr[0].get<float>(), sr[1].get<float>(),
                                 sr[2].get<float>(), sr[3].get<float>()};
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
            p->isLocalPlayer = pj.value("isLocalPlayer", false);
        }

        if (comps.contains("Zone")) {
            auto& zj = comps["Zone"];
            auto* z = entity->addComponent<ZoneComponent>();
            z->zoneName = zj.value("zoneName", "");
            z->displayName = zj.value("displayName", "");
            if (zj.contains("size")) {
                auto s = zj["size"];
                z->size = {s[0].get<float>(), s[1].get<float>()};
            }
            z->zoneType = zj.value("zoneType", "zone");
            z->minLevel = zj.value("minLevel", 1);
            z->maxLevel = zj.value("maxLevel", 99);
            z->pvpEnabled = zj.value("pvpEnabled", false);
        }

        if (comps.contains("Portal")) {
            auto& pj = comps["Portal"];
            auto* p = entity->addComponent<PortalComponent>();
            if (pj.contains("triggerSize")) {
                auto ts = pj["triggerSize"];
                p->triggerSize = {ts[0].get<float>(), ts[1].get<float>()};
            }
            p->targetScene = pj.value("targetScene", "");
            p->targetZone = pj.value("targetZone", "");
            if (pj.contains("targetSpawnPos")) {
                auto sp = pj["targetSpawnPos"];
                p->targetSpawnPos = {sp[0].get<float>(), sp[1].get<float>()};
            }
            p->useFadeTransition = pj.value("useFadeTransition", true);
            p->fadeDuration = pj.value("fadeDuration", 0.3f);
            p->showLabel = pj.value("showLabel", true);
            p->label = pj.value("label", "");
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
    // Menu bar content is now integrated into the toolbar — this just handles the prefab popup

    if (openSavePrefab_) {
        ImGui::OpenPopup("SavePrefabPopup");
        openSavePrefab_ = false;
    }

    static char prefabNameBuf[64] = "";
    if (ImGui::BeginPopupModal("SavePrefabPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save as Prefab");
        ImGui::Separator();
        if (selectedEntity_ && prefabNameBuf[0] == '\0') {
            strncpy(prefabNameBuf, selectedEntity_->name().c_str(), sizeof(prefabNameBuf) - 1);
        }
        ImGui::InputText("Name", prefabNameBuf, sizeof(prefabNameBuf));
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(120, 0)) && prefabNameBuf[0] != '\0') {
            PrefabLibrary::instance().save(prefabNameBuf, selectedEntity_);
            prefabNameBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            prefabNameBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ============================================================================
// Toolbar
// ============================================================================

void Editor::drawToolbar(World* /*world*/) {
    // Toolbar content has been moved:
    //   File/Edit/View/Entity menus -> DockSpace menu bar
    //   Play/tool/toggle buttons    -> Scene viewport toolbar
    // This method is intentionally empty.
}

// ============================================================================
// Hierarchy
// ============================================================================

void Editor::drawHierarchy(World* world) {
    if (ImGui::Begin("Hierarchy")) {
        if (!world) {
            ImGui::Text("No active scene");
            ImGui::End();
            return;
        }

        // Compact search box with hint text
        static char searchBuf[128] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##Search", "Search...", searchBuf, sizeof(searchBuf));
        ImGui::Spacing();

        std::string filter(searchBuf);

        // Group entities by name+tag, show groups as collapsible tree nodes
        struct GroupInfo {
            std::string name;
            std::string tag;
            std::vector<Entity*> entities;
        };
        std::vector<GroupInfo> groups;
        std::unordered_map<std::string, int> groupIndex;

        world->forEachEntity([&](Entity* entity) {
            if (!entity) return;

            if (!filter.empty()) {
                std::string combined = entity->name() + std::string(" ") + entity->tag();
                std::string filterLower = filter;
                std::transform(combined.begin(), combined.end(), combined.begin(), ::tolower);
                std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
                if (combined.find(filterLower) == std::string::npos) return;
            }

            std::string key = entity->name() + "|" + entity->tag();
            auto it = groupIndex.find(key);
            if (it == groupIndex.end()) {
                groupIndex[key] = (int)groups.size();
                groups.push_back({entity->name(), entity->tag(), {entity}});
            } else {
                groups[it->second].entities.push_back(entity);
            }
        });

        auto getTagColor = [](const std::string& tag) -> ImVec4 {
            if (tag == "player") return {0.3f, 0.8f, 1.0f, 1.0f};
            if (tag == "obstacle") return {1.0f, 0.6f, 0.3f, 1.0f};
            if (tag == "ground") return {0.5f, 0.8f, 0.5f, 1.0f};
            if (tag == "mob") return {1.0f, 0.4f, 0.4f, 1.0f};
            if (tag == "boss") return {1.0f, 0.2f, 0.8f, 1.0f};
            return {0.8f, 0.8f, 0.8f, 1.0f};
        };

        for (auto& group : groups) {
            bool hasTag = !group.tag.empty();
            ImVec4 color = getTagColor(group.tag);

            if (group.entities.size() == 1) {
                // Single entity -- show as leaf (no ID, just name like Unity)
                Entity* entity = group.entities[0];
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                         | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
                if (entity == selectedEntity_) flags |= ImGuiTreeNodeFlags_Selected;

                auto* spr = entity->getComponent<SpriteComponent>();
                bool hasError = spr && !spr->texture;

                if (hasError) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
                else if (hasTag) ImGui::PushStyleColor(ImGuiCol_Text, color);

                ImGui::TreeNodeEx((void*)(intptr_t)entity->id(), flags, "%s%s",
                    entity->name().c_str(), hasError ? " (!)" : "");

                if (ImGui::IsItemClicked()) selectedEntity_ = entity;
                if (hasError || hasTag) ImGui::PopStyleColor();
            } else {
                // Multiple entities -- group with child count badge
                if (hasTag) ImGui::PushStyleColor(ImGuiCol_Text, color);

                ImGuiTreeNodeFlags groupFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;

                char groupLabel[256];
                snprintf(groupLabel, sizeof(groupLabel), "%s (x%zu)",
                    group.name.c_str(), group.entities.size());

                bool open = ImGui::TreeNodeEx(groupLabel, groupFlags);
                if (hasTag) ImGui::PopStyleColor();

                if (open) {
                    for (auto* entity : group.entities) {
                        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                                 | ImGuiTreeNodeFlags_SpanAvailWidth;
                        if (entity == selectedEntity_) flags |= ImGuiTreeNodeFlags_Selected;

                        ImGui::TreeNodeEx((void*)(intptr_t)entity->id(), flags, "%s", entity->name().c_str());

                        if (ImGui::IsItemClicked()) selectedEntity_ = entity;
                    }
                    ImGui::TreePop();
                }
            }
        }
    }
    ImGui::End();
}

// ============================================================================
// Inspector
// ============================================================================

void Editor::drawInspector() {
    if (ImGui::Begin("Inspector")) {
        if (!selectedEntity_) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select an entity");
            ImGui::End();
            return;
        }

        // -- Entity name (prominent input at top) --
        char nameBuf[128];
        strncpy(nameBuf, selectedEntity_->name().c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##EntityName", nameBuf, sizeof(nameBuf))) {
            selectedEntity_->setName(nameBuf);
        }

        // Tag + Active on same line
        {
            char tagBuf[64];
            strncpy(tagBuf, selectedEntity_->tag().c_str(), sizeof(tagBuf) - 1);
            tagBuf[sizeof(tagBuf) - 1] = '\0';

            bool active = selectedEntity_->isActive();
            if (ImGui::Checkbox("##Active", &active)) {
                selectedEntity_->setActive(active);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##Tag", "Tag", tagBuf, sizeof(tagBuf))) {
                selectedEntity_->setTag(tagBuf);
            }
        }

        ImGui::Spacing();

        // Helper macro for a two-column property row
        #define INSPECTOR_ROW(labelText) \
            ImGui::TableNextRow(); \
            ImGui::TableSetColumnIndex(0); \
            ImGui::AlignTextToFramePadding(); \
            ImGui::Text(labelText); \
            ImGui::TableSetColumnIndex(1); \
            ImGui::SetNextItemWidth(-1)

        // Transform
        if (auto* t = selectedEntity_->getComponent<Transform>()) {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginTable("##TransformProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Position");
                    ImGui::DragFloat2("##pos", &t->position.x, 1.0f);

                    INSPECTOR_ROW("Tile");
                    Vec2 tile = Coords::toTile(t->position);
                    ImGui::Text("(%d, %d)", (int)tile.x, (int)tile.y);

                    INSPECTOR_ROW("Scale");
                    ImGui::DragFloat2("##scale", &t->scale.x, 0.01f, 0.01f, 10.0f);

                    INSPECTOR_ROW("Rotation");
                    float degrees = t->rotation * 57.2957795f;
                    if (ImGui::DragFloat("##rot", &degrees, 1.0f, -360.0f, 360.0f)) {
                        t->rotation = degrees * 0.0174532925f;
                    }

                    INSPECTOR_ROW("Depth");
                    ImGui::DragFloat("##depth", &t->depth, 0.1f);

                    ImGui::EndTable();
                }
            }
        }

        // Sprite
        if (auto* s = selectedEntity_->getComponent<SpriteComponent>()) {
            if (ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginTable("##SpriteProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Texture");
                    {
                        // Sprite texture selector — dropdown of available sprite assets
                        std::string currentName = s->texturePath.empty() ? "(none)" :
                            fs::path(s->texturePath).filename().string();
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::BeginCombo("##sprTex", currentName.c_str())) {
                            // Option to clear
                            if (ImGui::Selectable("(none)", s->texturePath.empty())) {
                                s->texturePath.clear();
                                s->texture = nullptr;
                            }
                            // List all sprite assets
                            for (auto& asset : assets_) {
                                if (asset.type != AssetType::Sprite) continue;
                                bool selected = (asset.relativePath == s->texturePath);
                                if (ImGui::Selectable(asset.name.c_str(), selected)) {
                                    s->texturePath = asset.relativePath;
                                    s->texture = TextureCache::instance().load(asset.relativePath);
                                    if (s->texture) {
                                        s->size = {(float)s->texture->width(), (float)s->texture->height()};
                                    }
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }

                    if (s->texture) {
                        INSPECTOR_ROW("Tex Size");
                        ImGui::Text("%dx%d", s->texture->width(), s->texture->height());

                        INSPECTOR_ROW("Preview");
                        ImTextureID texId = (ImTextureID)(intptr_t)s->texture->id();
                        ImGui::Image(texId, ImVec2(48, 48), ImVec2(0, 1), ImVec2(1, 0));
                    }

                    INSPECTOR_ROW("Size");
                    ImGui::DragFloat2("##sprSize", &s->size.x, 1.0f, 1.0f, 2048.0f);

                    INSPECTOR_ROW("Tint");
                    ImGui::ColorEdit4("##tint", &s->tint.r, ImGuiColorEditFlags_NoLabel);

                    INSPECTOR_ROW("Flip");
                    ImGui::Checkbox("X##flipX", &s->flipX);
                    ImGui::SameLine();
                    ImGui::Checkbox("Y##flipY", &s->flipY);

                    ImGui::EndTable();
                }

                // Source rect (UV region of the texture -- for tileset tiles)
                if (ImGui::TreeNode("Source Rect (UV)")) {
                    ImGui::DragFloat4("XYWH", &s->sourceRect.x, 0.01f, 0.0f, 1.0f);
                    if (s->texture) {
                        int px = (int)(s->sourceRect.x * s->texture->width());
                        int py = (int)(s->sourceRect.y * s->texture->height());
                        int pw = (int)(s->sourceRect.w * s->texture->width());
                        int ph = (int)(s->sourceRect.h * s->texture->height());
                        ImGui::Text("Pixel region: (%d, %d) %dx%d", px, py, pw, ph);
                    }
                    ImGui::TreePop();
                }
            }
        }

        // BoxCollider
        if (auto* c = selectedEntity_->getComponent<BoxCollider>()) {
            bool open = ImGui::CollapsingHeader("Box Collider", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::BeginPopupContextItem("##rmBoxCollider")) {
                if (ImGui::MenuItem("Remove Component")) {
                    selectedEntity_->removeComponent<BoxCollider>();
                    ImGui::EndPopup();
                    goto endInspectorComponents;
                }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<BoxCollider>()) {
                if (ImGui::BeginTable("##BoxColProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Size");
                    ImGui::DragFloat2("##boxSize", &c->size.x, 0.5f, 1.0f, 512.0f);

                    INSPECTOR_ROW("Offset");
                    ImGui::DragFloat2("##boxOff", &c->offset.x, 0.5f);

                    INSPECTOR_ROW("Trigger");
                    ImGui::Checkbox("##boxTrig", &c->isTrigger);

                    INSPECTOR_ROW("Static");
                    ImGui::Checkbox("##boxStatic", &c->isStatic);

                    ImGui::EndTable();
                }

                auto* spr = selectedEntity_->getComponent<SpriteComponent>();
                if (spr && ImGui::Button("Fit to Sprite##box")) {
                    c->size = spr->size;
                    c->offset = {0.0f, 0.0f};
                }
            }
        }

        // PolygonCollider
        if (auto* pc = selectedEntity_->getComponent<PolygonCollider>()) {
            bool open = ImGui::CollapsingHeader("Polygon Collider", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::BeginPopupContextItem("##rmPolyCollider")) {
                if (ImGui::MenuItem("Remove Component")) {
                    selectedEntity_->removeComponent<PolygonCollider>();
                    ImGui::EndPopup();
                    goto endInspectorComponents;
                }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<PolygonCollider>()) {
                if (ImGui::BeginTable("##PolyColProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Trigger");
                    ImGui::Checkbox("##polyTrig", &pc->isTrigger);

                    INSPECTOR_ROW("Static");
                    ImGui::Checkbox("##polyStatic", &pc->isStatic);

                    INSPECTOR_ROW("Vertices");
                    ImGui::Text("%zu", pc->points.size());

                    ImGui::EndTable();
                }

                int removeIdx = -1;
                for (int i = 0; i < (int)pc->points.size(); i++) {
                    ImGui::PushID(i);
                    char label[16];
                    snprintf(label, sizeof(label), "V%d", i);
                    ImGui::DragFloat2(label, &pc->points[i].x, 0.5f);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) removeIdx = i;
                    ImGui::PopID();
                }
                if (removeIdx >= 0) {
                    pc->points.erase(pc->points.begin() + removeIdx);
                }

                if (ImGui::Button("+ Vertex")) {
                    if (pc->points.empty()) pc->points.push_back({-8, -8});
                    else { Vec2 last = pc->points.back(); pc->points.push_back({last.x + 8, last.y}); }
                }
                ImGui::SameLine();
                if (ImGui::Button("Box##poly")) {
                    auto* spr = selectedEntity_->getComponent<SpriteComponent>();
                    float w = spr ? spr->size.x : 32.0f;
                    float h = spr ? spr->size.y : 32.0f;
                    *pc = PolygonCollider::makeBox(w, h);
                }
                ImGui::SameLine();
                if (ImGui::Button("Circle##poly")) {
                    auto* spr = selectedEntity_->getComponent<SpriteComponent>();
                    float r = spr ? (spr->size.x + spr->size.y) * 0.25f : 16.0f;
                    *pc = PolygonCollider::makeCircleApprox(r, 8);
                }
            }
        }

        // PlayerController
        if (auto* p = selectedEntity_->getComponent<PlayerController>()) {
            bool open = ImGui::CollapsingHeader("Player Controller", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::BeginPopupContextItem("##rmPlayerCtrl")) {
                if (ImGui::MenuItem("Remove Component")) {
                    selectedEntity_->removeComponent<PlayerController>();
                    ImGui::EndPopup();
                    goto endInspectorComponents;
                }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<PlayerController>()) {
                if (ImGui::BeginTable("##PlayerCtrlProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Speed");
                    ImGui::DragFloat("##moveSpd", &p->moveSpeed, 1.0f, 0.0f, 500.0f);

                    INSPECTOR_ROW("Local");
                    ImGui::Checkbox("##isLocal", &p->isLocalPlayer);

                    INSPECTOR_ROW("Facing");
                    const char* dirs[] = {"None", "Up", "Down", "Left", "Right"};
                    ImGui::Text("%s | %s", dirs[(int)p->facing], p->isMoving ? "Moving" : "Idle");

                    ImGui::EndTable();
                }
            }
        }

        // Animator
        if (auto* a = selectedEntity_->getComponent<Animator>()) {
            bool open = ImGui::CollapsingHeader("Animator");
            if (ImGui::BeginPopupContextItem("##rmAnimator")) {
                if (ImGui::MenuItem("Remove Component")) {
                    selectedEntity_->removeComponent<Animator>();
                    ImGui::EndPopup();
                    goto endInspectorComponents;
                }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<Animator>()) {
                if (ImGui::BeginTable("##AnimatorProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Animation");
                    ImGui::Text("%s", a->currentAnimation.c_str());

                    INSPECTOR_ROW("State");
                    ImGui::Text("%s | %.2f", a->playing ? "Playing" : "Stopped", a->timer);

                    ImGui::EndTable();
                }
            }
        }

        // ZoneComponent
        if (auto* z = selectedEntity_->getComponent<ZoneComponent>()) {
            bool open = ImGui::CollapsingHeader("Zone", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::BeginPopupContextItem("##rmZone")) {
                if (ImGui::MenuItem("Remove Component")) {
                    selectedEntity_->removeComponent<ZoneComponent>();
                    ImGui::EndPopup();
                    goto endInspectorComponents;
                }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<ZoneComponent>()) {
                if (ImGui::BeginTable("##ZoneProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Zone Name");
                    char znameBuf[64];
                    strncpy(znameBuf, z->zoneName.c_str(), sizeof(znameBuf) - 1);
                    znameBuf[sizeof(znameBuf) - 1] = '\0';
                    if (ImGui::InputText("##zoneName", znameBuf, sizeof(znameBuf))) z->zoneName = znameBuf;

                    INSPECTOR_ROW("Display");
                    char dispBuf[64];
                    strncpy(dispBuf, z->displayName.c_str(), sizeof(dispBuf) - 1);
                    dispBuf[sizeof(dispBuf) - 1] = '\0';
                    if (ImGui::InputText("##zoneDisp", dispBuf, sizeof(dispBuf))) z->displayName = dispBuf;

                    INSPECTOR_ROW("Size");
                    ImGui::DragFloat2("##zoneSize", &z->size.x, 8.0f, 32.0f, 10000.0f);

                    ImGui::EndTable();
                }

                const char* types[] = {"town", "zone", "dungeon"};
                int typeIdx = 0;
                if (z->zoneType == "zone") typeIdx = 1;
                if (z->zoneType == "dungeon") typeIdx = 2;
                if (ImGui::Combo("##zoneType", &typeIdx, types, 3)) {
                    z->zoneType = types[typeIdx];
                }

                if (ImGui::BeginTable("##ZoneProps2", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Min Level");
                    ImGui::DragInt("##zMinLvl", &z->minLevel, 1, 1, 99);

                    INSPECTOR_ROW("Max Level");
                    ImGui::DragInt("##zMaxLvl", &z->maxLevel, 1, 1, 99);

                    INSPECTOR_ROW("PvP");
                    ImGui::Checkbox("##zPvp", &z->pvpEnabled);

                    ImGui::EndTable();
                }
            }
        }

        // PortalComponent
        if (auto* p = selectedEntity_->getComponent<PortalComponent>()) {
            bool open = ImGui::CollapsingHeader("Portal", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::BeginPopupContextItem("##rmPortal")) {
                if (ImGui::MenuItem("Remove Component")) {
                    selectedEntity_->removeComponent<PortalComponent>();
                    ImGui::EndPopup();
                    goto endInspectorComponents;
                }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<PortalComponent>()) {
                if (ImGui::BeginTable("##PortalProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Trigger");
                    ImGui::DragFloat2("##pTrigSz", &p->triggerSize.x, 1.0f, 8.0f, 256.0f);

                    INSPECTOR_ROW("Scene");
                    char sceneBuf[64];
                    strncpy(sceneBuf, p->targetScene.c_str(), sizeof(sceneBuf) - 1);
                    sceneBuf[sizeof(sceneBuf) - 1] = '\0';
                    if (ImGui::InputText("##pScene", sceneBuf, sizeof(sceneBuf))) p->targetScene = sceneBuf;

                    INSPECTOR_ROW("Zone");
                    char zoneBuf[64];
                    strncpy(zoneBuf, p->targetZone.c_str(), sizeof(zoneBuf) - 1);
                    zoneBuf[sizeof(zoneBuf) - 1] = '\0';
                    if (ImGui::InputText("##pZone", zoneBuf, sizeof(zoneBuf))) p->targetZone = zoneBuf;

                    INSPECTOR_ROW("Spawn Pos");
                    ImGui::DragFloat2("##pSpawn", &p->targetSpawnPos.x, 1.0f);

                    INSPECTOR_ROW("Fade");
                    ImGui::Checkbox("##pFade", &p->useFadeTransition);

                    if (p->useFadeTransition) {
                        INSPECTOR_ROW("Duration");
                        ImGui::DragFloat("##pFadeDur", &p->fadeDuration, 0.05f, 0.1f, 2.0f);
                    }

                    INSPECTOR_ROW("Label");
                    ImGui::Checkbox("##pShowLbl", &p->showLabel);

                    INSPECTOR_ROW("Override");
                    char labelBuf[64];
                    strncpy(labelBuf, p->label.c_str(), sizeof(labelBuf) - 1);
                    labelBuf[sizeof(labelBuf) - 1] = '\0';
                    if (ImGui::InputText("##pLabel", labelBuf, sizeof(labelBuf))) p->label = labelBuf;

                    ImGui::EndTable();
                }
            }
        }

        // ---- Game Components (editable in inspector) ----

        // Character Stats
        if (auto* cs = selectedEntity_->getComponent<CharacterStatsComponent>()) {
            bool open = ImGui::CollapsingHeader("Character Stats");
            if (ImGui::BeginPopupContextItem("##rmCharStats")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<CharacterStatsComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<CharacterStatsComponent>()) {
                auto& s = cs->stats;
                char nameBuf[64]; strncpy(nameBuf, s.characterName.c_str(), sizeof(nameBuf)-1); nameBuf[sizeof(nameBuf)-1]=0;
                if (ImGui::InputText("Char Name##cs", nameBuf, sizeof(nameBuf))) s.characterName = nameBuf;

                // Class selector - reconfigures ClassDefinition on change
                const char* classNames[] = {"Warrior", "Mage", "Archer"};
                int classIdx = (int)s.classDef.classType;
                if (ImGui::Combo("Class##cs", &classIdx, classNames, 3)) {
                    auto& cd = s.classDef;
                    cd.classType = (ClassType)classIdx;
                    switch (cd.classType) {
                        case ClassType::Warrior:
                            cd.displayName = "Warrior"; s.className = "Warrior";
                            cd.baseMaxHP = 70; cd.baseMaxMP = 30;
                            cd.baseStrength = 14; cd.baseVitality = 12;
                            cd.baseIntelligence = 5; cd.baseDexterity = 8; cd.baseWisdom = 5;
                            cd.baseHitRate = 4.0f; cd.attackRange = 1.0f;
                            cd.primaryResource = ResourceType::Fury;
                            cd.hpPerLevel = 7.0f; cd.mpPerLevel = 2.0f;
                            cd.strPerLevel = 0.25f; cd.vitPerLevel = 0.25f;
                            cd.intPerLevel = 0.0f; cd.dexPerLevel = 0.0f; cd.wisPerLevel = 0.0f;
                            break;
                        case ClassType::Mage:
                            cd.displayName = "Mage"; s.className = "Mage";
                            cd.baseMaxHP = 50; cd.baseMaxMP = 150;
                            cd.baseStrength = 4; cd.baseVitality = 6;
                            cd.baseIntelligence = 16; cd.baseDexterity = 6; cd.baseWisdom = 14;
                            cd.baseHitRate = 0.0f; cd.attackRange = 7.0f;
                            cd.primaryResource = ResourceType::Mana;
                            cd.hpPerLevel = 5.0f; cd.mpPerLevel = 10.0f;
                            cd.strPerLevel = 0.0f; cd.vitPerLevel = 0.25f;
                            cd.intPerLevel = 0.25f; cd.dexPerLevel = 0.0f; cd.wisPerLevel = 0.25f;
                            break;
                        case ClassType::Archer:
                            cd.displayName = "Archer"; s.className = "Archer";
                            cd.baseMaxHP = 50; cd.baseMaxMP = 40;
                            cd.baseStrength = 8; cd.baseVitality = 9;
                            cd.baseIntelligence = 7; cd.baseDexterity = 18; cd.baseWisdom = 8;
                            cd.baseHitRate = 4.0f; cd.attackRange = 7.0f;
                            cd.primaryResource = ResourceType::Fury;
                            cd.hpPerLevel = 5.0f; cd.mpPerLevel = 2.0f;
                            cd.strPerLevel = 0.0f; cd.vitPerLevel = 0.25f;
                            cd.intPerLevel = 0.0f; cd.dexPerLevel = 0.25f; cd.wisPerLevel = 0.5f;
                            break;
                    }
                    s.recalculateStats();
                    s.recalculateXPRequirement();
                    s.currentHP = s.maxHP;
                    s.currentMP = s.maxMP;
                    s.currentFury = 0.0f;
                }

                ImGui::DragInt("Level##cs", &s.level, 0.1f, 1, 70);
                if (ImGui::IsItemDeactivatedAfterEdit()) { s.recalculateStats(); s.recalculateXPRequirement(); s.currentHP = s.maxHP; s.currentMP = s.maxMP; }
                ImGui::DragInt("Current HP##cs", &s.currentHP, 1.0f, 0, 999999);
                ImGui::DragInt("Max HP##cs", &s.maxHP, 1.0f, 1, 999999);
                ImGui::DragInt("Current MP##cs", &s.currentMP, 1.0f, 0, 999999);
                ImGui::DragInt("Max MP##cs", &s.maxMP, 1.0f, 1, 999999);
                if (s.classDef.usesFury()) {
                    ImGui::DragFloat("Fury##cs", &s.currentFury, 0.1f, 0.0f, 20.0f);
                    ImGui::DragInt("Max Fury##cs", &s.maxFury, 0.1f, 1, 20);
                }
                ImGui::DragInt("Honor##cs", &s.honor, 1.0f, 0, 1000000);
                ImGui::Checkbox("Is Dead##cs", &s.isDead);
                const char* pkNames[] = {"White","Purple","Red","Black"};
                int pkIdx = (int)s.pkStatus;
                if (ImGui::Combo("PK Status##cs", &pkIdx, pkNames, 4)) s.pkStatus = (PKStatus)pkIdx;
                ImGui::Separator();
                ImGui::Text("Class: %s  Resource: %s", s.classDef.displayName.c_str(), s.classDef.usesFury() ? "Fury" : "Mana");
                ImGui::Text("Base: HP:%d MP:%d STR:%d VIT:%d INT:%d DEX:%d WIS:%d",
                    s.classDef.baseMaxHP, s.classDef.baseMaxMP, s.classDef.baseStrength,
                    s.classDef.baseVitality, s.classDef.baseIntelligence, s.classDef.baseDexterity, s.classDef.baseWisdom);
                ImGui::Text("Computed: STR:%d VIT:%d INT:%d DEX:%d WIS:%d", s.getStrength(), s.getVitality(), s.getIntelligence(), s.getDexterity(), s.getWisdom());
                ImGui::Text("Armor:%d MR:%d HR:%.1f Crit:%.0f%% Spd:%.2f", s.getArmor(), s.getMagicResist(), s.getHitRate(), s.getCritRate()*100, s.getSpeed());
                ImGui::Text("Atk Range: %.0f tiles  DmgMult: %.2f", s.classDef.attackRange, s.getDamageMultiplier());
                if (ImGui::Button("Recalc Stats##cs")) { s.recalculateStats(); s.recalculateXPRequirement(); }
                ImGui::SameLine();
                if (ImGui::Button("Full Heal##cs")) { s.currentHP = s.maxHP; s.currentMP = s.maxMP; s.isDead = false; }
            }
        }

        // Enemy Stats
        if (auto* es = selectedEntity_->getComponent<EnemyStatsComponent>()) {
            bool open = ImGui::CollapsingHeader("Enemy Stats");
            if (ImGui::BeginPopupContextItem("##rmEnemyStats")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<EnemyStatsComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<EnemyStatsComponent>()) {
                auto& s = es->stats;
                char eName[64]; strncpy(eName, s.enemyName.c_str(), sizeof(eName)-1); eName[sizeof(eName)-1]=0;
                if (ImGui::InputText("Name##es", eName, sizeof(eName))) s.enemyName = eName;
                char eType[32]; strncpy(eType, s.monsterType.c_str(), sizeof(eType)-1); eType[sizeof(eType)-1]=0;
                if (ImGui::InputText("Type##es", eType, sizeof(eType))) s.monsterType = eType;
                ImGui::DragInt("Level##es", &s.level, 0.1f, 1, 70);
                ImGui::DragInt("HP##es", &s.currentHP, 1.0f, 0, 999999);
                ImGui::DragInt("Max HP##es", &s.maxHP, 1.0f, 1, 999999);
                ImGui::DragInt("Base Damage##es", &s.baseDamage, 1.0f, 0, 99999);
                ImGui::DragInt("Armor##es", &s.armor, 1.0f, 0, 9999);
                ImGui::DragInt("Magic Resist##es", &s.magicResist, 1.0f, 0, 9999);
                ImGui::DragInt("Hit Rate##es", &s.mobHitRate, 0.5f, 0, 100);
                ImGui::DragFloat("Crit Rate##es", &s.critRate, 0.01f, 0.0f, 1.0f, "%.2f");
                ImGui::DragFloat("Attack Speed##es", &s.attackSpeed, 0.1f, 0.1f, 10.0f);
                ImGui::DragFloat("Move Speed##es", &s.moveSpeed, 0.1f, 0.1f, 10.0f);
                ImGui::DragInt("XP Reward##es", &s.xpReward, 1.0f, 0, 99999);
                ImGui::DragInt("Honor##es", &s.honorReward, 1.0f, 0, 1000);
                ImGui::Checkbox("Aggressive##es", &s.isAggressive);
                ImGui::SameLine();
                ImGui::Checkbox("Magic Dmg##es", &s.dealsMagicDamage);
                ImGui::Checkbox("Alive##es", &s.isAlive);
                ImGui::Text("Threat entries: %zu", s.damageByAttacker.size());
                if (!s.damageByAttacker.empty() && ImGui::Button("Clear Threat##es")) s.clearThreatTable();
            }
        }

        // Mob AI
        if (auto* ai = selectedEntity_->getComponent<MobAIComponent>()) {
            bool open = ImGui::CollapsingHeader("Mob AI");
            if (ImGui::BeginPopupContextItem("##rmMobAI")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<MobAIComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<MobAIComponent>()) {
                auto& a = ai->ai;
                const char* mN[] = {"Idle","Roam","Chase","ChaseMem","Attack","ReturnHome"};
                const char* dN[] = {"None","Up","Down","Left","Right"};
                ImGui::Text("Mode: %s  Facing: %s", mN[(int)a.getMode()], dN[(int)a.getFacingDirection()]);
                ImGui::DragFloat("Aggro Radius##ai", &a.acquireRadius, 1.0f, 0.0f, 1000.0f);
                ImGui::DragFloat("Leash Radius##ai", &a.contactRadius, 1.0f, 0.0f, 1000.0f);
                ImGui::DragFloat("Attack Range##ai", &a.attackRange, 1.0f, 0.0f, 500.0f);
                ImGui::DragFloat("Roam Radius##ai", &a.roamRadius, 1.0f, 0.0f, 500.0f);
                ImGui::DragFloat("Chase Speed##ai", &a.baseChaseSpeed, 1.0f, 0.0f, 500.0f);
                ImGui::DragFloat("Return Speed##ai", &a.baseReturnSpeed, 1.0f, 0.0f, 500.0f);
                ImGui::DragFloat("Roam Speed##ai", &a.baseRoamSpeed, 1.0f, 0.0f, 500.0f);
                ImGui::DragFloat("Attack CD##ai", &a.attackCooldown, 0.05f, 0.1f, 10.0f, "%.2fs");
                ImGui::DragFloat("Think Interval##ai", &a.serverTickInterval, 0.01f, 0.05f, 1.0f, "%.2fs");
                ImGui::Checkbox("Passive##ai", &a.isPassive);
                ImGui::Checkbox("Can Roam##ai", &a.canRoam);
                ImGui::SameLine();
                ImGui::Checkbox("Can Chase##ai", &a.canChase);
                ImGui::Checkbox("Roam While Idle##ai", &a.roamWhileIdle);
                ImGui::Checkbox("Show Aggro Radius##ai", &a.showAggroRadius);
            }
        }

        // Combat Controller
        if (auto* cc = selectedEntity_->getComponent<CombatControllerComponent>()) {
            bool open = ImGui::CollapsingHeader("Combat Controller");
            if (ImGui::BeginPopupContextItem("##rmCombat")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<CombatControllerComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<CombatControllerComponent>()) {
                ImGui::DragFloat("Base Cooldown##cc", &cc->baseAttackCooldown, 0.05f, 0.1f, 5.0f, "%.2fs");
                ImGui::Text("CD Remaining: %.2f", cc->attackCooldownRemaining);
            }
        }

        // Inventory
        if (auto* inv = selectedEntity_->getComponent<InventoryComponent>()) {
            bool open = ImGui::CollapsingHeader("Inventory");
            if (ImGui::BeginPopupContextItem("##rmInv")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<InventoryComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<InventoryComponent>()) {
                int64_t gold = inv->inventory.getGold();
                ImGui::Text("Gold: %lld  Slots: %d / %d", (long long)gold, inv->inventory.usedSlots(), inv->inventory.totalSlots());
            }
        }

        // Skill Manager
        if (auto* sk = selectedEntity_->getComponent<SkillManagerComponent>()) {
            bool open = ImGui::CollapsingHeader("Skill Manager");
            if (ImGui::BeginPopupContextItem("##rmSkill")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<SkillManagerComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<SkillManagerComponent>()) {
                ImGui::Text("Points: %d avail, %d earned, %d spent", sk->skills.availablePoints(), sk->skills.earnedPoints(), sk->skills.spentPoints());
            }
        }

        // Status Effects
        if (auto* se = selectedEntity_->getComponent<StatusEffectComponent>()) {
            bool open = ImGui::CollapsingHeader("Status Effects");
            if (ImGui::BeginPopupContextItem("##rmSE")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<StatusEffectComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<StatusEffectComponent>()) {
                ImGui::Text("Invuln:%s StunImm:%s Shield:%.0f", se->effects.isInvulnerable()?"Y":"N", se->effects.isStunImmune()?"Y":"N", se->effects.currentShield());
                ImGui::Text("DmgMult:%.2f Reduc:%.2f Spd:%.2f", se->effects.getDamageMultiplier(), se->effects.getDamageReduction(), se->effects.getSpeedModifier());
                if (ImGui::Button("Clear All Effects##se")) se->effects.removeAllEffects();
            }
        }

        // Crowd Control
        if (auto* ccC = selectedEntity_->getComponent<CrowdControlComponent>()) {
            bool open = ImGui::CollapsingHeader("Crowd Control");
            if (ImGui::BeginPopupContextItem("##rmCC")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<CrowdControlComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<CrowdControlComponent>()) {
                const char* ccN[] = {"None","Stunned","Frozen","Rooted","Taunted"};
                ImGui::Text("CC: %s  Time: %.1f", ccN[(int)ccC->cc.getCurrentCC()], ccC->cc.getRemainingTime());
                ImGui::Text("CanMove: %s  CanAct: %s", ccC->cc.canMove()?"Y":"N", ccC->cc.canAct()?"Y":"N");
                if (ccC->cc.getCurrentCC() != CCType::None && ImGui::Button("Clear CC##cc")) ccC->cc.endCC();
            }
        }

        // Nameplate
        if (auto* np = selectedEntity_->getComponent<NameplateComponent>()) {
            bool open = ImGui::CollapsingHeader("Nameplate");
            if (ImGui::BeginPopupContextItem("##rmNP")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<NameplateComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<NameplateComponent>()) {
                char npName[64]; strncpy(npName, np->displayName.c_str(), sizeof(npName)-1); npName[sizeof(npName)-1]=0;
                if (ImGui::InputText("Name##np", npName, sizeof(npName))) np->displayName = npName;
                ImGui::DragInt("Level##np", &np->displayLevel, 0.1f, 1, 70);
                ImGui::Checkbox("Show Level##np", &np->showLevel);
                ImGui::DragFloat("Font Size##np", &np->fontSize, 0.02f, 0.3f, 2.0f, "%.2f");
                ImGui::Checkbox("Visible##np", &np->visible);
            }
        }

        // Mob Nameplate
        if (auto* mnp = selectedEntity_->getComponent<MobNameplateComponent>()) {
            bool open = ImGui::CollapsingHeader("Mob Nameplate");
            if (ImGui::BeginPopupContextItem("##rmMNP")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<MobNameplateComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<MobNameplateComponent>()) {
                char mnName[64]; strncpy(mnName, mnp->displayName.c_str(), sizeof(mnName)-1); mnName[sizeof(mnName)-1]=0;
                if (ImGui::InputText("Name##mnp", mnName, sizeof(mnName))) mnp->displayName = mnName;
                ImGui::DragInt("Level##mnp", &mnp->level, 0.1f, 1, 70);
                ImGui::Checkbox("Boss##mnp", &mnp->isBoss);
                ImGui::SameLine();
                ImGui::Checkbox("Elite##mnp", &mnp->isElite);
                ImGui::Checkbox("Show Level##mnp", &mnp->showLevel);
                ImGui::DragFloat("Font Size##mnp", &mnp->fontSize, 0.02f, 0.3f, 2.0f, "%.2f");
                ImGui::Checkbox("Visible##mnp", &mnp->visible);
            }
        }

        // Targeting
        if (auto* tgt = selectedEntity_->getComponent<TargetingComponent>()) {
            bool open = ImGui::CollapsingHeader("Targeting");
            if (ImGui::BeginPopupContextItem("##rmTgt")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<TargetingComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<TargetingComponent>()) {
                ImGui::DragFloat("Max Range##tgt", &tgt->maxTargetRange, 0.5f, 1.0f, 50.0f);
                ImGui::Text("Target: %u", tgt->selectedTargetId);
                if (tgt->hasTarget() && ImGui::Button("Clear Target##tgt")) tgt->clearTarget();
            }
        }

        // Marker components with remove support
        if (selectedEntity_->hasComponent<DamageableComponent>()) {
            bool open = ImGui::CollapsingHeader("Damageable");
            if (ImGui::BeginPopupContextItem("##rmDmg")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<DamageableComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open) ImGui::Text("(marker - entity can take damage)");
        }

        // Social components (removable)
        if (selectedEntity_->hasComponent<PartyComponent>()) {
            ImGui::CollapsingHeader("Party Manager");
            if (ImGui::BeginPopupContextItem("##rmParty")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<PartyComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
        }
        if (selectedEntity_->hasComponent<GuildComponent>()) {
            ImGui::CollapsingHeader("Guild Manager");
            if (ImGui::BeginPopupContextItem("##rmGuild")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<GuildComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
        }
        if (selectedEntity_->hasComponent<ChatComponent>()) {
            ImGui::CollapsingHeader("Chat Manager");
            if (ImGui::BeginPopupContextItem("##rmChat")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<ChatComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
        }
        if (selectedEntity_->hasComponent<FriendsComponent>()) {
            ImGui::CollapsingHeader("Friends Manager");
            if (ImGui::BeginPopupContextItem("##rmFriends")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<FriendsComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
        }
        if (selectedEntity_->hasComponent<TradeComponent>()) {
            ImGui::CollapsingHeader("Trade Manager");
            if (ImGui::BeginPopupContextItem("##rmTrade")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<TradeComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
        }
        if (selectedEntity_->hasComponent<MarketComponent>()) {
            ImGui::CollapsingHeader("Market Manager");
            if (ImGui::BeginPopupContextItem("##rmMarket")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<MarketComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
        }

        // Spawn Zone
        if (auto* szComp = selectedEntity_->getComponent<SpawnZoneComponent>()) {
            bool open = ImGui::CollapsingHeader("Spawn Zone");
            if (ImGui::BeginPopupContextItem("##rmSpawnZone")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<SpawnZoneComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<SpawnZoneComponent>()) {
                auto& cfg = szComp->config;

                char znBuf[64]; strncpy(znBuf, cfg.zoneName.c_str(), sizeof(znBuf)-1); znBuf[sizeof(znBuf)-1]=0;
                if (ImGui::InputText("Zone Name##sz", znBuf, sizeof(znBuf))) cfg.zoneName = znBuf;

                ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1), "(Move zone via Transform position)");
                ImGui::DragFloat2("Zone Size##sz", &cfg.size.x, 4.0f, 32.0f, 5000.0f);
                ImGui::DragFloat("Min Spawn Dist##sz", &cfg.minSpawnDistance, 0.5f, 0.0f, 500.0f);
                ImGui::DragFloat("Tick Interval##sz", &cfg.serverTickInterval, 0.01f, 0.05f, 5.0f, "%.2fs");
                ImGui::Checkbox("Show Bounds##sz", &szComp->showBounds);

                ImGui::Text("Tracked mobs: %zu  Rules: %zu", szComp->trackedMobs.size(), cfg.rules.size());
                ImGui::Separator();

                int removeIdx = -1;
                for (int ri = 0; ri < (int)cfg.rules.size(); ++ri) {
                    auto& rule = cfg.rules[ri];
                    ImGui::PushID(ri);

                    char ruleLabel[64]; std::snprintf(ruleLabel, sizeof(ruleLabel), "Rule %d: %s", ri, rule.enemyId.c_str());
                    if (ImGui::TreeNode(ruleLabel)) {
                        char eidBuf[64]; strncpy(eidBuf, rule.enemyId.c_str(), sizeof(eidBuf)-1); eidBuf[sizeof(eidBuf)-1]=0;
                        if (ImGui::InputText("Enemy ID##r", eidBuf, sizeof(eidBuf))) rule.enemyId = eidBuf;

                        ImGui::DragInt("Target Count##r", &rule.targetCount, 0.1f, 0, 100);
                        ImGui::DragInt("Min Level##r", &rule.minLevel, 0.1f, 1, 70);
                        ImGui::DragInt("Max Level##r", &rule.maxLevel, 0.1f, 1, 70);
                        ImGui::DragInt("Base HP##r", &rule.baseHP, 1.0f, 1, 999999);
                        ImGui::DragInt("Base Damage##r", &rule.baseDamage, 1.0f, 0, 99999);
                        ImGui::DragFloat("Respawn Time##r", &rule.respawnSeconds, 0.5f, 1.0f, 600.0f, "%.1fs");
                        ImGui::Checkbox("Aggressive##r", &rule.isAggressive);
                        ImGui::SameLine();
                        ImGui::Checkbox("Boss##r", &rule.isBoss);

                        if (ImGui::Button("Remove Rule##r")) removeIdx = ri;

                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                }

                if (removeIdx >= 0 && removeIdx < (int)cfg.rules.size()) {
                    cfg.rules.erase(cfg.rules.begin() + removeIdx);
                }

                if (ImGui::Button("+ Add Rule##sz")) {
                    cfg.rules.push_back(MobSpawnRule{});
                }
            }
        }

        endInspectorComponents:;

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

            ImGui::Separator();
            if (!selectedEntity_->hasComponent<ZoneComponent>() && ImGui::MenuItem("Zone")) {
                auto* z = selectedEntity_->addComponent<ZoneComponent>();
                z->zoneName = "NewZone";
                z->displayName = "New Zone";
            }
            if (!selectedEntity_->hasComponent<PortalComponent>() && ImGui::MenuItem("Portal")) {
                selectedEntity_->addComponent<PortalComponent>();
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f,0.8f,1.0f,1.0f), "-- Game Systems --");
            if (!selectedEntity_->hasComponent<CharacterStatsComponent>() && ImGui::MenuItem("Character Stats"))
                selectedEntity_->addComponent<CharacterStatsComponent>();
            if (!selectedEntity_->hasComponent<EnemyStatsComponent>() && ImGui::MenuItem("Enemy Stats"))
                selectedEntity_->addComponent<EnemyStatsComponent>();
            if (!selectedEntity_->hasComponent<MobAIComponent>() && ImGui::MenuItem("Mob AI")) {
                auto* a = selectedEntity_->addComponent<MobAIComponent>();
                auto* t = selectedEntity_->getComponent<Transform>();
                if (t) a->ai.initialize(t->position);
            }
            if (!selectedEntity_->hasComponent<CombatControllerComponent>() && ImGui::MenuItem("Combat Controller"))
                selectedEntity_->addComponent<CombatControllerComponent>();
            if (!selectedEntity_->hasComponent<DamageableComponent>() && ImGui::MenuItem("Damageable"))
                selectedEntity_->addComponent<DamageableComponent>();
            if (!selectedEntity_->hasComponent<InventoryComponent>() && ImGui::MenuItem("Inventory"))
                selectedEntity_->addComponent<InventoryComponent>();
            if (!selectedEntity_->hasComponent<SkillManagerComponent>() && ImGui::MenuItem("Skill Manager"))
                selectedEntity_->addComponent<SkillManagerComponent>();
            if (!selectedEntity_->hasComponent<StatusEffectComponent>() && ImGui::MenuItem("Status Effects"))
                selectedEntity_->addComponent<StatusEffectComponent>();
            if (!selectedEntity_->hasComponent<CrowdControlComponent>() && ImGui::MenuItem("Crowd Control"))
                selectedEntity_->addComponent<CrowdControlComponent>();
            if (!selectedEntity_->hasComponent<TargetingComponent>() && ImGui::MenuItem("Targeting"))
                selectedEntity_->addComponent<TargetingComponent>();
            if (!selectedEntity_->hasComponent<NameplateComponent>() && ImGui::MenuItem("Nameplate"))
                selectedEntity_->addComponent<NameplateComponent>();
            if (!selectedEntity_->hasComponent<MobNameplateComponent>() && ImGui::MenuItem("Mob Nameplate"))
                selectedEntity_->addComponent<MobNameplateComponent>();
            if (!selectedEntity_->hasComponent<SpawnZoneComponent>() && ImGui::MenuItem("Spawn Zone"))
                selectedEntity_->addComponent<SpawnZoneComponent>();

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f,0.8f,1.0f,1.0f), "-- Social --");
            if (!selectedEntity_->hasComponent<ChatComponent>() && ImGui::MenuItem("Chat Manager"))
                selectedEntity_->addComponent<ChatComponent>();
            if (!selectedEntity_->hasComponent<PartyComponent>() && ImGui::MenuItem("Party Manager"))
                selectedEntity_->addComponent<PartyComponent>();
            if (!selectedEntity_->hasComponent<GuildComponent>() && ImGui::MenuItem("Guild Manager"))
                selectedEntity_->addComponent<GuildComponent>();
            if (!selectedEntity_->hasComponent<FriendsComponent>() && ImGui::MenuItem("Friends Manager"))
                selectedEntity_->addComponent<FriendsComponent>();
            if (!selectedEntity_->hasComponent<TradeComponent>() && ImGui::MenuItem("Trade Manager"))
                selectedEntity_->addComponent<TradeComponent>();
            if (!selectedEntity_->hasComponent<MarketComponent>() && ImGui::MenuItem("Market Manager"))
                selectedEntity_->addComponent<MarketComponent>();

            ImGui::EndPopup();
        }
    }
    ImGui::End();

    #undef INSPECTOR_ROW
}

// ============================================================================
// Erase Tool
// ============================================================================

void Editor::eraseTileAt(World* world, Camera* camera, const Vec2& screenPos,
                         int windowWidth, int windowHeight) {
    if (!world || !camera) return;

    Vec2 worldPos = camera->screenToWorld(screenPos, windowWidth, windowHeight);

    // Find the nearest ground tile and delete it
    Entity* nearest = nullptr;
    float nearestDist = 999999.0f;

    world->forEach<Transform, SpriteComponent>(
        [&](Entity* entity, Transform* t, SpriteComponent*) {
            if (entity->tag() != "ground") return;
            float dist = worldPos.distance(t->position);
            if (dist < gridSize_ * 0.6f && dist < nearestDist) {
                nearest = entity;
                nearestDist = dist;
            }
        }
    );

    if (nearest) {
        // Record for undo
        auto cmd = std::make_unique<DeleteCommand>();
        cmd->entityData = PrefabLibrary::entityToJson(nearest);
        cmd->deletedHandle = nearest->handle();
        UndoSystem::instance().push(std::move(cmd));

        world->destroyEntity(nearest->handle());
    }
}

// ============================================================================
// Console Command Panel
// ============================================================================

void Editor::drawConsole(World* world) {
    ImGui::SetNextWindowSize(ImVec2(400, 50), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);

    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Command", nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60);
    bool enter = ImGui::InputText("##cmd", consoleCmdBuf_, sizeof(consoleCmdBuf_),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Run") || enter) {
        if (consoleCmdBuf_[0] != '\0') {
            executeCommand(world, consoleCmdBuf_);
            consoleCmdBuf_[0] = '\0';
        }
    }

    ImGui::End();
}

void Editor::executeCommand(World* world, const std::string& cmd) {
    LOG_INFO("Console", "> %s", cmd.c_str());

    // Parse command
    std::istringstream iss(cmd);
    std::string token;
    std::vector<std::string> args;
    while (iss >> token) args.push_back(token);

    if (args.empty()) return;

    if (args[0] == "help") {
        LOG_INFO("Console", "Commands: list, count, find <name>, delete <id>, spawn <prefab> <x> <y>, set <id>.<prop> <val>, tp <x> <y>");
    }
    else if (args[0] == "list") {
        if (!world) return;
        // Group by name+tag to avoid flooding with 640 tiles
        std::unordered_map<std::string, int> counts;
        std::unordered_map<std::string, EntityId> firstId;
        world->forEachEntity([&](Entity* e) {
            std::string key = std::string(e->name()) + " (" + e->tag() + ")";
            counts[key]++;
            if (firstId.find(key) == firstId.end()) firstId[key] = e->id();
        });
        for (auto& [key, count] : counts) {
            if (count > 1)
                LOG_INFO("Console", "  %s x%d (first id=%u)", key.c_str(), count, firstId[key]);
            else
                LOG_INFO("Console", "  [%u] %s", firstId[key], key.c_str());
        }
    }
    else if (args[0] == "count") {
        if (world) LOG_INFO("Console", "Entities: %zu", world->entityCount());
    }
    else if (args[0] == "find" && args.size() > 1) {
        if (!world) return;
        world->forEachEntity([&](Entity* e) {
            if (e->name().find(args[1]) != std::string::npos ||
                e->tag().find(args[1]) != std::string::npos) {
                auto* t = e->getComponent<Transform>();
                if (t) LOG_INFO("Console", "  [%u] %s at (%.0f, %.0f)", e->id(), e->name().c_str(), t->position.x, t->position.y);
                else LOG_INFO("Console", "  [%u] %s", e->id(), e->name().c_str());
            }
        });
    }
    else if (args[0] == "delete" && args.size() > 1) {
        if (!world) return;
        EntityId id = (EntityId)std::stoul(args[1]);
        auto* e = world->getEntity(id);
        if (e) {
            LOG_INFO("Console", "Deleted entity %u (%s)", id, e->name().c_str());
            world->destroyEntity(id);
        } else {
            LOG_WARN("Console", "Entity %u not found", id);
        }
    }
    else if (args[0] == "spawn" && args.size() > 3) {
        if (!world) return;
        float x = std::stof(args[2]);
        float y = std::stof(args[3]);
        auto* e = PrefabLibrary::instance().spawn(args[1], *world, {x, y});
        if (e) LOG_INFO("Console", "Spawned '%s' at (%.0f, %.0f) id=%u", args[1].c_str(), x, y, e->id());
        else LOG_WARN("Console", "Prefab '%s' not found", args[1].c_str());
    }
    else if (args[0] == "tp" && args.size() > 2) {
        if (!world) return;
        float x = std::stof(args[1]);
        float y = std::stof(args[2]);
        world->forEach<Transform, PlayerController>(
            [&](Entity*, Transform* t, PlayerController* p) {
                if (p->isLocalPlayer) {
                    t->position = {x, y};
                    LOG_INFO("Console", "Teleported player to (%.0f, %.0f)", x, y);
                }
            }
        );
    }
    else {
        LOG_WARN("Console", "Unknown command: %s (type 'help')", args[0].c_str());
    }
}

// ============================================================================
// Keyboard Shortcuts
// ============================================================================

void Editor::handleKeyShortcuts(World* world, const SDL_Event& event) {
    if (!open_ || !world) return;

    auto scancode = event.key.keysym.scancode;
    bool ctrl = (event.key.keysym.mod & KMOD_CTRL) != 0;
    bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;

    // Ctrl+Z = Undo
    if (ctrl && scancode == SDL_SCANCODE_Z && !shift) {
        UndoSystem::instance().undo(world);
    }
    // Ctrl+Y or Ctrl+Shift+Z = Redo
    if ((ctrl && scancode == SDL_SCANCODE_Y) ||
        (ctrl && shift && scancode == SDL_SCANCODE_Z)) {
        UndoSystem::instance().redo(world);
    }
    // Ctrl+S = Save current scene
    if (ctrl && scancode == SDL_SCANCODE_S && !currentScenePath_.empty()) {
        saveScene(world, currentScenePath_);
    }
    // Ctrl+D = Duplicate
    if (ctrl && scancode == SDL_SCANCODE_D && selectedEntity_) {
        auto json = PrefabLibrary::entityToJson(selectedEntity_);
        Entity* copy = PrefabLibrary::jsonToEntity(json, *world);
        if (copy) {
            auto* t = copy->getComponent<Transform>();
            if (t) t->position += Vec2(32.0f, 0.0f);
            selectedEntity_ = copy;

            auto cmd = std::make_unique<CreateCommand>();
            cmd->entityData = PrefabLibrary::entityToJson(copy);
            cmd->createdHandle = copy->handle();
            UndoSystem::instance().push(std::move(cmd));
        }
    }
    // Ctrl+A = Select all
    if (ctrl && scancode == SDL_SCANCODE_A) {
        selectedEntities_.clear();
        world->forEachEntity([&](Entity* e) {
            selectedEntities_.insert(e->handle());
        });
        LOG_INFO("Editor", "Selected all (%zu entities)", selectedEntities_.size());
    }
    // Ctrl+C = Copy (store selection)
    if (ctrl && scancode == SDL_SCANCODE_C && selectedEntity_) {
        // Store in clipboard (just log for now, full clipboard later)
        LOG_INFO("Editor", "Copied entity '%s'", selectedEntity_->name().c_str());
    }
    // Delete = Delete selected
    if (scancode == SDL_SCANCODE_DELETE && selectedEntity_) {
        auto cmd = std::make_unique<DeleteCommand>();
        cmd->entityData = PrefabLibrary::entityToJson(selectedEntity_);
        cmd->deletedHandle = selectedEntity_->handle();
        UndoSystem::instance().push(std::move(cmd));

        world->destroyEntity(selectedEntity_->handle());
        selectedEntity_ = nullptr;
    }
    // W = Move tool
    if (scancode == SDL_SCANCODE_W && !ctrl) {
        currentTool_ = EditorTool::Move;
    }
    // E = Resize tool
    if (scancode == SDL_SCANCODE_E && !ctrl) {
        currentTool_ = EditorTool::Resize;
    }
    // B = Paint tool
    if (scancode == SDL_SCANCODE_B && !ctrl) {
        currentTool_ = EditorTool::Paint;
    }
    // X = Erase tool
    if (scancode == SDL_SCANCODE_X && !ctrl) {
        currentTool_ = EditorTool::Erase;
    }
}

} // namespace fate
