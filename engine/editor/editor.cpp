#include "engine/editor/editor.h"
#include "engine/editor/combat_text_editor.h"
#include "engine/ui/ui_safe_area.h"
#include "engine/core/logger.h"
#ifndef FATEMMO_METAL
// Editor uses direct GL for ImGui integration — intentionally outside RHI
#include "engine/render/gfx/backend/gl/gl_loader.h"
#endif
#include "engine/render/fullscreen_quad.h"
#include "engine/input/input.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_freetype.h"
#include "imgui_impl_sdl2.h"
#ifdef FATEMMO_METAL
#include <imgui_impl_metal.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#else
#include "imgui_impl_opengl3.h"
#endif

#ifdef FATE_HAS_GAME
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/player_controller.h"
#include "game/components/box_collider.h"
#include "game/components/polygon_collider.h"
#include "game/components/animator.h"
#include "game/components/zone_component.h"
#include "game/components/game_components.h"
#include "game/components/faction_component.h"
#include "game/components/pet_component.h"
#include "game/systems/spawn_system.h"
#endif // FATE_HAS_GAME
#include "engine/ecs/prefab.h"
#include "engine/scene/scene.h"
#include "engine/scene/scene_manager.h"
#include "engine/editor/undo.h"
#include "engine/editor/log_viewer.h"
#ifdef FATE_HAS_GAME
#include "engine/ui/ui_serializer.h"
#include "game/animation_loader.h"
#endif // FATE_HAS_GAME

#include "engine/ecs/component_meta.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {
    bool isValidAssetName(const char* name) {
        if (!name || name[0] == '\0') return false;
        for (const char* p = name; *p; ++p) {
            if (*p == '/' || *p == '\\') return false;
        }
        if (strstr(name, "..") != nullptr) return false;
        return true;
    }
} // anonymous namespace

namespace fate {

// ============================================================================
// Init / Shutdown
// ============================================================================

#ifdef FATEMMO_METAL
bool Editor::init(SDL_Window* window, void* metalLayer) {
#else
bool Editor::init(SDL_Window* window, SDL_GLContext glContext) {
#endif
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
#if defined(ENGINE_MEMORY_DEBUG)
    ImPlot::CreateContext();
#endif

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.FontGlobalScale = 1.0f;

    // Load Inter font family with FreeType hinting
    ImFontConfig fontCfg;
    fontCfg.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
    fontCfg.OversampleH = 1;
    fontCfg.OversampleV = 1;

    // Load Inter fonts if present, otherwise fall back to ImGui default.
    // AddFontFromFileTTF asserts on missing files, so check existence first.
    FILE* fontCheck = fopen("assets/fonts/Inter-Regular.ttf", "rb");
    if (fontCheck) {
        fclose(fontCheck);
        fontBody_ = io.Fonts->AddFontFromFileTTF("assets/fonts/Inter-Regular.ttf", 14.0f, &fontCfg);
        fontHeading_ = io.Fonts->AddFontFromFileTTF("assets/fonts/Inter-SemiBold.ttf", 16.0f, &fontCfg);
        fontSmall_ = io.Fonts->AddFontFromFileTTF("assets/fonts/Inter-Regular.ttf", 12.0f, &fontCfg);
    }
    if (!fontBody_) {
        LOG_WARN("Editor", "Inter fonts not found — using ImGui default");
        fontBody_ = io.Fonts->AddFontDefault();
        fontHeading_ = fontBody_;
        fontSmall_ = fontBody_;
    }

    io.Fonts->Build();

    // Wire font pointers to sub-editors
    animationEditor_.setFonts(fontHeading_, fontSmall_);
    assetBrowser_.setFonts(fontHeading_, fontSmall_);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    // Spacing — 8px grid, tight vertical, comfortable horizontal
    style.WindowPadding     = ImVec2(8, 8);
    style.FramePadding      = ImVec2(6, 4);
    style.CellPadding       = ImVec2(4, 3);
    style.ItemSpacing       = ImVec2(8, 4);
    style.ItemInnerSpacing  = ImVec2(4, 4);
    style.IndentSpacing     = 16.0f;
    style.ScrollbarSize     = 11.0f;
    style.GrabMinSize       = 8.0f;

    // Rounding — subtle modern softness
    style.WindowRounding    = 3.0f;
    style.ChildRounding     = 3.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 3.0f;

    // Borders — minimal, modern
    style.WindowBorderSize     = 1.0f;
    style.ChildBorderSize      = 0.0f;
    style.PopupBorderSize      = 1.0f;
    style.FrameBorderSize      = 0.0f;
    style.TabBorderSize        = 0.0f;
    style.DockingSeparatorSize = 2.0f;

    // Color scheme — layered dark backgrounds with blue accent
    ImVec4* c = style.Colors;

    // Background hierarchy (darkest -> lightest)
    c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.078f, 0.078f, 0.086f, 1.00f);
    c[ImGuiCol_WindowBg]             = ImVec4(0.118f, 0.118f, 0.133f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.118f, 0.118f, 0.133f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.188f, 0.188f, 0.212f, 0.96f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.165f, 0.165f, 0.180f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.200f, 0.200f, 0.220f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.235f, 0.235f, 0.259f, 1.00f);

    // Title bar & menu
    c[ImGuiCol_TitleBg]              = ImVec4(0.078f, 0.078f, 0.094f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.118f, 0.118f, 0.133f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.078f, 0.078f, 0.094f, 0.50f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.102f, 0.102f, 0.118f, 1.00f);

    // Tabs
    c[ImGuiCol_Tab]                  = ImVec4(0.094f, 0.094f, 0.110f, 1.00f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.200f, 0.220f, 0.259f, 1.00f);
    c[ImGuiCol_TabSelected]          = ImVec4(0.118f, 0.118f, 0.133f, 1.00f);
    c[ImGuiCol_TabSelectedOverline]  = ImVec4(0.290f, 0.541f, 0.859f, 1.00f);
    c[ImGuiCol_TabDimmed]            = ImVec4(0.078f, 0.078f, 0.102f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.102f, 0.102f, 0.125f, 1.00f);
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.290f, 0.541f, 0.859f, 0.50f);

    // Headers (CollapsingHeader, Selectable)
    c[ImGuiCol_Header]               = ImVec4(0.165f, 0.176f, 0.196f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.200f, 0.220f, 0.259f, 1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.227f, 0.251f, 0.314f, 1.00f);

    // Buttons
    c[ImGuiCol_Button]               = ImVec4(0.145f, 0.145f, 0.188f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.200f, 0.200f, 0.251f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.243f, 0.243f, 0.298f, 1.00f);

    // Text
    c[ImGuiCol_Text]                 = ImVec4(0.831f, 0.831f, 0.847f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.502f, 0.502f, 0.533f, 1.00f);
    c[ImGuiCol_TextSelectedBg]       = ImVec4(0.290f, 0.541f, 0.859f, 0.40f);

    // Borders & separators
    c[ImGuiCol_Border]               = ImVec4(0.165f, 0.165f, 0.188f, 1.00f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.165f, 0.165f, 0.188f, 1.00f);
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.290f, 0.541f, 0.859f, 0.60f);
    c[ImGuiCol_SeparatorActive]      = ImVec4(0.290f, 0.541f, 0.859f, 1.00f);

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.078f, 0.078f, 0.094f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.200f, 0.200f, 0.220f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.251f, 0.251f, 0.282f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.314f, 0.314f, 0.345f, 1.00f);

    // Accent-colored interactive elements
    c[ImGuiCol_CheckMark]            = ImVec4(0.290f, 0.541f, 0.859f, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.290f, 0.541f, 0.859f, 0.80f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.369f, 0.604f, 0.910f, 1.00f);

    // Resize grip
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.200f, 0.200f, 0.220f, 0.40f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.290f, 0.541f, 0.859f, 0.60f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0.290f, 0.541f, 0.859f, 0.90f);

    // Docking & nav
    c[ImGuiCol_DockingPreview]       = ImVec4(0.290f, 0.541f, 0.859f, 0.40f);
    c[ImGuiCol_NavHighlight]         = ImVec4(0.290f, 0.541f, 0.859f, 1.00f);

#ifdef FATEMMO_METAL
    ImGui_ImplSDL2_InitForMetal(window);
    CAMetalLayer* layer = (__bridge CAMetalLayer*)metalLayer;
    ImGui_ImplMetal_Init(layer.device);
#else
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330");
#endif

    SDL_SetWindowTitle(window, "FateMMO Engine | Editor");

    scanAssets();
    assetBrowser_.init(assetRoot_, sourceDir_);
    assetBrowser_.onOpenAnimation = [this](const std::string& path) {
        if (path.find(".png") != std::string::npos || path.find(".jpg") != std::string::npos)
            animationEditor_.openWithSheet(path);
        else
            animationEditor_.openFile(path);
    };

    dialogueEditor_.init();
    animationEditor_.init();

    LOG_INFO("Editor", "Editor initialized");
    return true;
}

void Editor::shutdown() {
    assetBrowser_.shutdown();
    // Release GPU textures before GL context is destroyed
    paletteTexture_.reset();
    for (auto& entry : assets_) entry.thumbnail.reset();
    dialogueEditor_.shutdown();
    viewportFbo_.destroy();
#if defined(ENGINE_MEMORY_DEBUG)
    ImPlot::DestroyContext();
#endif
#ifdef FATEMMO_METAL
    ImGui_ImplMetal_Shutdown();
#else
    ImGui_ImplOpenGL3_Shutdown();
#endif
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void Editor::processEvent(const SDL_Event& event) {
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void Editor::beginFrame() {
    frameStarted_ = false;

    // Capture previous frame's IO state BEFORE NewFrame() resets it.
    // ImGui's WantCaptureKeyboard/Mouse reflect which widgets had focus
    // last frame — reading after NewFrame() always returns false.
    ImGuiIO& io = ImGui::GetIO();
    wantsKeyboard_ = io.WantCaptureKeyboard;
    wantsMouse_ = io.WantCaptureMouse;

#ifdef FATEMMO_METAL
    ImGui_ImplMetal_NewFrame(nil);
#else
    ImGui_ImplOpenGL3_NewFrame();
#endif
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    frameStarted_ = true;
}

#ifdef FATE_HAS_GAME
// ============================================================================
// Render
// ============================================================================

void Editor::applyLayerVisibility(World* world) {
    if (!world) return;
    world->forEach<SpriteComponent, TileLayerComponent>(
        [&](Entity* entity, SpriteComponent* s, TileLayerComponent* tlc) {
            int idx = layerIndex(tlc->layer);
            s->enabled = showLayer_[idx];
        }
    );
}

void Editor::renderScene(SpriteBatch* batch, Camera* camera) {
    // Called while FBO is bound — draw in-viewport overlays via SpriteBatch
    if (!open_ || !batch || !camera) return;

    // Apply tile layer visibility toggles
    if (paused_ && dockWorld_) {
        applyLayerVisibility(dockWorld_);
    }

    if (showGrid_ && paused_) {
        // Prefer GPU grid shader; fall back to SpriteBatch grid if shader fails
        drawSceneGridShader(camera);
        if (!gridShaderLoaded_) {
            drawSceneGrid(batch, camera);
        }
    }

    // Draw selection outlines for selected entities
    if (paused_) {
        drawSelectionOutlines(batch, camera);
    }

    // Draw brush preview when in paint/erase mode
    if (paused_ && (currentTool_ == EditorTool::Paint || currentTool_ == EditorTool::Erase) && brushSize_ > 0) {
        ImVec2 imMouse = ImGui::GetMousePos();
        Vec2 mouseScreen = {imMouse.x - viewportPos_.x, imMouse.y - viewportPos_.y};
        Vec2 mouseWorld = camera->screenToWorld(mouseScreen, (int)viewportSize_.x, (int)viewportSize_.y);
        float half = gridSize_ * 0.5f;
        mouseWorld.x = std::floor(mouseWorld.x / gridSize_) * gridSize_ + half;
        mouseWorld.y = std::floor(mouseWorld.y / gridSize_) * gridSize_ + half;

        int bhalf = brushSize_ / 2;
        float totalSize = brushSize_ * gridSize_;
        Vec2 origin = {
            mouseWorld.x + (-bhalf) * gridSize_ - half,
            mouseWorld.y + (-bhalf) * gridSize_ - half
        };

        Color previewColor = (currentTool_ == EditorTool::Erase)
            ? Color(1.0f, 0.3f, 0.3f, 0.3f)
            : Color(0.3f, 1.0f, 0.3f, 0.3f);

        batch->drawRect(origin, {totalSize, totalSize}, previewColor);
    }
}

void Editor::drawSceneGridShader(Camera* camera) {
    // Lazy-load the grid shader (only attempt once)
    if (!gridShaderLoaded_ && !gridShaderAttempted_) {
        gridShaderAttempted_ = true;
        gridShaderLoaded_ = gridShader_.loadFromFile(
            "assets/shaders/fullscreen_quad.vert",
            "assets/shaders/grid.frag");
        if (!gridShaderLoaded_) {
            LOG_ERROR("Editor", "Failed to load grid shader — falling back to SpriteBatch grid");
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

void Editor::renderUI(World* world, Camera* camera, SpriteBatch* batch, FrameArena* frameArena) {
    if (!frameStarted_) return;

    // ImGuizmo requires BeginFrame() once per ImGui frame
    ImGuizmo::BeginFrame();

    dockWorld_ = world;
    dockCamera_ = camera;
    refreshSelection(world);
    drawDockSpace();
    drawMenuBar(world);
    drawSceneViewport();
    // drawViewportHUD removed — coordinates now shown by FateStatusBar in the game HUD
    drawHierarchy(world);
    drawInspector();
    drawConsole(world);
    if (showCombatTextEditor_) {
        drawCombatTextEditorWindow(&showCombatTextEditor_);
    }
    LogViewer::instance().draw();
    drawTilePalette(world, camera);
    drawAssetBrowser(world, camera);
    drawDebugInfoPanel(world);

#if defined(ENGINE_MEMORY_DEBUG)
    if (showMemoryPanel_) {
        drawMemoryPanel(&showMemoryPanel_, frameArena);
    }
#endif

    if (showDemoWindow_) {
        ImGui::ShowDemoWindow(&showDemoWindow_);
    }

    dialogueEditor_.draw();
    animationEditor_.draw();
    paperDollPanel_.draw();
    contentBrowserPanel_.draw();

    // UI editor panels (hierarchy tree + inspector)
    if (uiManager_) {
        uiEditorPanel_.draw(*uiManager_);
    }

    // Draw selection outline around selected UI widget in the viewport
    if (uiManager_) {
        auto* selNode = uiEditorPanel_.selectedNode();
        if (selNode && selNode->visible()) {
            const Rect& r = selNode->computedRect();
            float vpX = viewportPos_.x;
            float vpY = viewportPos_.y;
            // Scale from FBO resolution to displayed viewport size
            float fboW = (float)viewportFbo_.width();
            float fboH = (float)viewportFbo_.height();
            float sx = (fboW > 0) ? viewportSize_.x / fboW : 1.0f;
            float sy = (fboH > 0) ? viewportSize_.y / fboH : 1.0f;
            ImVec2 tl(vpX + r.x * sx, vpY + r.y * sy);
            ImVec2 br(vpX + (r.x + r.w) * sx, vpY + (r.y + r.h) * sy);
            auto* dl = ImGui::GetForegroundDrawList();
            dl->AddRect(tl, br, IM_COL32(0, 255, 200, 200), 0.0f, 0, 2.0f);
            // Corner handles (small squares at corners)
            float hs = 4.0f;
            ImU32 handleCol = IM_COL32(255, 255, 255, 220);
            dl->AddRectFilled(ImVec2(tl.x - hs, tl.y - hs), ImVec2(tl.x + hs, tl.y + hs), handleCol);
            dl->AddRectFilled(ImVec2(br.x - hs, tl.y - hs), ImVec2(br.x + hs, tl.y + hs), handleCol);
            dl->AddRectFilled(ImVec2(tl.x - hs, br.y - hs), ImVec2(tl.x + hs, br.y + hs), handleCol);
            dl->AddRectFilled(ImVec2(br.x - hs, br.y - hs), ImVec2(br.x + hs, br.y + hs), handleCol);
        }
    }

    // Post-process config panel
    if (showPostProcessPanel_ && postProcessConfig_) {
        ImGui::SetNextWindowSize(ImVec2(280, 320), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Post Process", &showPostProcessPanel_)) {
            ImGui::Text("Post-processing settings");
            ImGui::Separator();

            ImGui::Checkbox("Bloom Enabled", &postProcessConfig_->bloomEnabled);
            ImGui::DragFloat("Bloom Threshold", &postProcessConfig_->bloomThreshold, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Bloom Strength", &postProcessConfig_->bloomStrength, 0.01f, 0.0f, 4.0f);

            ImGui::Separator();
            ImGui::Checkbox("Vignette Enabled", &postProcessConfig_->vignetteEnabled);
            ImGui::DragFloat("Vignette Radius", &postProcessConfig_->vignetteRadius, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Vignette Smoothness", &postProcessConfig_->vignetteSmoothness, 0.01f, 0.0f, 2.0f);

            ImGui::Separator();
            float tint[3] = {postProcessConfig_->colorTint.r, postProcessConfig_->colorTint.g, postProcessConfig_->colorTint.b};
            if (ImGui::ColorEdit3("Color Tint", tint)) {
                postProcessConfig_->colorTint.r = tint[0];
                postProcessConfig_->colorTint.g = tint[1];
                postProcessConfig_->colorTint.b = tint[2];
            }
            ImGui::DragFloat("Brightness", &postProcessConfig_->brightness, 0.01f, 0.0f, 3.0f);
            ImGui::DragFloat("Contrast", &postProcessConfig_->contrast, 0.01f, 0.0f, 3.0f);
        }
        ImGui::End();
    }

    ImGui::Render();
#ifndef FATEMMO_METAL
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
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
            if (ImGui::MenuItem("New Scene", nullptr, false, !inPlayMode_)) {
                if (dockWorld_) {
                    dockWorld_->forEachEntity([&](Entity* e) {
                        dockWorld_->destroyEntity(e->handle());
                    });
                    dockWorld_->processDestroyQueue();
                    selectedEntity_ = nullptr;
                    selectedHandle_ = {};
                    currentScenePath_.clear();
                    LOG_INFO("Editor", "New scene");
                }
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Open Scene", !inPlayMode_)) {
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
            // Save — enabled when a scene path is set (from Open Scene or async load)
            if (ImGui::MenuItem("Save", "Ctrl+S", false, !inPlayMode_ && !currentScenePath_.empty())) {
                saveScene(dockWorld_, currentScenePath_);
            }
            // Save As — always prompts for a new name
            if (ImGui::BeginMenu("Save As...", !inPlayMode_)) {
                static char saveNameBuf[64] = "WhisperingWoods";
                ImGui::InputText("Name", saveNameBuf, sizeof(saveNameBuf));
                if (ImGui::Button("Save")) {
                    if (isValidAssetName(saveNameBuf)) {
                        std::string path = std::string("assets/scenes/") + saveNameBuf + ".json";
                        saveScene(dockWorld_, path);
                        ImGui::CloseCurrentPopup();
                    } else {
                        LOG_WARN("Editor", "Invalid scene name: %s", saveNameBuf);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            bool canUndo = UndoSystem::instance().canUndo();
            bool canRedo = UndoSystem::instance().canRedo();
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo)) { UndoSystem::instance().undo(dockWorld_); refreshSelection(dockWorld_); }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo)) { UndoSystem::instance().redo(dockWorld_); refreshSelection(dockWorld_); }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Grid Snap", nullptr, &gridSnap_);
            ImGui::DragFloat("Grid Size", &gridSize_, 1.0f, 8.0f, 128.0f);
            ImGui::Separator();
            ImGui::MenuItem("Show Grid", nullptr, &showGrid_);
            ImGui::MenuItem("Show Colliders", nullptr, &showCollisionDebug_);
            ImGui::Separator();
            ImGui::MenuItem("Post Process", nullptr, &showPostProcessPanel_);
            ImGui::Separator();
            ImGui::MenuItem("UI Hierarchy", nullptr, &uiEditorPanel_.showHierarchy);
            ImGui::MenuItem("UI Inspector", nullptr, &uiEditorPanel_.showInspector);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) { resetLayout_ = true; }
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &showDemoWindow_);
#if defined(ENGINE_MEMORY_DEBUG)
            ImGui::MenuItem("Memory", nullptr, &showMemoryPanel_);
#endif
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Entity")) {
            if (ImGui::MenuItem("Create Empty")) {
                if (dockWorld_) {
                    auto* e = dockWorld_->createEntity("New Entity");
                    e->addComponent<Transform>();
                    selectedEntity_ = e;
                    selectedHandle_ = e ? e->handle() : EntityHandle{};
                }
            }
            if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D", false, selectedEntity_ != nullptr)) {
                if (dockWorld_ && selectedEntity_) {
                    auto json = PrefabLibrary::entityToJson(selectedEntity_);
                    Entity* copy = PrefabLibrary::jsonToEntity(json, *dockWorld_);
                    auto* t = copy->getComponent<Transform>();
                    if (t) t->position += Vec2(32.0f, 0.0f);
                    selectedEntity_ = copy;
                    selectedHandle_ = copy ? copy->handle() : EntityHandle{};
                }
            }
            if (ImGui::MenuItem("Save as Prefab", nullptr, false, selectedEntity_ != nullptr)) {
                openSavePrefab_ = true;
            }
            if (ImGui::MenuItem("Delete Selected", "Delete", false, selectedEntity_ != nullptr && !isEntityLocked(selectedEntity_))) {
                if (dockWorld_ && selectedEntity_) {
                    dockWorld_->destroyEntity(selectedEntity_->handle());
                    selectedEntity_ = nullptr;
                    selectedHandle_ = {};
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
            bool dlgOpen = dialogueEditor_.isOpen();
            if (ImGui::MenuItem("Dialogue Editor", nullptr, &dlgOpen)) {
                dialogueEditor_.setOpen(dlgOpen);
            }
            bool animOpen = animationEditor_.isOpen();
            if (ImGui::MenuItem("Animation Editor", nullptr, &animOpen)) {
                animationEditor_.setOpen(animOpen);
            }
            ImGui::MenuItem("Combat Text Editor", nullptr, &showCombatTextEditor_);
            bool pdOpen = paperDollPanel_.isOpen();
            if (ImGui::MenuItem("Paper Doll Manager", nullptr, &pdOpen)) {
                paperDollPanel_.setOpen(pdOpen);
            }
            bool cbOpen = contentBrowserPanel_.isOpen();
            if (ImGui::MenuItem("Content Browser", nullptr, &cbOpen)) {
                contentBrowserPanel_.setOpen(cbOpen);
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
        ImGui::DockBuilderDockWindow("Network", dockBottom);
        ImGui::DockBuilderDockWindow("Animation Editor", dockBottom);

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
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.145f, 0.145f, 0.157f, 1.00f));

            float toolbarHeight = ImGui::GetFrameHeight() + 6.0f;
            ImGui::BeginChild("##ViewportToolbar", ImVec2(0, toolbarHeight), false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            float btnH = ImGui::GetFrameHeight();
            float btnSq = btnH; // square buttons

            // Left side: tool buttons
            auto toolBtn = [&](const char* label, EditorTool tool) {
                bool active = (currentTool_ == tool);
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.290f, 0.541f, 0.859f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.369f, 0.604f, 0.910f, 1.00f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.502f, 0.502f, 0.533f, 1.0f));
                }
                if (ImGui::Button(label, ImVec2(0, btnH))) currentTool_ = tool;
                if (active) ImGui::PopStyleColor(2);
                else ImGui::PopStyleColor(3);
                ImGui::SameLine();
            };
            toolBtn("Move", EditorTool::Move);
            toolBtn("Scale", EditorTool::Scale);
            toolBtn("Rotate", EditorTool::Rotate);
            toolBtn("Paint", EditorTool::Paint);
            toolBtn("Erase", EditorTool::Erase);
            toolBtn("Fill", EditorTool::Fill);
            toolBtn("Rect", EditorTool::RectFill);
            toolBtn("Line", EditorTool::LineTool);

            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            // Toggle buttons
            auto toggleBtn = [&](const char* label, bool* val) {
                bool wasActive = *val;
                if (wasActive) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.290f, 0.541f, 0.859f, 0.50f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.290f, 0.541f, 0.859f, 0.70f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.502f, 0.502f, 0.533f, 1.0f));
                }
                if (ImGui::Button(label, ImVec2(0, btnH))) *val = !(*val);
                if (wasActive) ImGui::PopStyleColor(2);
                else ImGui::PopStyleColor(3);
                ImGui::SameLine();
            };
            toggleBtn("Grid", &showGrid_);
            toggleBtn("Snap", &gridSnap_);
            toggleBtn("Colliders", &showCollisionDebug_);

            // Ground tile lock toggle (inverted: button shows locked state)
            {
                bool locked = groundLocked_;
                if (locked) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 0.60f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.3f, 0.2f, 0.80f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.502f, 0.502f, 0.533f, 1.0f));
                }
                if (ImGui::Button(locked ? "Locked" : "Unlocked", ImVec2(0, btnH)))
                    groundLocked_ = !groundLocked_;
                if (locked) ImGui::PopStyleColor(2);
                else ImGui::PopStyleColor(3);
                ImGui::SameLine();
            }

            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            // Play/Stop (with ECS snapshot/restore)
            {
                float playBtnW = 50.0f;
                if (!inPlayMode_) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.50f, 0.20f, 1.00f));
                    if (ImGui::Button("Play", ImVec2(playBtnW, btnH))) {
                        // Save editor camera state, reset to default zoom for play
                        if (dockCamera_) {
                            savedCamPos_ = dockCamera_->position();
                            savedCamZoom_ = dockCamera_->zoom();
                            dockCamera_->setZoom(1.0f);
                        }
                        enterPlayMode(dockWorld_);
                    }
                    ImGui::PopStyleColor();
                } else {
                    // Pause/Resume toggle
                    if (paused_) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.50f, 0.20f, 1.00f));
                        if (ImGui::Button("Resume", ImVec2(playBtnW, btnH))) {
                            // Restore gameplay camera (discard editor zoom/pan from pause)
                            if (dockCamera_) {
                                dockCamera_->setPosition(pausedCamPos_);
                                dockCamera_->setZoom(pausedCamZoom_);
                            }
                            paused_ = false;
                        }
                        ImGui::PopStyleColor();
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.50f, 0.20f, 1.00f));
                        if (ImGui::Button("Pause", ImVec2(playBtnW, btnH))) {
                            // Save gameplay camera before editor takes over
                            if (dockCamera_) {
                                pausedCamPos_ = dockCamera_->position();
                                pausedCamZoom_ = dockCamera_->zoom();
                            }
                            paused_ = true;
                        }
                        ImGui::PopStyleColor();
                    }

                    ImGui::SameLine();

                    // Stop — destroy runtime state, restore scene from snapshot
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.20f, 0.20f, 1.00f));
                    if (ImGui::Button("Stop", ImVec2(playBtnW, btnH))) {
                        // Restore editor camera state
                        if (dockCamera_) {
                            dockCamera_->setPosition(savedCamPos_);
                            dockCamera_->setZoom(savedCamZoom_);
                        }
                        exitPlayMode(dockWorld_);
                    }
                    ImGui::PopStyleColor();
                }
            }

            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            // Device resolution dropdown (categorized)
            {
                const auto& dev = kDeviceProfiles[displayPresetIdx_];
                char label[96];
                if (dev.width == 0)
                    snprintf(label, sizeof(label), "%s", dev.name);
                else
                    snprintf(label, sizeof(label), "%s (%dx%d)", dev.name, dev.width, dev.height);

                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::BeginCombo("##Device", label, ImGuiComboFlags_HeightLarge)) {
                    const char* lastCategory = nullptr;
                    for (int i = 0; i < kDeviceProfileCount; i++) {
                        const auto& d = kDeviceProfiles[i];
                        // Category separator
                        if (lastCategory == nullptr || std::strcmp(lastCategory, d.category) != 0) {
                            if (lastCategory != nullptr) ImGui::Separator();
                            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", d.category);
                            lastCategory = d.category;
                        }
                        char itemLabel[96];
                        if (d.width == 0)
                            snprintf(itemLabel, sizeof(itemLabel), "  %s", d.name);
                        else
                            snprintf(itemLabel, sizeof(itemLabel), "  %s  %dx%d", d.name, d.width, d.height);

                        if (ImGui::Selectable(itemLabel, i == displayPresetIdx_))
                            displayPresetIdx_ = i;
                    }
                    ImGui::EndCombo();
                }

                // Update simulated safe area when device changes
                {
                    const auto& selected = kDeviceProfiles[displayPresetIdx_];
                    fate::SafeAreaInsets insets;
                    insets.top    = selected.safeTop;
                    insets.bottom = selected.safeBottom;
                    insets.left   = selected.safeLeft;
                    insets.right  = selected.safeRight;
                    fate::setSimulatedSafeArea(insets);
                }

                ImGui::SameLine();
                ImGui::Checkbox("Safe Area", &showSafeAreaOverlay_);
            }

            ImGui::SameLine();

            // Right-aligned: FPS stats in muted gray
            {
                ImGuiIO& io = ImGui::GetIO();
                char stats[64];
                snprintf(stats, sizeof(stats), "%.0f FPS | %zu ent",
                         io.Framerate, dockWorld_ ? dockWorld_->entityCount() : 0u);
                if (fontSmall_) ImGui::PushFont(fontSmall_);
                float textW = ImGui::CalcTextSize(stats).x;
                float regionW = ImGui::GetContentRegionAvail().x;
                if (regionW > textW + 4.0f) {
                    ImGui::SameLine(ImGui::GetCursorPosX() + regionW - textW);
                    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.50f, 1.0f), "%s", stats);
                }
                if (fontSmall_) ImGui::PopFont();
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

        const auto& preset = kDeviceProfiles[displayPresetIdx_];
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

                // Safe area overlay: draw hardware cutout shapes + safe area boundary lines
                if (!paused_ && showSafeAreaOverlay_) {
                    const auto& selDev = kDeviceProfiles[displayPresetIdx_];
                    if (selDev.safeLeft > 0 || selDev.safeBottom > 0 ||
                        selDev.hasNotch || selDev.hasDynamicIsland) {
                        float sf = selDev.scaleFactor;
                        float imgX = viewportPos_.x;
                        float imgY = viewportPos_.y;
                        float imgW = viewportSize_.x;
                        float imgH = viewportSize_.y;
                        // Scale: logical points -> display pixels in viewport
                        float scX = (selDev.width > 0) ? imgW / static_cast<float>(selDev.width) * sf : 0.0f;
                        float scY = (selDev.height > 0) ? imgH / static_cast<float>(selDev.height) * sf : 0.0f;
                        auto* dl = ImGui::GetWindowDrawList();
                        ImU32 fillCol = IM_COL32(20, 20, 20, 200);
                        ImU32 lineCol = IM_COL32(255, 80, 80, 120);

                        // Dynamic Island (landscape: pill on left side, centered vertically)
                        // Physical: ~126pt wide x 36pt tall in portrait -> 36pt wide x 126pt tall in landscape
                        if (selDev.hasDynamicIsland) {
                            float pillW = 36.0f * scX;   // width in landscape (was height in portrait)
                            float pillH = 126.0f * scY;  // height in landscape (was width in portrait)
                            float pillX = imgX + 11.0f * scX; // ~11pt inset from screen edge
                            float pillY = imgY + imgH * 0.5f - pillH * 0.5f; // centered vertically
                            float rounding = pillH * 0.15f;
                            dl->AddRectFilled({pillX, pillY}, {pillX + pillW, pillY + pillH},
                                              fillCol, rounding);
                        }

                        // Notch (landscape: wider cutout on left side, centered vertically)
                        // Physical notch: ~209pt wide x ~30pt deep in portrait -> 30pt wide x 209pt tall in landscape
                        if (selDev.hasNotch && !selDev.hasDynamicIsland) {
                            float notchW = 30.0f * scX;
                            float notchH = 209.0f * scY;
                            float notchX = imgX + 11.0f * scX;
                            float notchY = imgY + imgH * 0.5f - notchH * 0.5f;
                            float rounding = notchW * 0.3f;
                            dl->AddRectFilled({notchX, notchY}, {notchX + notchW, notchY + notchH},
                                              fillCol, rounding);
                        }

                        // Home indicator (landscape: thin bar at bottom center)
                        // Physical: ~134pt wide x 5pt tall
                        if (selDev.safeBottom > 0) {
                            float barW = 134.0f * scX;
                            float barH = 5.0f * scY;
                            float barX = imgX + imgW * 0.5f - barW * 0.5f;
                            float barY = imgY + imgH - barH - 8.0f * scY; // 8pt from bottom edge
                            float rounding = barH * 0.5f;
                            dl->AddRectFilled({barX, barY}, {barX + barW, barY + barH},
                                              IM_COL32(200, 200, 200, 180), rounding);
                        }

                        // Safe area boundary lines (dashed feel via thin colored lines)
                        float safeLeft   = selDev.safeLeft * scX;
                        float safeBottom = selDev.safeBottom * scY;
                        if (safeLeft > 0) {
                            float lx = imgX + safeLeft;
                            dl->AddLine({lx, imgY}, {lx, imgY + imgH}, lineCol, 1.0f);
                        }
                        if (safeBottom > 0) {
                            float ly = imgY + imgH - safeBottom;
                            dl->AddLine({imgX, ly}, {imgX + imgW, ly}, lineCol, 1.0f);
                        }
                    }
                }

                // ImGuizmo: draw transform gizmo over the selected entity
                if (paused_ && selectedEntity_ && dockCamera_ &&
                    (currentTool_ == EditorTool::Move ||
                     currentTool_ == EditorTool::Scale ||
                     currentTool_ == EditorTool::Rotate)) {
                    drawImGuizmo(dockCamera_);
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

// drawViewportHUD removed — coordinates now shown by FateStatusBar in the game HUD

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
                                 currentTool_ == EditorTool::Scale ? "Scale" :
                                 currentTool_ == EditorTool::Rotate ? "Rotate" :
                                 currentTool_ == EditorTool::Paint ? "Paint" :
                                 currentTool_ == EditorTool::Erase ? "Erase" :
                                 currentTool_ == EditorTool::Fill ? "Fill" :
                                 currentTool_ == EditorTool::RectFill ? "RectFill" :
                                 currentTool_ == EditorTool::LineTool ? "Line" : "?");
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
            selectedHandle_ = entity->handle();
            LOG_INFO("Editor", "Placed at (%.0f, %.0f)", placePos.x, placePos.y);

            // Record undo for asset placement
            auto cmd = std::make_unique<CreateCommand>();
            cmd->entityData = PrefabLibrary::entityToJson(entity);
            cmd->createdHandle = entity->handle();
            UndoSystem::instance().push(std::move(cmd));
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
        } else if (s && t) {
            hw = s->size.x * t->scale.x * 0.5f + 2.0f;
            hh = s->size.y * t->scale.y * 0.5f + 2.0f;
        } else {
            hw = hh = 0.0f;
        }

        if (t && (s || szComp)) {
            // Only check resize handles when Scale tool is active (E key).
            // This prevents accidental resizing when trying to move tiles.
            bool allowResize = (currentTool_ == EditorTool::Scale);
            float handleZone = 6.0f / camera->zoom(); // tighter hit zone

            if (allowResize && !isEntityLocked(selectedEntity_)) {
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
                    if (worldPos.distance(handles[i]) < handleZone) {
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
                isDraggingEntity_ = !isEntityLocked(selectedEntity_);
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
        selectedHandle_ = best->handle();
        isDraggingEntity_ = !isEntityLocked(best);
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
        selectedHandle_ = {};
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

    // Only grid-snap ground tiles; other entities move freely.
    // Detect the tile's grid offset from its original position (some scenes
    // use center-aligned tiles at +16, others use corner-aligned at +0).
    if (gridSnap_ && selectedEntity_->tag() == "ground") {
        float offX = std::fmod(dragStartEntityPos_.x, gridSize_);
        float offY = std::fmod(dragStartEntityPos_.y, gridSize_);
        if (offX < 0) offX += gridSize_;
        if (offY < 0) offY += gridSize_;
        newPos.x = std::round((newPos.x - offX) / gridSize_) * gridSize_ + offX;
        newPos.y = std::round((newPos.y - offY) / gridSize_) * gridSize_ + offY;
    }

    t->position = newPos;
}

// Forward declarations for tile tool helpers (defined below paintTileAt)
static std::unique_ptr<UndoCommand> paintOneTile(World* world,
    const Vec2& worldPos, int tileIndex, int paletteCols,
    int paletteTileSize, float gridSize,
    const std::shared_ptr<Texture>& paletteTexture,
    const std::string& paletteTexturePath,
    const std::string& tileLayer);
static Vec2 tileToWorldCenter(int col, int row, float gridSize);

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

    // Finalize RectFill / LineTool drag
    if (isToolDragging_ && dockWorld_ &&
        (currentTool_ == EditorTool::RectFill || currentTool_ == EditorTool::LineTool) &&
        selectedTileIndex_ >= 0 && paletteTexture_) {

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
                auto cmd = paintOneTile(dockWorld_, wp, selectedTileIndex_, paletteColumns_,
                                        paletteTileSize_, gridSize_, paletteTexture_, paletteTexturePath_,
                                        selectedTileLayer_);
                if (cmd) compound->commands.push_back(std::move(cmd));
            }
            if (!compound->empty()) UndoSystem::instance().push(std::move(compound));
        }
    }
    isToolDragging_ = false;
    toolDragStart_ = {-1, -1};
    toolDragEnd_ = {-1, -1};

    // Flush pending brush stroke compound
    if (pendingBrushStroke_ && !pendingBrushStroke_->empty()) {
        pendingBrushStroke_->desc = "Paint (" +
            std::to_string(pendingBrushStroke_->commands.size()) + " tiles)";
        UndoSystem::instance().push(std::move(pendingBrushStroke_));
    }
    pendingBrushStroke_.reset();
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
        // Toggle between enhanced and legacy browser
        ImGui::Checkbox("Enhanced", &useEnhancedBrowser_);
        ImGui::SameLine();

        if (useEnhancedBrowser_) {
            // Sync drag state from enhanced browser back to editor
            if (assetBrowser_.isDraggingAsset()) {
                isDraggingAsset_ = true;
                draggedAssetPath_ = assetBrowser_.draggedAssetPath();
            }
            assetBrowser_.draw(world, camera);
            ImGui::End();
            return;
        }

        // --- Legacy browser below ---
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
                                if (ImGui::MenuItem("Open in Animation Editor")) {
                                    animationEditor_.openWithSheet(asset.fullPath);
                                }
                            }
                            if (asset.type == AssetType::Script || asset.type == AssetType::Shader) {
                                if (ImGui::MenuItem("Open in VS Code")) {
#ifdef _WIN32
                                    int wlen = MultiByteToWideChar(CP_UTF8, 0, asset.fullPath.c_str(), -1, nullptr, 0);
                                    std::wstring wpath(wlen, L'\0');
                                    MultiByteToWideChar(CP_UTF8, 0, asset.fullPath.c_str(), -1, wpath.data(), wlen);
                                    ShellExecuteW(nullptr, L"open", L"code", wpath.c_str(), nullptr, SW_SHOWNORMAL);
#endif
                                }
                            }
                            if (ImGui::MenuItem("Show in Explorer")) {
#ifdef _WIN32
                                std::string dir = asset.fullPath;
                                size_t lastSlash = dir.find_last_of("/\\");
                                if (lastSlash != std::string::npos) dir = dir.substr(0, lastSlash);
                                for (auto& c : dir) if (c == '/') c = '\\';
                                int wlen = MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0);
                                std::wstring wdir(wlen, L'\0');
                                MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, wdir.data(), wlen);
                                ShellExecuteW(nullptr, L"open", L"explorer", wdir.c_str(), nullptr, SW_SHOWNORMAL);
#endif
                            }
                            if (ImGui::MenuItem("Copy Path")) {
                                SDL_SetClipboardText(asset.fullPath.c_str());
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Delete File")) {
                                pendingDeleteFile_ = true;
                                pendingDeletePath_ = asset.fullPath;
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
                            pendingDeletePrefab_ = true;
                            pendingDeletePrefabName_ = pname;
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

                if (pendingDeletePrefab_) {
                    ImGui::OpenPopup("Delete Prefab?");
                    pendingDeletePrefab_ = false;
                }
                if (ImGui::BeginPopupModal("Delete Prefab?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Delete prefab '%s'?", pendingDeletePrefabName_.c_str());
                    ImGui::Separator();
                    if (ImGui::Button("Delete", ImVec2(120, 0))) {
                        std::string path = "assets/prefabs/" + pendingDeletePrefabName_ + ".json";
                        if (fs::exists(path)) fs::remove(path);
                        PrefabLibrary::instance().loadAll();
                        LOG_INFO("Editor", "Deleted prefab: %s", pendingDeletePrefabName_.c_str());
                        pendingDeletePrefabName_.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        pendingDeletePrefabName_.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
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

    if (pendingDeleteFile_) {
        ImGui::OpenPopup("Delete File?");
        pendingDeleteFile_ = false;
    }
    if (ImGui::BeginPopupModal("Delete File?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete this file?");
        ImGui::TextWrapped("%s", pendingDeletePath_.c_str());
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            if (fs::exists(pendingDeletePath_)) {
                fs::remove(pendingDeletePath_);
                LOG_INFO("Editor", "Deleted: %s", pendingDeletePath_.c_str());
                scanAssets();
            }
            pendingDeletePath_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            pendingDeletePath_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
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

// Paint a single tile at world position, returning the undo command (caller manages undo stack)
static std::unique_ptr<UndoCommand> paintOneTile(World* world,
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

    // Check if there's already a tile from the SAME tileset + layer at this position
    std::unique_ptr<UndoCommand> result;
    bool replaced = false;
    world->forEach<Transform, SpriteComponent>(
        [&](Entity* entity, Transform* t, SpriteComponent* s) {
            if (replaced) return;
            if (entity->tag() != "ground") return;
            auto* tlc = entity->getComponent<TileLayerComponent>();
            std::string entLayer = tlc ? tlc->layer : "ground";
            if (entLayer != tileLayer) return;
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
                if (el == tileLayer && entity->tag() == "ground" &&
                    std::abs(t->position.x - worldPos.x) < 1.0f &&
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
static Vec2 tileToWorldCenter(int col, int row, float gridSize) {
    float half = gridSize * 0.5f;
    return { col * gridSize + half, row * gridSize + half };
}

void Editor::paintTileAt(World* world, Camera* camera, const Vec2& screenPos,
                         int windowWidth, int windowHeight) {
    bool isCollisionLayer = (selectedTileLayer_ == "collision");
    if (!world || !camera) return;
    if (!isCollisionLayer && (selectedTileIndex_ < 0 || !paletteTexture_)) return;
    if (!isCollisionLayer && (paletteColumns_ <= 0 || paletteRows_ <= 0 || paletteTileSize_ <= 0)) return;
    if (!isCollisionLayer && selectedTileIndex_ >= paletteColumns_ * paletteRows_) return;

    Vec2 worldPos = camera->screenToWorld(screenPos, windowWidth, windowHeight);

    // Snap to grid center
    float half = gridSize_ * 0.5f;
    worldPos.x = std::floor(worldPos.x / gridSize_) * gridSize_ + half;
    worldPos.y = std::floor(worldPos.y / gridSize_) * gridSize_ + half;

    int tileCol = (int)std::floor(worldPos.x / gridSize_);
    int tileRow = (int)std::floor(worldPos.y / gridSize_);

    // --- Fill tool: flood fill on click ---
    if (currentTool_ == EditorTool::Fill) {
        // Collision layer has no source rects — fill is not meaningful
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
                if (entity->tag() != "ground") return;
                auto* tlc = entity->getComponent<TileLayerComponent>();
                std::string entLayer = tlc ? tlc->layer : "ground";
                if (entLayer != selectedTileLayer_) return;
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
                // Check if collision tile already exists here
                bool exists = false;
                world->forEach<Transform, TileLayerComponent>(
                    [&](Entity* entity, Transform* t, TileLayerComponent* tlc) {
                        if (tlc->layer == "collision" &&
                            std::abs(t->position.x - stampPos.x) < 1.0f &&
                            std::abs(t->position.y - stampPos.y) < 1.0f) {
                            exists = true;
                        }
                    }
                );
                if (!exists) {
                    Entity* tile = world->createEntity("CollisionTile");
                    tile->setTag("ground");
                    auto* transform = tile->addComponent<Transform>(stampPos);
                    transform->depth = -1.0f;
                    auto* sprite = tile->addComponent<SpriteComponent>();
                    sprite->size = {gridSize_, gridSize_};
                    sprite->tint = Color(1.0f, 0.2f, 0.2f, 0.35f);
                    auto* tlc = tile->addComponent<TileLayerComponent>();
                    tlc->layer = "collision";

                    auto cmd = std::make_unique<CreateCommand>();
                    cmd->createdHandle = tile->handle();
                    cmd->entityData = PrefabLibrary::entityToJson(tile);
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

    // Brush size
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("Brush", &brushSize_, 1, 1);
    if (brushSize_ < 1) brushSize_ = 1;
    if (brushSize_ > 5) brushSize_ = 5;

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

    // Paint mode toggle — shows active tool name
    bool isTileToolActive = (currentTool_ == EditorTool::Paint ||
                             currentTool_ == EditorTool::Fill ||
                             currentTool_ == EditorTool::RectFill ||
                             currentTool_ == EditorTool::LineTool);
    if (isTileToolActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        const char* activeLabel = currentTool_ == EditorTool::Paint ? "PAINTING" :
                                  currentTool_ == EditorTool::Fill ? "FILLING" :
                                  currentTool_ == EditorTool::RectFill ? "RECT FILL" : "LINE";
        if (ImGui::Button(activeLabel)) currentTool_ = EditorTool::Move;
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
        } else if (s && t) {
            hw = s->size.x * t->scale.x * 0.5f + 2.0f;
            hh = s->size.y * t->scale.y * 0.5f + 2.0f;
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
// Selection Outlines (stencil-based)
// ============================================================================

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

// ============================================================================
// ImGuizmo Transform Handles
// ============================================================================

void Editor::drawImGuizmo(Camera* camera) {
    if (!selectedEntity_ || !camera || viewportSize_.x <= 0 || viewportSize_.y <= 0) return;

    auto* t = selectedEntity_->getComponent<Transform>();
    if (!t) return;

    // Sync ImGuizmo operation to tool mode
    if (currentTool_ == EditorTool::Move)   gizmoOperation_ = ImGuizmo::TRANSLATE;
    else if (currentTool_ == EditorTool::Scale) gizmoOperation_ = ImGuizmo::SCALE;
    else if (currentTool_ == EditorTool::Rotate) gizmoOperation_ = ImGuizmo::ROTATE;

    // Set ImGuizmo rect to the viewport
    ImGuizmo::SetRect(viewportPos_.x, viewportPos_.y, viewportSize_.x, viewportSize_.y);
    ImGuizmo::SetOrthographic(true);

    // Build view matrix (camera space — translate by -camPos, scale by zoom)
    float zoom = camera->zoom();
    Vec2 camPos = camera->position();
    float invZoom = 1.0f / zoom;

    // View: identity (for 2D orthographic ImGuizmo, view is usually identity)
    float view[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };

    // Use the camera VP matrix as projection
    Mat4 vp = const_cast<Camera*>(camera)->getViewProjection();
    float proj[16];
    for (int i = 0; i < 16; i++) proj[i] = vp.m[i];

    // Build model matrix from entity transform (column-major)
    float cosR = cosf(t->rotation);
    float sinR = sinf(t->rotation);
    float sx = t->scale.x;
    float sy = t->scale.y;
    float tx = t->position.x;
    float ty = t->position.y;

    float model[16] = {
        cosR*sx,  sinR*sx, 0, 0,
        -sinR*sy, cosR*sy, 0, 0,
        0,        0,       1, 0,
        tx,       ty,      0, 1
    };

    if (ImGuizmo::Manipulate(view, proj, gizmoOperation_, ImGuizmo::LOCAL, model)) {
        // Decompose the modified model matrix back to entity transform
        float matTranslation[3], matRotation[3], matScale[3];
        ImGuizmo::DecomposeMatrixToComponents(model, matTranslation, matRotation, matScale);

        Vec2 oldPos = t->position;
        Vec2 oldScale = t->scale;
        float oldRot = t->rotation;

        t->position.x = matTranslation[0];
        t->position.y = matTranslation[1];
        t->scale.x = matScale[0];
        t->scale.y = matScale[1];
        // ImGuizmo returns degrees, convert to radians
        t->rotation = matRotation[2] * 0.0174532925f;

        // Record undo if transform changed
        if (t->position != oldPos) {
            auto cmd = std::make_unique<MoveCommand>();
            cmd->entityHandle = selectedEntity_->handle();
            cmd->oldPos = oldPos;
            cmd->newPos = t->position;
            UndoSystem::instance().push(std::move(cmd));
        }
        if (t->rotation != oldRot) {
            auto cmd = std::make_unique<RotateCommand>();
            cmd->entityHandle = selectedEntity_->handle();
            cmd->oldRotation = oldRot;
            cmd->newRotation = t->rotation;
            UndoSystem::instance().push(std::move(cmd));
        }
        if (t->scale != oldScale) {
            auto cmd = std::make_unique<ScaleCommand>();
            cmd->entityHandle = selectedEntity_->handle();
            cmd->oldScale = oldScale;
            cmd->newScale = t->scale;
            UndoSystem::instance().push(std::move(cmd));
        }
    }
}

// ============================================================================
// Play-in-Editor: Snapshot / Restore
// ============================================================================

void Editor::enterPlayMode(World* world) {
    if (inPlayMode_ || !world) return;
#ifdef FATE_HAS_GAME
    playModeSnapshot_ = nlohmann::json::array();
    world->forEachEntity([&](Entity* e) {
        // Skip transient runtime entities — same filter as saveScene
        std::string tag = e->tag();
        if (tag == "mob" || tag == "boss" || tag == "player" ||
            tag == "ghost" || tag == "dropped_item") return;
        playModeSnapshot_.push_back(PrefabLibrary::entityToJson(e));
    });
    // Auto-load animation metadata for scene-placed entities (NPCs, objects)
    world->forEachEntity([](Entity* e) {
        auto* sprite = e->getComponent<SpriteComponent>();
        auto* animator = e->getComponent<Animator>();
        if (sprite && animator && !sprite->texturePath.empty()) {
            AnimationLoader::tryAutoLoad(*sprite, *animator);
        }
    });
#endif // FATE_HAS_GAME

    paused_ = false;
    inPlayMode_ = true;
}


void Editor::exitPlayMode(World* world) {
    if (!inPlayMode_ || !world) return;
#ifdef FATE_HAS_GAME
    // Destroy all current entities
    std::vector<EntityHandle> toDestroy;
    world->forEachEntity([&](Entity* e) {
        toDestroy.push_back(e->handle());
    });
    for (auto h : toDestroy) {
        world->destroyEntity(h);
    }
    world->processDestroyQueue();

    // Restore from snapshot
    for (auto& entityJson : playModeSnapshot_) {
        PrefabLibrary::jsonToEntity(entityJson, *world);
    }
    playModeSnapshot_ = nlohmann::json();
#endif // FATE_HAS_GAME
    paused_ = true;
    inPlayMode_ = false;

    // Clear selection (entities were recreated with new handles)
    clearSelection();
}

// ============================================================================
// Scene Save / Load
// ============================================================================

void Editor::saveScene(World* world, const std::string& path) {
    if (!world) return;
    currentScenePath_ = path;

    nlohmann::json root;
    root["version"]   = SCENE_FORMAT_VERSION;
    root["gridSize"]  = gridSize_;
    root["sceneName"] = SceneManager::instance().currentSceneName();

    nlohmann::json entitiesJson = nlohmann::json::array();

    world->forEachEntity([&](Entity* entity) {
        // Skip transient runtime entities — these are not part of the scene:
        // mob/boss: spawned by SpawnSystem, player: created on server connect,
        // ghost: networked other-player entities, dropped_item: runtime loot
        std::string tag = entity->tag();
        if (tag == "mob" || tag == "boss" || tag == "player" ||
            tag == "ghost" || tag == "dropped_item") return;

        // Registry-based serialization — all registered components are handled
        entitiesJson.push_back(PrefabLibrary::entityToJson(entity));
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

    LOG_INFO("Editor", "Scene saved to %s (%zu entities)", path.c_str(), entitiesJson.size());
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

    // Read version (default to 1 for backward compat with pre-header files)
    int version = root.value("version", 1);
    if (version > SCENE_FORMAT_VERSION) {
        LOG_ERROR("Editor", "Scene file version %d is newer than supported (%d)",
                  version, SCENE_FORMAT_VERSION);
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

    // Registry-based deserialization — all registered components are handled
    size_t loadedCount = 0;
    for (auto& ej : root["entities"]) {
        PrefabLibrary::jsonToEntity(ej, *world);
        ++loadedCount;
    }
    selectedEntity_ = nullptr;
    selectedHandle_ = {};
    LOG_INFO("Editor", "Scene loaded v%d from %s (%zu entities)",
             version, path.c_str(), world->entityCount());
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
        if (ImGui::Button("Save", ImVec2(120, 0)) && isValidAssetName(prefabNameBuf)) {
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
                // Paper-doll entities (players) use AppearanceComponent for rendering,
                // not SpriteComponent::texture — don't flag them as errors
                bool hasError = spr && !spr->texture && !entity->getComponent<AppearanceComponent>();

                if (hasError) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
                else if (hasTag) ImGui::PushStyleColor(ImGuiCol_Text, color);

                ImGui::TreeNodeEx((void*)(intptr_t)entity->id(), flags, "%s%s",
                    entity->name().c_str(), hasError ? " (!)" : "");

                if (ImGui::IsItemClicked()) { selectedEntity_ = entity; selectedHandle_ = entity->handle(); }
                if (hasError || hasTag) ImGui::PopStyleColor();

                // Right-click context menu
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Delete", "Del", false, !isEntityLocked(entity))) {
                        if (dockWorld_) {
                            dockWorld_->destroyEntity(entity->handle());
                            if (selectedEntity_ == entity) { selectedEntity_ = nullptr; selectedHandle_ = {}; }
                        }
                    }
                    if (ImGui::MenuItem("Duplicate")) {
                        if (dockWorld_) {
                            auto json = PrefabLibrary::entityToJson(entity);
                            Entity* copy = PrefabLibrary::jsonToEntity(json, *dockWorld_);
                            if (copy) {
                                auto* t = copy->getComponent<Transform>();
                                if (t) t->position += Vec2(32.0f, 0.0f);
                                selectedEntity_ = copy;
                                selectedHandle_ = copy->handle();
                            }
                        }
                    }
                    ImGui::EndPopup();
                }
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
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float indentX = ImGui::GetCursorScreenPos().x - ImGui::GetStyle().IndentSpacing * 0.5f + 4.0f;
                    float startY = ImGui::GetCursorScreenPos().y;

                    for (auto* entity : group.entities) {
                        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                                 | ImGuiTreeNodeFlags_SpanAvailWidth;
                        if (entity == selectedEntity_) flags |= ImGuiTreeNodeFlags_Selected;

                        ImGui::TreeNodeEx((void*)(intptr_t)entity->id(), flags, "%s", entity->name().c_str());

                        if (ImGui::IsItemClicked()) { selectedEntity_ = entity; selectedHandle_ = entity->handle(); }

                        // Right-click context menu
                        if (ImGui::BeginPopupContextItem()) {
                            if (ImGui::MenuItem("Delete", "Del", false, !isEntityLocked(entity))) {
                                if (dockWorld_) {
                                    dockWorld_->destroyEntity(entity->handle());
                                    if (selectedEntity_ == entity) { selectedEntity_ = nullptr; selectedHandle_ = {}; }
                                }
                            }
                            if (ImGui::MenuItem("Duplicate")) {
                                if (dockWorld_) {
                                    auto json = PrefabLibrary::entityToJson(entity);
                                    Entity* copy = PrefabLibrary::jsonToEntity(json, *dockWorld_);
                                    if (copy) {
                                        auto* t = copy->getComponent<Transform>();
                                        if (t) t->position += Vec2(32.0f, 0.0f);
                                        selectedEntity_ = copy;
                                        selectedHandle_ = copy->handle();
                                    }
                                }
                            }
                            ImGui::EndPopup();
                        }
                    }

                    float endY = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;
                    if (endY > startY) {
                        dl->AddLine(
                            ImVec2(indentX, startY), ImVec2(indentX, endY),
                            IM_COL32(255, 255, 255, 25), 1.0f);
                    }

                    ImGui::TreePop();
                }
            }
        }
    }
    ImGui::End();
}

// Inspector methods moved to editor_inspector.cpp
// (drawReflectedComponent, captureInspectorUndo, drawInspector)

// ============================================================================
// Erase Tool
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
                    if (entity->tag() != "ground") return;
                    auto* tlc = entity->getComponent<TileLayerComponent>();
                    std::string entLayer = tlc ? tlc->layer : "ground";
                    if (entLayer != selectedTileLayer_) return;
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
        try {
            EntityId id = (EntityId)std::stoul(args[1]);
            auto* e = world->getEntity(id);
            if (e) {
                LOG_INFO("Console", "Deleted entity %u (%s)", id, e->name().c_str());
                world->destroyEntity(id);
            } else {
                LOG_WARN("Console", "Entity %u not found", id);
            }
        } catch (const std::exception&) {
            LOG_WARN("Console", "Invalid entity id: %s", args[1].c_str());
        }
    }
    else if (args[0] == "spawn" && args.size() > 3) {
        if (!world) return;
        try {
            float x = std::stof(args[2]);
            float y = std::stof(args[3]);
            auto* e = PrefabLibrary::instance().spawn(args[1], *world, {x, y});
            if (e) LOG_INFO("Console", "Spawned '%s' at (%.0f, %.0f) id=%u", args[1].c_str(), x, y, e->id());
            else LOG_WARN("Console", "Prefab '%s' not found", args[1].c_str());
        } catch (const std::exception&) {
            LOG_WARN("Console", "Invalid coordinates for spawn");
        }
    }
    else if (args[0] == "tp" && args.size() > 2) {
        if (!world) return;
        try {
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
        } catch (const std::exception&) {
            LOG_WARN("Console", "Invalid coordinates for tp");
        }
    }
    else {
        LOG_WARN("Console", "Unknown command: %s (type 'help')", args[0].c_str());
    }
}

// ============================================================================
// Keyboard Shortcuts
// ============================================================================

void Editor::handleKeyShortcuts(World* world, const SDL_Event& event) {
    if (!world) return;

    // Ctrl+S prefab save works even when editor panels are hidden,
    // so designers can tweak values in play mode and save without
    // needing the full editor open.
    {
        auto scancode = event.key.keysym.scancode;
        bool ctrl = (event.key.keysym.mod & KMOD_CTRL) != 0;
        if (ctrl && scancode == SDL_SCANCODE_S) {
            world->forEachEntity([](Entity* e) {
                if (e->tag() != "player") return;
                if (PrefabLibrary::instance().has("player")) {
                    PrefabLibrary::instance().save("player", e);
                    LOG_INFO("Editor", "Ctrl+S: saved player entity to prefab 'player'");
                }
            });
        }
    }

    if (!open_) return;

    auto scancode = event.key.keysym.scancode;
    bool ctrl = (event.key.keysym.mod & KMOD_CTRL) != 0;
    bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;

    // Non-modifier shortcuts (W/E/R/B/X/Delete) only fire when paused.
    // In Play mode the game owns the keyboard — tool switching would
    // conflict with WASD movement, chat, and other gameplay keys.
    bool allowToolKeys = paused_;

    // Escape = Cancel placement / clear selection
    if (scancode == SDL_SCANCODE_ESCAPE) {
        if (isDraggingAsset_) {
            isDraggingAsset_ = false;
            draggedAssetPath_.clear();
            pendingBrushStroke_.reset();
        } else if (selectedEntity_) {
            clearSelection();
        }
    }

    // Ctrl+Z = Undo
    if (ctrl && scancode == SDL_SCANCODE_Z && !shift) {
        UndoSystem::instance().undo(world);
        refreshSelection(world);
        if (uiManager_) uiEditorPanel_.revalidateSelection(*uiManager_);
    }
    // Ctrl+Y or Ctrl+Shift+Z = Redo
    if ((ctrl && scancode == SDL_SCANCODE_Y) ||
        (ctrl && shift && scancode == SDL_SCANCODE_Z)) {
        UndoSystem::instance().redo(world);
        refreshSelection(world);
        if (uiManager_) uiEditorPanel_.revalidateSelection(*uiManager_);
    }
    // Ctrl+S = Save current scene
    if (ctrl && scancode == SDL_SCANCODE_S && !inPlayMode_) {
        if (!currentScenePath_.empty()) {
            saveScene(world, currentScenePath_);
            LOG_INFO("Editor", "Ctrl+S: saved scene to %s", currentScenePath_.c_str());
        } else {
            LOG_WARN("Editor", "Ctrl+S: no scene path set (currentScenePath_ is empty)");
        }
    }
    // Also save the focused UI screen if a UI widget is selected
    if (ctrl && scancode == SDL_SCANCODE_S) {
        auto& uiPanel = uiEditorPanel_;
        auto* selNode = uiPanel.selectedNode();
        if (selNode && !uiPanel.selectedScreenId().empty() && uiManager_) {
            std::string screenId = uiPanel.selectedScreenId();
            auto* root = uiManager_->getScreen(screenId);
            if (root) {
                std::string relPath = "assets/ui/screens/" + screenId + ".json";
                UISerializer::saveToFile(relPath, screenId, root);
                LOG_INFO("Editor", "Saved UI screen: %s", relPath.c_str());
                // Also save to source directory so changes survive rebuilds
                // sourceDir_ is FATE_SOURCE_DIR/assets/scenes — go up to project root
                if (!sourceDir_.empty()) {
                    std::string projectRoot = sourceDir_;
                    auto pos = projectRoot.rfind("/assets/scenes");
                    if (pos == std::string::npos) pos = projectRoot.rfind("\\assets\\scenes");
                    if (pos != std::string::npos) projectRoot = projectRoot.substr(0, pos);
                    std::string srcPath = projectRoot + "/" + relPath;
                    UISerializer::saveToFile(srcPath, screenId, root);
                    LOG_INFO("Editor", "Saved UI screen (source): %s", srcPath.c_str());
                }
                uiManager_->suppressHotReload();
            }
        }
        // Player prefab save is handled above (before the !open_ guard)
    }
    // Ctrl+D = Duplicate
    if (ctrl && scancode == SDL_SCANCODE_D && selectedEntity_) {
        auto json = PrefabLibrary::entityToJson(selectedEntity_);
        Entity* copy = PrefabLibrary::jsonToEntity(json, *world);
        if (copy) {
            auto* t = copy->getComponent<Transform>();
            if (t) t->position += Vec2(32.0f, 0.0f);
            selectedEntity_ = copy;
            selectedHandle_ = copy->handle();

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
    // Delete = Delete selected (paused only)
    if (allowToolKeys && scancode == SDL_SCANCODE_DELETE && selectedEntity_ && !isEntityLocked(selectedEntity_)) {
        auto cmd = std::make_unique<DeleteCommand>();
        cmd->entityData = PrefabLibrary::entityToJson(selectedEntity_);
        cmd->deletedHandle = selectedEntity_->handle();
        UndoSystem::instance().push(std::move(cmd));

        world->destroyEntity(selectedEntity_->handle());
        selectedEntity_ = nullptr;
        selectedHandle_ = {};
    }
    // W = Move tool (paused only — W is move-up in Play mode)
    if (allowToolKeys && scancode == SDL_SCANCODE_W && !ctrl) {
        currentTool_ = EditorTool::Move;
        pendingBrushStroke_.reset();
    }
    // E = Scale tool (paused only)
    if (allowToolKeys && scancode == SDL_SCANCODE_E && !ctrl) {
        currentTool_ = EditorTool::Scale;
        pendingBrushStroke_.reset();
    }
    // R = Rotate tool (paused only)
    if (allowToolKeys && scancode == SDL_SCANCODE_R && !ctrl) {
        currentTool_ = EditorTool::Rotate;
        pendingBrushStroke_.reset();
    }
    // B = Paint tool (paused only)
    if (allowToolKeys && scancode == SDL_SCANCODE_B && !ctrl) {
        currentTool_ = EditorTool::Paint;
        pendingBrushStroke_.reset();
    }
    // X = Erase tool (paused only)
    if (allowToolKeys && scancode == SDL_SCANCODE_X && !ctrl) {
        currentTool_ = EditorTool::Erase;
        pendingBrushStroke_.reset();
    }
    // G = Flood fill tool (paused only)
    if (allowToolKeys && scancode == SDL_SCANCODE_G && !ctrl) {
        currentTool_ = EditorTool::Fill;
        pendingBrushStroke_.reset();
    }
    // U = Rectangle fill tool (paused only)
    if (allowToolKeys && scancode == SDL_SCANCODE_U && !ctrl) {
        currentTool_ = EditorTool::RectFill;
        pendingBrushStroke_.reset();
    }
    // L = Line tool (paused only)
    if (allowToolKeys && scancode == SDL_SCANCODE_L && !ctrl) {
        currentTool_ = EditorTool::LineTool;
        pendingBrushStroke_.reset();
    }
}

#else // !FATE_HAS_GAME — demo build with engine-only panels

void Editor::applyLayerVisibility(World*) {}
void Editor::renderScene(SpriteBatch*, Camera*) {}
void Editor::renderUI(World* world, Camera*, SpriteBatch*, FrameArena* frameArena) {
    if (!frameStarted_) return;

    drawDockSpace();
    drawMenuBar(world);
    drawSceneViewport();
    drawHierarchy(world);
    drawDebugInfoPanel(world);
    LogViewer::instance().draw();
    drawAssetBrowser(world, nullptr);
    dialogueEditor_.draw();

    if (uiManager_) {
        uiEditorPanel_.draw(*uiManager_);
    }

#if defined(ENGINE_MEMORY_DEBUG)
    if (showMemoryPanel_) {
        drawMemoryPanel(&showMemoryPanel_, frameArena);
    }
#endif

    if (showDemoWindow_) {
        ImGui::ShowDemoWindow(&showDemoWindow_);
    }

    // Post-process config panel
    if (showPostProcessPanel_ && postProcessConfig_) {
        ImGui::SetNextWindowSize(ImVec2(280, 320), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Post Process", &showPostProcessPanel_)) {
            ImGui::Checkbox("Bloom Enabled", &postProcessConfig_->bloomEnabled);
            ImGui::DragFloat("Bloom Threshold", &postProcessConfig_->bloomThreshold, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Bloom Strength", &postProcessConfig_->bloomStrength, 0.01f, 0.0f, 4.0f);
            ImGui::Separator();
            ImGui::Checkbox("Vignette", &postProcessConfig_->vignetteEnabled);
            ImGui::DragFloat("Vignette Radius", &postProcessConfig_->vignetteRadius, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Vignette Smoothness", &postProcessConfig_->vignetteSmoothness, 0.01f, 0.0f, 2.0f);
            ImGui::Separator();
            float tint[3] = {postProcessConfig_->colorTint.r, postProcessConfig_->colorTint.g, postProcessConfig_->colorTint.b};
            if (ImGui::ColorEdit3("Color Tint", tint)) {
                postProcessConfig_->colorTint = {tint[0], tint[1], tint[2], 1.0f};
            }
            ImGui::DragFloat("Brightness", &postProcessConfig_->brightness, 0.01f, 0.0f, 3.0f);
            ImGui::DragFloat("Contrast", &postProcessConfig_->contrast, 0.01f, 0.0f, 3.0f);
        }
        ImGui::End();
    }

    ImGui::Render();
#ifndef FATEMMO_METAL
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
    wantsKeyboard_ = io.WantCaptureKeyboard;
    wantsMouse_ = io.WantCaptureMouse;
}
void Editor::drawDockSpace() {
    ImGuiDockNodeFlags flags = ImGuiDockNodeFlags_PassthruCentralNode;
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), flags);
}
void Editor::drawMenuBar(World*) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Post Process", nullptr, &showPostProcessPanel_);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &showDemoWindow_);
#if defined(ENGINE_MEMORY_DEBUG)
            ImGui::MenuItem("Memory", nullptr, &showMemoryPanel_);
#endif
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("About")) {
            ImGui::Text("FateMMO Engine v0.1.0");
            ImGui::Separator();
            ImGui::Text("C++23 | OpenGL 3.3 | Custom UDP");
            ImGui::Text("github.com/wFate/FateMMO_GameEngine");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}
void Editor::drawSceneViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("Scene")) {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 size = ImGui::GetContentRegionAvail();
        viewportPos_ = {pos.x, pos.y};
        viewportSize_ = {size.x, size.y};
        viewportHovered_ = ImGui::IsWindowHovered();

        int w = (int)size.x, h = (int)size.y;
        if (w > 0 && h > 0) {
            viewportFbo_.resize(w, h);
            if (viewportFbo_.isValid()) {
                ImGui::Image((ImTextureID)(uintptr_t)viewportFbo_.textureId(), size,
                             ImVec2(0, 1), ImVec2(1, 0));
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
void Editor::drawHierarchy(World* world) {
    if (ImGui::Begin("Hierarchy")) {
        if (world) {
            ImGui::Text("Entities: %zu", world->entityCount());
            ImGui::Separator();
            ImGui::TextDisabled("(Full game build shows entity tree)");
        } else {
            ImGui::TextDisabled("No world loaded");
        }
    }
    ImGui::End();
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
    }
    ImGui::End();
}
void Editor::drawSceneGridShader(Camera*) {}
void Editor::handleSceneClick(World*, Camera*, const Vec2&, int, int) {}
void Editor::handleSceneDrag(Camera*, const Vec2&, int, int) {}
void Editor::handleMouseUp() {}
void Editor::paintTileAt(World*, Camera*, const Vec2&, int, int) {}
void Editor::eraseTileAt(World*, Camera*, const Vec2&, int, int) {}
void Editor::scanAssets() {}
void Editor::drawAssetBrowser(World* world, Camera* camera) {
    if (ImGui::Begin("Project")) {
        assetBrowser_.draw(world, camera);
    }
    ImGui::End();
}
void Editor::drawTilePalette(World*, Camera*) {}
void Editor::drawSceneGrid(SpriteBatch*, Camera*) {}
void Editor::drawSelectionOutlines(SpriteBatch*, Camera*) {}
void Editor::drawImGuizmo(Camera*) {}
void Editor::saveScene(World*, const std::string&) {}
void Editor::loadScene(World*, const std::string&) {}
void Editor::enterPlayMode(World*) {}
void Editor::exitPlayMode(World*) {}
void Editor::drawHUD(World*) {}
void Editor::drawToolbar(World*) {}
void Editor::drawConsole(World*) {}
void Editor::executeCommand(World*, const std::string&) {}
void Editor::loadTileset(const std::string&, int) {}
void Editor::handleKeyShortcuts(World*, const SDL_Event&) {}
void Editor::captureInspectorUndo() {}

#endif // FATE_HAS_GAME

} // namespace fate
