#include "engine/editor/editor.h"
#include "engine/editor/combat_text_editor.h"
#ifdef FATE_HAS_GAME
#include "engine/ui/ui_safe_area.h"
#include "engine/render/layout_class.h"
#endif
#include "engine/core/logger.h"
#include "engine/core/atomic_write.h"
#if FATE_ENABLE_HOT_RELOAD
#include "engine/module/hot_reload_manager.h"
#endif
#ifndef FATEMMO_METAL
// Editor uses direct GL for ImGui integration --intentionally outside RHI
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

#include "engine/components/transform.h"
#include "engine/components/sprite_component.h"
#ifdef FATE_HAS_GAME
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
    // Route every undo-stack push/undo/redo through our dirty bookkeeping.
    // Domains: UIScreen → markUIScreenDirty(screenId), PlayerPrefab →
    // markPlayerPrefabDirty, Scene → markSceneDirty. Wired here (init runs
    // once before any editor input) instead of per-call-site so all push
    // sites stay narrow.
    UndoSystem::instance().setDirtyCallback([](UndoCommand* cmd) {
        if (!cmd) return;
        auto& ed = Editor::instance();
        switch (cmd->domain()) {
            case UndoDomain::Scene:
                ed.markSceneDirty();
                break;
            case UndoDomain::PlayerPrefab:
                ed.markPlayerPrefabDirty();
                break;
            case UndoDomain::UIScreen:
#ifdef FATE_HAS_GAME
                if (auto* p = dynamic_cast<UIPropertyCommand*>(cmd))
                    ed.markUIScreenDirty(p->screenId);
                else if (auto* p = dynamic_cast<UIWidgetMoveCommand*>(cmd))
                    ed.markUIScreenDirty(p->screenId);
#endif
                break;
        }
    });

    // Selection survival across PropertyCommand-style entity recreation.
    // Without this, undo/redo on an inspector edit minted a fresh
    // EntityHandle, the editor kept holding the OLD handle in
    // selectedHandle_, refreshSelection's getEntity returned nullptr, and
    // the user lost both the hierarchy highlight AND the scene-view
    // bounding box — the entity was still in the world but invisible to
    // the editor. UndoSystem::remapHandle now fires this callback after
    // patching the other commands' stored handles; we patch the editor's
    // selection state with the same mapping. Multi-select is covered too.
    UndoSystem::instance().setHandleRemapCallback(
        [](EntityHandle oldH, EntityHandle newH) {
            auto& ed = Editor::instance();
            if (ed.selectedHandle_ == oldH) {
                ed.selectedHandle_ = newH;
                LOG_INFO("Undo", "Selection re-bound: %u -> %u",
                         oldH.index(), newH.index());
            }
            // Multi-select: re-key the entry in place.
            auto it = ed.selectedEntities_.find(oldH);
            if (it != ed.selectedEntities_.end()) {
                ed.selectedEntities_.erase(it);
                ed.selectedEntities_.insert(newH);
            }
            // refreshSelection reruns next renderUI tick when world is
            // available; nothing to do synchronously here.
        });

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
        LOG_WARN("Editor", "Inter fonts not found --using ImGui default");
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

    // Spacing --8px grid, tight vertical, comfortable horizontal
    style.WindowPadding     = ImVec2(8, 8);
    style.FramePadding      = ImVec2(6, 4);
    style.CellPadding       = ImVec2(4, 3);
    style.ItemSpacing       = ImVec2(8, 4);
    style.ItemInnerSpacing  = ImVec2(4, 4);
    style.IndentSpacing     = 16.0f;
    style.ScrollbarSize     = 11.0f;
    style.GrabMinSize       = 8.0f;

    // Rounding --subtle modern softness
    style.WindowRounding    = 3.0f;
    style.ChildRounding     = 3.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 3.0f;

    // Borders --minimal, modern
    style.WindowBorderSize     = 1.0f;
    style.ChildBorderSize      = 0.0f;
    style.PopupBorderSize      = 1.0f;
    style.FrameBorderSize      = 0.0f;
    style.TabBorderSize        = 0.0f;
    style.DockingSeparatorSize = 2.0f;

    // Color scheme --layered dark backgrounds with blue accent
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
    assetBrowser_.init(".", assetRoot_, sourceDir_);
    assetBrowser_.onOpenAnimation = [this](const std::string& path) {
        if (path.find(".png") != std::string::npos || path.find(".jpg") != std::string::npos)
            animationEditor_.openWithSheet(path);
        else
            animationEditor_.openFile(path);
    };
    assetBrowser_.onDeleteFile = [this](const std::string& path) {
        pendingDeleteFile_ = true;
        pendingDeletePath_ = path;
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
    // last frame --reading after NewFrame() always returns false.
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

// ============================================================================
// Scene Identity / Save / Load (engine-layer; available in all builds)
// ============================================================================

std::string Editor::currentSceneId() const {
    if (currentScenePath_.empty()) return {};
    size_t slash = currentScenePath_.find_last_of("/\\");
    size_t dot = currentScenePath_.rfind('.');
    size_t start = (slash != std::string::npos) ? slash + 1 : 0;
    if (dot != std::string::npos && dot > start)
        return currentScenePath_.substr(start, dot - start);
    return currentScenePath_.substr(start);
}

bool Editor::flushDirtyPlayerPrefab(World* world) {
    if (!playerPrefabDirty_) return true;
    // Find the player FIRST. The has("player") check is unreliable as a
    // gate because PrefabLibrary::save() itself populates the cache — a
    // first-ever save would short-circuit here and silently drop the
    // bit, losing the user's edit. Only the absence of a live player
    // entity is grounds for dropping the dirty bit, since at that point
    // the data is genuinely unrecoverable.
    Entity* player = nullptr;
    if (world) {
        world->forEachEntity([&](Entity* e) {
            if (!player && e->tag() == "player") player = e;
        });
    }
    if (!player) {
        // Stale dirty bit — the entity carrying the unsaved edits is gone
        // (e.g. user explicitly deleted the player). The unsaved data is
        // unrecoverable, but blocking subsequent ops would be worse than
        // dropping the bit. Log so the user has a breadcrumb.
        LOG_WARN("Editor", "Player prefab dirty but no player entity; dropping stale bit");
        playerPrefabDirty_ = false;
        return true;
    }
    // Attempt the save unconditionally. PrefabLibrary::save now treats a
    // source-dir write failure as a hard failure (returns false), so any
    // false return here means at least one of {runtime, source} is stale
    // and we MUST keep playerPrefabDirty_ set so the caller (loadScene
    // etc.) can refuse to proceed.
    if (PrefabLibrary::instance().save("player", player)) {
        LOG_INFO("Editor", "Auto-saved player prefab before scene transition");
        playerPrefabDirty_ = false;
        return true;
    }
    lastSaveStatus_ = "Player prefab save FAILED before scene transition";
    lastSaveSucceeded_ = false;
    LOG_ERROR("Editor", "Player prefab save FAILED before scene transition");
    return false;
}

bool Editor::flushDirtyUIScreens() {
#ifdef FATE_HAS_GAME
    if (!uiManager_ || dirtyScreens_.empty()) return true;
    bool allOk = true;
    // Snapshot first; we mutate dirtyScreens_ inside the loop on success.
    std::vector<std::pair<std::string, fate::LayoutClass>> pending(
        dirtyScreens_.begin(), dirtyScreens_.end());
    for (const auto& [screenId, recordedCls] : pending) {
        auto* root = uiManager_->getScreen(screenId);
        if (!root) {
            // Screen was unloaded after being marked dirty; nothing to
            // write. Drop the bit so it doesn't linger.
            dirtyScreens_.erase({screenId, recordedCls});
            continue;
        }
        // Use the class recorded at edit time, NOT current(). If the user
        // edited Tablet then switched to Base, this still writes the
        // foo.tablet.json file rather than clobbering foo.json with a tree
        // that's about to be reloaded.
        const std::string& base = uiManager_->screenBasePath(screenId);
        std::string canonicalBase = !base.empty()
            ? base
            : "assets/ui/screens/" + screenId + ".json";
        std::string relPath = (recordedCls == fate::LayoutClass::Base)
            ? canonicalBase
            : fate::mangleVariantPath(canonicalBase, recordedCls);

        bool runtimeOk = UISerializer::saveToFile(relPath, screenId, root);
        if (!runtimeOk) {
            lastSaveStatus_ = "UI screen save FAILED: " + relPath;
            lastSaveSucceeded_ = false;
            LOG_ERROR("Editor", "UI screen save FAILED: %s", relPath.c_str());
            allOk = false;
            continue;  // leave dirty bit set so the user can retry
        }
        LOG_INFO("Editor", "Saved UI screen: %s", relPath.c_str());

        // Source-dir copy is the one that survives rebuilds; if it fails
        // we MUST keep the screen dirty so the user can retry.
        bool sourceOk = true;
        if (!sourceDir_.empty()) {
            std::string projectRoot = sourceDir_;
            auto pos = projectRoot.rfind("/assets/scenes");
            if (pos == std::string::npos) pos = projectRoot.rfind("\\assets\\scenes");
            if (pos != std::string::npos) projectRoot = projectRoot.substr(0, pos);
            std::string srcPath = projectRoot + "/" + relPath;
            sourceOk = UISerializer::saveToFile(srcPath, screenId, root);
            if (!sourceOk) {
                lastSaveStatus_ = "UI screen source save FAILED: " + srcPath;
                lastSaveSucceeded_ = false;
                LOG_ERROR("Editor", "UI screen source save FAILED: %s", srcPath.c_str());
                allOk = false;
            } else {
                LOG_INFO("Editor", "Saved UI screen (source): %s", srcPath.c_str());
            }
        }
        uiManager_->suppressHotReload();
        // Only drop the dirty bit when both writes succeeded.
        if (runtimeOk && sourceOk) {
            dirtyScreens_.erase({screenId, recordedCls});
        }
    }
    return allOk;
#else
    return true;
#endif // FATE_HAS_GAME
}

bool Editor::saveScene(World* world, const std::string& path) {
    if (!world) {
        lastSaveStatus_ = "Scene save FAILED: no world";
        lastSaveSucceeded_ = false;
        return false;
    }
    currentScenePath_ = path;

    nlohmann::json root;
    root["version"]   = SCENE_FORMAT_VERSION;
    root["gridSize"]  = gridSize_;
    root["sceneName"] = SceneManager::instance().currentSceneName();
    root["metadata"]  = sceneMetadata_;

    nlohmann::json entitiesJson = nlohmann::json::array();

    world->forEachEntity([&](Entity* entity) {
        // Skip runtime-spawned entities so they never get baked into the
        // scene .json. Two layers of protection:
        //   1. isReplicated() flag: every createGhost*/spawnPet/createPlayer
        //      path sets this. Authored content from JSON always loads with
        //      isReplicated=false (the field isn't serialized).
        //   2. Tag fallback: in case a future factory forgets to set the
        //      flag, the legacy tag list still catches the obvious ones.
        if (entity->isReplicated()) return;
        std::string tag = entity->tag();
        if (tag == "mob" || tag == "boss" || tag == "player" ||
            tag == "ghost" || tag == "dropped_item" || tag == "pet") return;

        // Registry-based serialization --all registered components are handled
        entitiesJson.push_back(PrefabLibrary::entityToJson(entity));
    });

    root["entities"] = entitiesJson;

    if (inPlayMode_) {
        playModeSnapshot_ = entitiesJson;
    }

    std::string jsonStr = root.dump(2);

    // Editor save deliberately bypasses IAssetSource and writes loose-files
    // directly via writeFileAtomic. This is intentional and load-bearing:
    //   1. The editor is stripped from FATE_SHIPPING builds (see CMakeLists.txt),
    //      so it only ever runs against loose-files on a developer machine.
    //   2. Atomic .tmp+rename has no PhysFS analogue — packed `.pak` archives
    //      don't support partial-overwrite, so an IAssetWriter wrapper would
    //      either be a no-op for archives or have radically different semantics
    //      from the disk path callers depend on (.tmp survival, source-dir
    //      dual-save, hot-reload suppression).
    // To enable saving into a packaged build, introduce a `IAssetWriter`
    // interface with `writeBytes(key, content) → Result` semantics, route
    // every authored-asset writer (this function, UISerializer::saveToFile,
    // DialogueNodeEditor::saveToFile, AnimationEditor::saveTemplate /
    // saveFrameSet / saveMetaJson, the packed-meta writer in packFrameSet,
    // and the prefab save path) through it, and provide both a DirectFs
    // writer (preserves today's behavior) and a `.pak` overlay writer.
    // Until that interface lands, the loose-file assumption is a hard
    // contract — do NOT add a new authored-asset writer that calls
    // std::ofstream / fwrite / IAssetSource directly.
    std::string writeErr;
    if (!writeFileAtomic(path, jsonStr, &writeErr)) {
        LOG_ERROR("Editor", "Scene save FAILED: %s (%s) — runtime path not updated on disk",
                  path.c_str(), writeErr.c_str());
        lastSaveStatus_ = "Scene save FAILED: " + path + " (" + writeErr + ")";
        lastSaveSucceeded_ = false;
        return false;
    }

    // Also save to source dir (persists across rebuilds)
    if (!sourceDir_.empty()) {
        std::string srcPath = sourceDir_ + "/" + fs::path(path).filename().string();
        if (!writeFileAtomic(srcPath, jsonStr, &writeErr)) {
            LOG_ERROR("Editor", "Scene save to source FAILED: %s (%s) — runtime copy ok, source copy stale",
                      srcPath.c_str(), writeErr.c_str());
            lastSaveStatus_ = "Source save FAILED: " + srcPath + " (" + writeErr + ")";
            lastSaveSucceeded_ = false;
            return false;
        }
        LOG_INFO("Editor", "Scene also saved to source: %s", srcPath.c_str());
    }

    LOG_INFO("Editor", "Scene saved to %s (%zu entities)", path.c_str(), entitiesJson.size());
    lastSaveStatus_ = "Scene saved: " + path;
    lastSaveSucceeded_ = true;
    return true;
}

void Editor::loadScene(World* world, const std::string& path) {
    if (!world) return;

    // Helper: surface a load failure both in the log and the HUD strip. Reuses
    // the save status channel — designers don't distinguish save vs load
    // failures, they just need to know the scene didn't come up. Note that
    // currentScenePath_ is NOT updated until the load actually succeeds, so
    // a Ctrl+S immediately after a failed load won't quietly overwrite the
    // good source file with whatever's in the world right now.
    auto reportLoadFailure = [this, &path](const std::string& detail) {
        LOG_ERROR("Editor", "Scene load FAILED (%s): %s", detail.c_str(), path.c_str());
        lastSaveStatus_ = "Scene load FAILED: " + path + " (" + detail + ")";
        lastSaveSucceeded_ = false;
    };

    std::ifstream file(path);
    if (!file.is_open()) {
        reportLoadFailure("cannot open file");
        return;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(file);
    } catch (const nlohmann::json::exception& e) {
        reportLoadFailure(std::string("parse error: ") + e.what());
        return;
    }

    // Read version (default to 1 for backward compat with pre-header files)
    int version = root.value("version", 1);
    if (version > SCENE_FORMAT_VERSION) {
        reportLoadFailure("file version " + std::to_string(version) +
                          " is newer than supported " + std::to_string(SCENE_FORMAT_VERSION));
        return;
    }

    // Validate shape BEFORE destroying anything — otherwise a scene file with
    // valid JSON but a malformed entities/gridSize/metadata field would wipe
    // the current world and leave the editor empty.
    if (root.contains("gridSize") && !root["gridSize"].is_number()) {
        reportLoadFailure(std::string("gridSize must be a number (got ") +
                          root["gridSize"].type_name() + ")");
        return;
    }
    if (root.contains("metadata") && !root["metadata"].is_object()) {
        reportLoadFailure(std::string("metadata must be an object (got ") +
                          root["metadata"].type_name() + ")");
        return;
    }
    if (root.contains("entities") && !root["entities"].is_array()) {
        reportLoadFailure(std::string("entities must be an array (got ") +
                          root["entities"].type_name() + ")");
        return;
    }

    // Validation passed. Before mutating any state, flush the dirty player
    // prefab using the still-alive prior world — the entity carrying the
    // unsaved edits is about to be destroyed by the entity-clear loop
    // below, so this is the last chance to persist it. If the save fails
    // we abort the load entirely (do NOT swap currentScenePath_, do NOT
    // touch the world); the user can retry once they've fixed the cause
    // of the write failure.
    if (!flushDirtyPlayerPrefab(world)) {
        reportLoadFailure("aborted: pending player-prefab save failed");
        return;
    }

    // Committing to the load. Adopt the path now so that a follow-up
    // Ctrl+S targets this scene, and clear any stale failure status from
    // a prior load attempt.
    currentScenePath_ = path;
    // Drop scene-local dirty only. The pending entity diff belonged to
    // the prior scene; without this, Ctrl+S would write the new scene's
    // path with sceneDirty_ left over from scene A. UI screen dirty is
    // a cross-scene document and keeps its bits — those edits target
    // their own files, not the scene .json. Player prefab dirty was
    // either saved by flushDirtyPlayerPrefab above (clearing its bit) or
    // dropped as stale.
    clearSceneDirty();
    if (!lastSaveSucceeded_ && lastSaveStatus_.find("load FAILED") != std::string::npos) {
        lastSaveStatus_.clear();
        lastSaveSucceeded_ = true;
    }

    // Clear existing entities (in play mode, keep runtime entities for spectator)
    world->forEachEntity([&](Entity* entity) {
#ifdef FATE_HAS_GAME
        if (inPlayMode_) {
            std::string tag = entity->tag();
            if (tag == "mob" || tag == "boss" || tag == "player" ||
                tag == "ghost" || tag == "dropped_item" || tag == "pet") return;
        }
#endif
        world->destroyEntity(entity->handle());
    });
    world->processDestroyQueue();

    if (root.contains("gridSize")) {
        gridSize_ = root["gridSize"].get<float>();
    }

    // Preserve scene metadata for round-trip (sceneType, minLevel, maxLevel, etc.)
    if (root.contains("metadata")) {
        sceneMetadata_ = root["metadata"];
    } else {
        sceneMetadata_ = nlohmann::json::object();
    }

    if (!root.contains("entities")) {
        // Still notify spectator even for empty scenes
#ifdef FATE_HAS_GAME
        if ((inPlayMode_ || isObserving_) && onSceneLoadedInPlayMode) {
            size_t slash = path.find_last_of("/\\");
            size_t dot = path.rfind('.');
            size_t start = (slash != std::string::npos) ? slash + 1 : 0;
            std::string sceneName = (dot > start) ? path.substr(start, dot - start) : path.substr(start);
            onSceneLoadedInPlayMode(sceneName);
        }
#endif
        return;
    }

    // Registry-based deserialization --all registered components are handled
    size_t loadedCount = 0;
    for (auto& ej : root["entities"]) {
        PrefabLibrary::jsonToEntity(ej, *world);
        ++loadedCount;
    }
    selectedEntity_ = nullptr;
    selectedHandle_ = {};

#ifdef FATE_HAS_GAME
    // Notify game app for spectator mode (sends CmdSpectateScene to server)
    if ((inPlayMode_ || isObserving_) && onSceneLoadedInPlayMode) {
        size_t slash = path.find_last_of("/\\");
        size_t dot = path.rfind('.');
        size_t start = (slash != std::string::npos) ? slash + 1 : 0;
        std::string sceneName = (dot > start) ? path.substr(start, dot - start) : path.substr(start);
        onSceneLoadedInPlayMode(sceneName);
    }
#endif

    LOG_INFO("Editor", "Scene loaded v%d from %s (%zu entities)",
             version, path.c_str(), world->entityCount());
}

#ifdef FATE_HAS_GAME
// ============================================================================
// Render
// ============================================================================

void Editor::renderScene(SpriteBatch* batch, Camera* camera) {
    // Called while FBO is bound --draw in-viewport overlays via SpriteBatch
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

    // Draw DB-backed spawn zone circles when overlay is enabled
    if (showSpawnDebug_ && paused_) {
        std::string sceneId = currentSceneId();
        if (!sceneId.empty()) {
            contentBrowserPanel_.ensureSpawnListLoaded();
            const auto& list = contentBrowserPanel_.spawnList();

            // Sync viewport selection with content browser panel selection
            int cbSelection = contentBrowserPanel_.selectedSpawnIndex();
            const std::string& selectedScene = contentBrowserPanel_.selectedSpawnScene();
            bool hasSceneSelection = !selectedScene.empty();
            if (cbSelection >= 0) {
                selectedSpawnZoneIdx_ = cbSelection;
            } else if (hasSceneSelection) {
                selectedSpawnZoneIdx_ = -1;  // clear single-zone when scene is selected
            }
            bool hasSelection = (selectedSpawnZoneIdx_ >= 0) || hasSceneSelection;

            Mat4 vp = camera->getViewProjection();
            batch->begin(vp);

            for (int i = 0; i < (int)list.size(); ++i) {
                const auto& zone = list[i];
                if (!zone.is_object()) continue;

                std::string zoneScene;
                if (zone.contains("scene_id") && zone["scene_id"].is_string())
                    zoneScene = zone["scene_id"].get<std::string>();
                if (zoneScene != sceneId) continue;

                float cx = 0, cy = 0, r = 100, h = 0;
                if (zone.contains("center_x") && zone["center_x"].is_number()) cx = zone["center_x"].get<float>();
                if (zone.contains("center_y") && zone["center_y"].is_number()) cy = zone["center_y"].get<float>();
                if (zone.contains("radius") && zone["radius"].is_number()) r = zone["radius"].get<float>();
                if (zone.contains("height") && zone["height"].is_number()) h = zone["height"].get<float>();
                float halfW = r;
                float halfH = (h > 0) ? h : r;

                int targetCount = 3;
                if (zone.contains("target_count") && zone["target_count"].is_number())
                    targetCount = zone["target_count"].get<int>();

                bool isSelected = (i == selectedSpawnZoneIdx_) ||
                                   (hasSceneSelection && zoneScene == selectedScene);
                bool isUnpositioned = (cx == 0.0f && cy == 0.0f);

                // When a zone is selected, dim everything else heavily
                float dimFactor = (hasSelection && !isSelected) ? 0.15f : 1.0f;

                Color outlineColor;
                if (isSelected) {
                    outlineColor = Color(1.0f, 0.9f, 0.2f, 0.9f);
                } else if (isUnpositioned) {
                    outlineColor = Color(0.9f, 0.5f, 0.1f, 0.35f * dimFactor);
                } else if (targetCount <= 1) {
                    outlineColor = Color(0.9f, 0.3f, 0.3f, 0.45f * dimFactor);
                } else {
                    outlineColor = Color(0.3f, 0.8f, 0.9f, 0.4f * dimFactor);
                }

                Vec2 center = {cx, cy};
                std::string shape = "circle";
                if (zone.contains("zone_shape") && zone["zone_shape"].is_string())
                    shape = zone["zone_shape"].get<std::string>();

                float thickness = isSelected ? 2.5f : 1.0f;

                if (shape == "rectangle") {
                    if (isSelected) {
                        Color fillColor = outlineColor;
                        fillColor.a = 0.12f;
                        batch->drawRect(center, {halfW * 2.0f, halfH * 2.0f}, fillColor, 93.0f);
                    }
                    batch->drawRect({cx, cy - halfH}, {halfW * 2.0f, thickness}, outlineColor, 93.5f);
                    batch->drawRect({cx, cy + halfH}, {halfW * 2.0f, thickness}, outlineColor, 93.5f);
                    batch->drawRect({cx - halfW, cy}, {thickness, halfH * 2.0f}, outlineColor, 93.5f);
                    batch->drawRect({cx + halfW, cy}, {thickness, halfH * 2.0f}, outlineColor, 93.5f);
                } else {
                    // Circle/ellipse
                    if (isSelected) {
                        Color fillColor = outlineColor;
                        fillColor.a = 0.12f;
                        batch->drawCircle(center, r, fillColor, 93.0f, 32);
                    }
                    batch->drawRing(center, r, thickness, outlineColor, 93.5f, 32);
                }
            }

            batch->end();

            // Draw labels via ImGui overlay (screen-space text on top of viewport)
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            for (int i = 0; i < (int)list.size(); ++i) {
                const auto& zone = list[i];
                if (!zone.is_object()) continue;

                std::string zoneScene;
                if (zone.contains("scene_id") && zone["scene_id"].is_string())
                    zoneScene = zone["scene_id"].get<std::string>();
                if (zoneScene != sceneId) continue;

                float cx = 0, cy = 0;
                if (zone.contains("center_x") && zone["center_x"].is_number()) cx = zone["center_x"].get<float>();
                if (zone.contains("center_y") && zone["center_y"].is_number()) cy = zone["center_y"].get<float>();

                std::string mobId;
                if (zone.contains("mob_def_id") && zone["mob_def_id"].is_string())
                    mobId = zone["mob_def_id"].get<std::string>();

                int targetCount = 3;
                if (zone.contains("target_count") && zone["target_count"].is_number())
                    targetCount = zone["target_count"].get<int>();

                // Convert world pos to screen pos
                Vec2 screenPos = camera->worldToScreen({cx, cy},
                    (int)viewportSize_.x, (int)viewportSize_.y);
                float sx = viewportPos_.x + screenPos.x;
                float sy = viewportPos_.y + screenPos.y;

                // Check if on-screen
                if (sx < viewportPos_.x || sx > viewportPos_.x + viewportSize_.x) continue;
                if (sy < viewportPos_.y || sy > viewportPos_.y + viewportSize_.y) continue;

                char label[128];
                std::snprintf(label, sizeof(label), "%s x%d", mobId.c_str(), targetCount);

                ImVec2 textSize = ImGui::CalcTextSize(label);
                bool isSelected = (i == selectedSpawnZoneIdx_) ||
                                   (hasSceneSelection && zoneScene == selectedScene);
                // Dim unselected labels when something is selected
                uint8_t labelAlpha = (hasSelection && !isSelected) ? 40 : 180;
                ImU32 textCol = isSelected ? IM_COL32(255, 230, 50, 220)
                                           : IM_COL32(200, 255, 200, labelAlpha);
                ImU32 bgCol = isSelected ? IM_COL32(0, 0, 0, 160)
                                          : IM_COL32(0, 0, 0, (hasSelection ? 30 : 140));

                // Background rect behind text
                fg->AddRectFilled(
                    ImVec2(sx - textSize.x * 0.5f - 2, sy - textSize.y * 0.5f - 1),
                    ImVec2(sx + textSize.x * 0.5f + 2, sy + textSize.y * 0.5f + 1),
                    bgCol, 2.0f);
                fg->AddText(
                    ImVec2(sx - textSize.x * 0.5f, sy - textSize.y * 0.5f),
                    textCol, label);
            }
        }
    }
}

void Editor::renderUI(World* world, Camera* camera, SpriteBatch* batch, FrameArena* frameArena) {
    if (!frameStarted_) return;

    // ImGuizmo requires BeginFrame() once per ImGui frame
    ImGuizmo::BeginFrame();

    dockWorld_ = world;
    dockCamera_ = camera;
    refreshSelection(world);
    drawDockSpace();
    if (onDrawDockedGamePanels) onDrawDockedGamePanels();
    drawMenuBar(world);
    drawSceneViewport();
    // drawViewportHUD removed --coordinates now shown by FateStatusBar in the game HUD
    drawHierarchy(world);
    drawInspector();
    drawConsole(world);
    if (showCombatTextEditor_) {
        drawCombatTextEditorWindow(&showCombatTextEditor_);
    }
    if (showRoleNameplatesPanel_) drawRoleNameplatesPanel();
    if (showHotReloadPanel_) drawHotReloadPanel();
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
#ifdef FATE_HAS_GAME
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
#endif // FATE_HAS_GAME (UI editor panel)

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
                    dockWorld_->processDestroyQueue("editor_new_scene");
                    selectedEntity_ = nullptr;
                    selectedHandle_ = {};
                    currentScenePath_.clear();
                    LOG_INFO("Editor", "New scene");
                }
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Open Scene", !inPlayMode_ || paused_ || isObserving_)) {
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
            // Save --enabled when a scene path is set (from Open Scene or async load)
            const bool canSaveScene = !currentScenePath_.empty();
            if (ImGui::MenuItem("Save", "Ctrl+S", false, canSaveScene)) {
                if (saveScene(dockWorld_, currentScenePath_)) {
                    sceneDirty_ = false;
                } else {
                    LOG_WARN("Editor", "Menu save did not complete: %s", lastSaveStatus_.c_str());
                }
            }
            // Save As --always prompts for a new name
            if (ImGui::BeginMenu("Save As...", !inPlayMode_ || paused_)) {
                static char saveNameBuf[64] = "WhisperingWoods";
                ImGui::InputText("Name", saveNameBuf, sizeof(saveNameBuf));
                if (ImGui::Button("Save")) {
                    if (isValidAssetName(saveNameBuf)) {
                        std::string path = std::string("assets/scenes/") + saveNameBuf + ".json";
                        if (!saveScene(dockWorld_, path)) {
                            LOG_WARN("Editor", "Save As did not complete: %s", lastSaveStatus_.c_str());
                        }
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
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo)) {
                UndoSystem::instance().undo(dockWorld_);
                refreshSelection(dockWorld_);
#ifdef FATE_HAS_GAME
                if (uiManager_) uiEditorPanel_.revalidateSelection(*uiManager_);
#endif
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo)) {
                UndoSystem::instance().redo(dockWorld_);
                refreshSelection(dockWorld_);
#ifdef FATE_HAS_GAME
                if (uiManager_) uiEditorPanel_.revalidateSelection(*uiManager_);
#endif
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Grid Snap", nullptr, &gridSnap_);
            ImGui::DragFloat("Grid Size", &gridSize_, 1.0f, 8.0f, 128.0f);
            ImGui::Separator();
            ImGui::MenuItem("Show Grid", nullptr, &showGrid_);
            ImGui::MenuItem("Show Colliders", nullptr, &showCollisionDebug_);
            ImGui::MenuItem("Show Spawn Zones", nullptr, &showSpawnDebug_);
#ifdef FATE_HAS_GAME
            ImGui::MenuItem("Show AI Radii",      nullptr, &MobAIComponent::showAIRadiiGlobal);
            ImGui::MenuItem("Show Aggro Ledger",  nullptr, &MobAIComponent::showAggroLedgerGlobal);
            ImGui::MenuItem("Show Guard Patrol",  nullptr, &GuardComponent::showGuardPatrolGlobal);
#endif
            ImGui::Separator();
            ImGui::MenuItem("Post Process", nullptr, &showPostProcessPanel_);
            ImGui::Separator();
            ImGui::MenuItem("UI Hierarchy", nullptr, &uiEditorPanel_.showHierarchy);
            ImGui::MenuItem("UI Inspector", nullptr, &uiEditorPanel_.showInspector);
            if (uiManager_) {
                bool ttChrome = uiManager_->tooltipUseChrome();
                if (ImGui::MenuItem("Tooltip Chrome (Checkpoint 2)", nullptr, &ttChrome)) {
                    uiManager_->setTooltipUseChrome(ttChrome);
                }
                bool pnChrome = uiManager_->panelUseChrome();
                if (ImGui::MenuItem("Panel Chrome (Checkpoint 3)", nullptr, &pnChrome)) {
                    uiManager_->setPanelUseChrome(pnChrome);
                }
                bool shChrome = uiManager_->shopUseChrome();
                if (ImGui::MenuItem("Shop Chrome (Checkpoint 4)", nullptr, &shChrome)) {
                    uiManager_->setShopUseChrome(shChrome);
                }
                bool lgChrome = uiManager_->loginUseChrome();
                if (ImGui::MenuItem("Login Chrome (Checkpoint 6)", nullptr, &lgChrome)) {
                    uiManager_->setLoginUseChrome(lgChrome);
                }
                bool hudChrome = uiManager_->hudUseChrome();
                if (ImGui::MenuItem("HUD Chrome (Checkpoint 7)", nullptr, &hudChrome)) {
                    uiManager_->setHudUseChrome(hudChrome);
                }
            }
            if (netPanelOpen_) {
                ImGui::MenuItem("Network", nullptr, netPanelOpen_);
            }
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
            ImGui::MenuItem("Role Nameplates",    nullptr, &showRoleNameplatesPanel_);
            bool pdOpen = paperDollPanel_.isOpen();
            if (ImGui::MenuItem("Paper Doll Manager", nullptr, &pdOpen)) {
                paperDollPanel_.setOpen(pdOpen);
            }
            bool cbOpen = contentBrowserPanel_.isOpen();
            if (ImGui::MenuItem("Content Browser", nullptr, &cbOpen)) {
                contentBrowserPanel_.setOpen(cbOpen);
            }
            ImGui::MenuItem("Hot Reload", nullptr, &showHotReloadPanel_);
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
            toggleBtn("Spawns", &showSpawnDebug_);
            toggleBtn("Game UI", &showGameUI_);

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
                            // Snap gameplay zoom back to 1.0 — the player's view, not whatever the editor camera was at.
                            if (dockCamera_) {
                                dockCamera_->setZoom(1.0f);
                            }
                            paused_ = false;
                        }
                        ImGui::PopStyleColor();
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.50f, 0.20f, 1.00f));
                        if (ImGui::Button("Pause", ImVec2(playBtnW, btnH))) {
                            paused_ = true;
                        }
                        ImGui::PopStyleColor();
                    }

                    ImGui::SameLine();

                    // Stop --destroy runtime state, restore scene from snapshot
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

            // Admin Observer toggle (available when not in play mode)
            if (isObserving_) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.30f, 0.30f, 1.00f));
                if (ImGui::Button("Stop Obs", ImVec2(60.0f, btnH))) {
                    if (onObserveStop) onObserveStop();
                    isObserving_ = false;
                }
                ImGui::PopStyleColor(2);
                ImGui::SameLine();
            } else if (!inPlayMode_) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.35f, 0.55f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.45f, 0.65f, 1.00f));
                if (ImGui::Button("Observe", ImVec2(60.0f, btnH))) {
                    if (onObserveRequested) onObserveRequested();
                    isObserving_ = true;
                }
                ImGui::PopStyleColor(2);
                ImGui::SameLine();
            }

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

                        if (ImGui::Selectable(itemLabel, i == displayPresetIdx_)) {
                            // Pre-commit flush: if the new preset implies a
                            // different LayoutClass, persist any pending UI
                            // edits at their RECORDED variant path BEFORE we
                            // commit displayPresetIdx_ / safe area / registry.
                            // flushDirtyUIScreens uses each entry's recorded
                            // class (not current()), so this works without
                            // touching the registry. On failure we leave the
                            // preset where it was — the user keeps editing
                            // the existing tree, and the registry/safe-area
                            // block below stays consistent with the unchanged
                            // preset because it reads displayPresetIdx_.
                            fate::LayoutClass targetCls;
                            const auto& d = kDeviceProfiles[i];
                            if (d.width <= 0 || d.height <= 0) {
                                targetCls = fate::LayoutClass::Base;
                            } else {
                                targetCls = static_cast<fate::LayoutClass>(d.layoutClass);
                            }
                            bool flushOk = true;
                            if (targetCls != fate::LayoutClassRegistry::current()) {
                                flushOk = flushDirtyUIScreens();
                            }
                            if (flushOk) {
                                displayPresetIdx_ = i;
                            } else {
                                LOG_WARN("Editor",
                                    "Device switch refused: pending UI saves failed (%s)",
                                    lastSaveStatus_.c_str());
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

#ifdef FATE_HAS_GAME
                // Update simulated safe area + UI layout class when device changes.
                // Layout class drives variant JSON selection (foo.tablet.json /
                // foo.compact.json); when it changes we reload every loaded screen
                // so the editor preview matches what real players will see.
                {
                    const auto& selected = kDeviceProfiles[displayPresetIdx_];
                    fate::SafeAreaInsets insets;
                    insets.top    = selected.safeTop;
                    insets.bottom = selected.safeBottom;
                    insets.left   = selected.safeLeft;
                    insets.right  = selected.safeRight;
                    fate::setSimulatedSafeArea(insets);

                    // Free Aspect (no preset dimensions) classifies by panel aspect;
                    // every other entry uses the explicit layoutClass tag.
                    fate::LayoutClass cls;
                    if (selected.width <= 0 || selected.height <= 0) {
                        cls = fate::LayoutClass::Base;
                    } else {
                        cls = static_cast<fate::LayoutClass>(selected.layoutClass);
                    }
                    if (fate::LayoutClassRegistry::set(cls) && uiManager_) {
                        uiManager_->reloadAllScreensForLayoutClass();
                    }
                }
#endif

                ImGui::SameLine();
                ImGui::Checkbox("Safe Area", &showSafeAreaOverlay_);
            }

            ImGui::SameLine();

            // Scene dropdown (quick scene switcher)
            {
                // Extract current scene name from path
                std::string sceneName = "(none)";
                if (!currentScenePath_.empty()) {
                    size_t slash = currentScenePath_.find_last_of("/\\");
                    size_t dot = currentScenePath_.rfind('.');
                    size_t start = (slash != std::string::npos) ? slash + 1 : 0;
                    if (dot != std::string::npos && dot > start)
                        sceneName = currentScenePath_.substr(start, dot - start);
                    else
                        sceneName = currentScenePath_.substr(start);
                }

                bool canSwitch = !inPlayMode_ || paused_ || isObserving_;
                if (!canSwitch) ImGui::BeginDisabled();
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::BeginCombo("##Scene", sceneName.c_str(), ImGuiComboFlags_HeightLarge)) {
                    std::string scenesDir = "assets/scenes";
                    if (fs::exists(scenesDir)) {
                        for (auto& entry : fs::directory_iterator(scenesDir)) {
                            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
                            std::string name = entry.path().stem().string();
                            bool selected = (name == sceneName);
                            if (ImGui::Selectable(name.c_str(), selected)) {
                                loadScene(dockWorld_, entry.path().string());
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                if (!canSwitch) ImGui::EndDisabled();
                ImGui::SameLine();
            }

            // Right-aligned: FPS stats in muted gray
            {
                ImGuiIO& io = ImGui::GetIO();
                char stats[64];
                snprintf(stats, sizeof(stats), "%.0f FPS | %zu ent",
                         io.Framerate, dockWorld_ ? dockWorld_->entityCount() : 0u);
                if (fontSmall_) ImGui::PushFont(fontSmall_);
                float textW = ImGui::CalcTextSize(stats).x;
                float regionW = ImGui::GetContentRegionAvail().x;
                if (regionW > textW + 8.0f) {
                    ImGui::SameLine(ImGui::GetCursorPosX() + regionW - textW - 4.0f);
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
        bool useDeviceRes = preset.width > 0 && preset.height > 0;

        int fbW, fbH;
        if (useDeviceRes) {
            // Play mode with device preset --FBO at device resolution
            fbW = preset.width;
            fbH = preset.height;
        } else {
            // Edit mode or Free Aspect --FBO fills the panel
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
                    // Free aspect --image fills the panel
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

// drawViewportHUD removed --coordinates now shown by FateStatusBar in the game HUD

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

    // Paint-tool fallback: route to paintTileAt if a paint tool is active.
    // This catches clicks that bypass isTilePaintMode() (e.g. toolbar "Paint"
    // pressed before a tile is selected). paintTileAt guards internally and
    // returns early when nothing valid is selected, so behaviour is unchanged.
    if (currentTool_ == EditorTool::Paint   ||
        currentTool_ == EditorTool::Fill     ||
        currentTool_ == EditorTool::RectFill ||
        currentTool_ == EditorTool::LineTool) {
        paintTileAt(world, camera, screenPos, windowWidth, windowHeight);
        return;
    }

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
        // Detect prefab files: if the path ends with .json and is inside prefabs/,
        // derive the prefab name and spawn it via PrefabLibrary
        bool isPrefab = false;
        if (draggedAssetPath_.substr(0, 7) == "prefab:") {
            std::string prefabName = draggedAssetPath_.substr(7);
            entity = PrefabLibrary::instance().spawn(prefabName, *world, placePos);
            isPrefab = true;
        } else {
            // Check if this is a prefab .json file from the asset browser
            fs::path assetPath(draggedAssetPath_);
            std::string ext = assetPath.extension().string();
            std::string normalized = assetPath.generic_string(); // forward slashes
            LOG_INFO("Editor", "Prefab check: ext='%s' normalized='%s'", ext.c_str(), normalized.c_str());
            if (ext == ".json" && normalized.find("prefabs/") != std::string::npos) {
                size_t prefabStart = normalized.find("prefabs/") + 8;
                std::string prefabName = normalized.substr(prefabStart);
                prefabName = prefabName.substr(0, prefabName.size() - 5); // strip .json
                LOG_INFO("Editor", "Spawning prefab: '%s'", prefabName.c_str());
                entity = PrefabLibrary::instance().spawn(prefabName, *world, placePos);
                isPrefab = true;
                if (!entity) LOG_ERROR("Editor", "PrefabLibrary::spawn returned nullptr for '%s'", prefabName.c_str());
            }
        }
        if (!isPrefab) {
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

    // Flush pending spawn zone radius change on any new click
    if (spawnRadiusDirty_) {
        spawnRadiusDirty_ = false;
        auto& spawnList = contentBrowserPanel_.spawnListMut();
        if (selectedSpawnZoneIdx_ >= 0 && selectedSpawnZoneIdx_ < (int)spawnList.size()) {
            contentBrowserPanel_.saveSpawnZone(spawnList[selectedSpawnZoneIdx_]);
        }
    }

    // Spawn zone overlay: click to select/drag a DB spawn zone circle
    if (showSpawnDebug_ && currentTool_ == EditorTool::Move) {
        std::string sceneId = currentSceneId();
        auto& list = contentBrowserPanel_.spawnListMut();
        int hitIdx = -1;
        float hitDist = 99999.0f;

        for (int i = 0; i < (int)list.size(); ++i) {
            const auto& zone = list[i];
            if (!zone.is_object()) continue;
            std::string zs;
            if (zone.contains("scene_id") && zone["scene_id"].is_string())
                zs = zone["scene_id"].get<std::string>();
            if (zs != sceneId) continue;

            float cx = 0, cy = 0, r = 100;
            if (zone.contains("center_x") && zone["center_x"].is_number()) cx = zone["center_x"].get<float>();
            if (zone.contains("center_y") && zone["center_y"].is_number()) cy = zone["center_y"].get<float>();
            if (zone.contains("radius") && zone["radius"].is_number()) r = zone["radius"].get<float>();

            float dx = worldPos.x - cx;
            float dy = worldPos.y - cy;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= r && dist < hitDist) {
                hitIdx = i;
                hitDist = dist;
            }
        }

        if (hitIdx >= 0) {
            selectedSpawnZoneIdx_ = hitIdx;
            isDraggingSpawnZone_ = true;
            spawnDragStartWorld_ = worldPos;
            auto& zone = list[hitIdx];
            spawnDragStartCx_ = zone.contains("center_x") && zone["center_x"].is_number() ? zone["center_x"].get<float>() : 0.0f;
            spawnDragStartCy_ = zone.contains("center_y") && zone["center_y"].is_number() ? zone["center_y"].get<float>() : 0.0f;
            spawnRadiusBeforeResize_ = zone.contains("radius") && zone["radius"].is_number() ? zone["radius"].get<float>() : 100.0f;
            // Clear entity selection when selecting a spawn zone
            selectedEntity_ = nullptr;
            selectedHandle_ = {};
            isDraggingEntity_ = false;
            return;
        } else {
            selectedSpawnZoneIdx_ = -1;
        }
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

    // Click was outside the selected entity --select a new one
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
    // Spawn zone dragging (DB-backed circles)
    if (isDraggingSpawnZone_ && camera) {
        Vec2 worldPos = camera->screenToWorld(screenPos, windowWidth, windowHeight);
        Vec2 delta = worldPos - spawnDragStartWorld_;
        auto& list = contentBrowserPanel_.spawnListMut();
        if (selectedSpawnZoneIdx_ >= 0 && selectedSpawnZoneIdx_ < (int)list.size()) {
            auto& zone = list[selectedSpawnZoneIdx_];
            zone["center_x"] = spawnDragStartCx_ + delta.x;
            zone["center_y"] = spawnDragStartCy_ + delta.y;
        }
        return;
    }

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

// Forward declarations for tile tool helpers (defined in engine/editor/editor_tile.cpp)
std::unique_ptr<UndoCommand> paintOneTile(World* world,
    const Vec2& worldPos, int tileIndex, int paletteCols,
    int paletteTileSize, float gridSize,
    const std::shared_ptr<Texture>& paletteTexture,
    const std::string& paletteTexturePath,
    const std::string& tileLayer);
std::unique_ptr<UndoCommand> paintOneCollisionTile(World* world,
    const Vec2& worldPos, float gridSize);
Vec2 tileToWorldCenter(int col, int row, float gridSize);

void Editor::handleMouseUp() {
    // Save spawn zone position after drag
    if (isDraggingSpawnZone_) {
        isDraggingSpawnZone_ = false;
        auto& list = contentBrowserPanel_.spawnListMut();
        if (selectedSpawnZoneIdx_ >= 0 && selectedSpawnZoneIdx_ < (int)list.size()) {
            auto& zone = list[selectedSpawnZoneIdx_];
            float cx = zone.contains("center_x") && zone["center_x"].is_number() ? zone["center_x"].get<float>() : 0.0f;
            float cy = zone.contains("center_y") && zone["center_y"].is_number() ? zone["center_y"].get<float>() : 0.0f;
            // Only save if position actually changed
            if (cx != spawnDragStartCx_ || cy != spawnDragStartCy_) {
                contentBrowserPanel_.saveSpawnZone(zone);
            }
        }
        return;
    }

    // Save spawn zone radius after scroll-wheel resize
    if (spawnRadiusDirty_) {
        spawnRadiusDirty_ = false;
        auto& list = contentBrowserPanel_.spawnListMut();
        if (selectedSpawnZoneIdx_ >= 0 && selectedSpawnZoneIdx_ < (int)list.size()) {
            contentBrowserPanel_.saveSpawnZone(list[selectedSpawnZoneIdx_]);
        }
    }

    // Record undo for completed drag/resize
    if (selectedEntity_) {
        if (isDraggingEntity_) {
            auto* t = selectedEntity_->getComponent<Transform>();
            if (t && t->position != dragStartEntityPos_) {
                // Promote-on-edit: scene-viewport drag mirrors inspector edit.
                // See captureInspectorUndo for the reasoning.
                if (selectedEntity_->isReplicated()) {
                    selectedEntity_->setReplicated(false);
                }
                auto cmd = std::make_unique<MoveCommand>();
                cmd->entityHandle = selectedEntity_->handle();
                cmd->oldPos = dragStartEntityPos_;
                cmd->newPos = t->position;
                cmd->isPlayerPrefab = (selectedEntity_->tag() == "player");
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
                cmd->isPlayerPrefab = (selectedEntity_->tag() == "player");
                UndoSystem::instance().push(std::move(cmd));
            }
        }
    }
    isDraggingEntity_ = false;
    isResizingEntity_ = false;
    resizeHandle_ = -1;

    // Finalize RectFill / LineTool drag.
    // Collision-layer rect/line tools don't require a palette texture — they
    // stamp tinted squares like the brush does on the collision layer. Other
    // layers still need a selected tile + palette; surface a clear status if
    // those are missing instead of dropping the drag silently.
    if (isToolDragging_ && dockWorld_ &&
        (currentTool_ == EditorTool::RectFill || currentTool_ == EditorTool::LineTool)) {

        const bool isCollisionLayer = (selectedTileLayer_ == "collision");
        bool ready = isCollisionLayer
                   ? true
                   : (selectedTileIndex_ >= 0 && paletteTexture_ != nullptr);

        if (!ready) {
            if (!paletteTexture_) {
                lastToolStatus_ = "Rect/Line tool: no tileset loaded — open Tile Palette and pick one";
            } else {
                lastToolStatus_ = "Rect/Line tool: select a tile in the Tile Palette first";
            }
        } else {
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
                        ? paintOneCollisionTile(dockWorld_, wp, gridSize_)
                        : paintOneTile(dockWorld_, wp, selectedTileIndex_, paletteColumns_,
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

    // Flush pending brush stroke compound
    if (pendingBrushStroke_ && !pendingBrushStroke_->empty()) {
        pendingBrushStroke_->desc = "Paint (" +
            std::to_string(pendingBrushStroke_->commands.size()) + " tiles)";
        UndoSystem::instance().push(std::move(pendingBrushStroke_));
    }
    pendingBrushStroke_.reset();
}

bool Editor::handleSpawnZoneScroll(float scrollY) {
    if (!showSpawnDebug_ || selectedSpawnZoneIdx_ < 0) return false;

    // Only resize radius when holding Shift — otherwise let camera zoom normally
    if (!(ImGui::GetIO().KeyShift)) return false;

    auto& list = contentBrowserPanel_.spawnListMut();
    if (selectedSpawnZoneIdx_ >= (int)list.size()) return false;

    auto& zone = list[selectedSpawnZoneIdx_];
    float radius = zone.contains("radius") && zone["radius"].is_number()
        ? zone["radius"].get<float>() : 100.0f;

    float step = 8.0f;
    if (scrollY > 0) radius += step;
    else if (scrollY < 0) radius -= step;
    if (radius < 16.0f) radius = 16.0f;
    if (radius > 2000.0f) radius = 2000.0f;

    zone["radius"] = radius;
    spawnRadiusDirty_ = true;
    return true;
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

    // Build view matrix (camera space --translate by -camPos, scale by zoom)
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
        const bool selIsPlayer = (selectedEntity_->tag() == "player");
        if (t->position != oldPos) {
            auto cmd = std::make_unique<MoveCommand>();
            cmd->entityHandle = selectedEntity_->handle();
            cmd->oldPos = oldPos;
            cmd->newPos = t->position;
            cmd->isPlayerPrefab = selIsPlayer;
            UndoSystem::instance().push(std::move(cmd));
        }
        if (t->rotation != oldRot) {
            auto cmd = std::make_unique<RotateCommand>();
            cmd->entityHandle = selectedEntity_->handle();
            cmd->oldRotation = oldRot;
            cmd->newRotation = t->rotation;
            cmd->isPlayerPrefab = selIsPlayer;
            UndoSystem::instance().push(std::move(cmd));
        }
        if (t->scale != oldScale) {
            auto cmd = std::make_unique<ScaleCommand>();
            cmd->entityHandle = selectedEntity_->handle();
            cmd->oldScale = oldScale;
            cmd->newScale = t->scale;
            cmd->isPlayerPrefab = selIsPlayer;
            UndoSystem::instance().push(std::move(cmd));
        }
    }
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

    // Show save failures in the HUD so the designer notices even if they
    // weren't watching the log. Success path stays silent.
    float bottomY = io.DisplaySize.y - 42.0f;
    if (!lastSaveSucceeded_ && !lastSaveStatus_.empty()) {
        ImVec2 errPad(12.0f, 6.0f);
        ImGui::SetNextWindowPos(ImVec2(12.0f, bottomY));
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, errPad);
        ImGui::Begin("##HUD_SaveErr", nullptr, flags);
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", lastSaveStatus_.c_str());
        ImGui::End();
        ImGui::PopStyleVar(2);
        bottomY -= 32.0f;
    }

    // Tile-tool feedback ("no tileset", "select a tile", etc.). Stacked above
    // the save error so both are visible if both are active. Amber rather than
    // red — these are workflow hints, not failures.
    if (!lastToolStatus_.empty()) {
        ImVec2 hintPad(12.0f, 6.0f);
        ImGui::SetNextWindowPos(ImVec2(12.0f, bottomY));
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, hintPad);
        ImGui::Begin("##HUD_ToolStatus", nullptr, flags);
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", lastToolStatus_.c_str());
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
}

// ============================================================================
// Menu Bar
// ============================================================================

void Editor::drawMenuBar(World* world) {
    // Menu bar content is now integrated into the toolbar --this just handles the prefab popup

    if (openSavePrefab_) {
        ImGui::OpenPopup("SavePrefabPopup");
        // Clear any stale popup-local error from a prior open. Each
        // open-popup cycle starts fresh; failures only persist while the
        // modal is on screen.
        prefabPopupError_.clear();
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
        // Modal-local error display. Belongs to this popup only — kept
        // separate from lastSaveStatus_ / lastSaveSucceeded_ so:
        //   (a) a popup success doesn't silently clear a stale dirty-domain
        //       failure (UI screen / scene / player prefab) left by an
        //       earlier Ctrl+S;
        //   (b) a popup failure doesn't get stuck in the HUD strip after
        //       the user retries successfully — when retry runs, we just
        //       clear prefabPopupError_ and the modal closes.
        if (!prefabPopupError_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                               prefabPopupError_.c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(120, 0)) && isValidAssetName(prefabNameBuf)) {
            // PrefabLibrary::save() reports source-write failure via its
            // bool return. Success closes the popup; failure keeps it
            // open and shows the cause in prefabPopupError_ so the
            // designer can fix the underlying issue and retry.
            if (PrefabLibrary::instance().save(prefabNameBuf, selectedEntity_)) {
                LOG_INFO("Editor", "Prefab saved: %s", prefabNameBuf);
                prefabPopupError_.clear();
                prefabNameBuf[0] = '\0';
                ImGui::CloseCurrentPopup();
            } else {
                LOG_ERROR("Editor", "Prefab save FAILED: %s", prefabNameBuf);
                prefabPopupError_ = std::string("Save FAILED: ") + prefabNameBuf +
                                    " (runtime or source write rejected)";
                // Modal stays open; lastSaveStatus_ untouched so any
                // pending dirty-domain failure from Ctrl+S remains
                // visible in the HUD strip.
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            prefabPopupError_.clear();
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

        // Player anchor for distance sort + per-entry distance label.
        // Found once per frame; groups sort by distance only when expanded.
        Vec2 playerPos(0, 0);
        bool hasPlayer = false;
        if (Entity* player = world->findByTag("player")) {
            if (auto* t = player->getComponent<Transform>()) {
                playerPos = t->position;
                hasPlayer = true;
            }
        }

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
            std::string t = tag;
            std::transform(t.begin(), t.end(), t.begin(), ::tolower);
            if (t == "player") return {0.3f, 0.8f, 1.0f, 1.0f};
            if (t == "obstacle") return {1.0f, 0.6f, 0.3f, 1.0f};
            if (t == "ground") return {0.5f, 0.8f, 0.5f, 1.0f};
            if (t == "mob") return {1.0f, 0.4f, 0.4f, 1.0f};
            if (t == "boss") return {1.0f, 0.2f, 0.8f, 1.0f};
            if (t == "npc") return {0.7f, 0.9f, 0.4f, 1.0f};
            if (t == "interact_site") return {0.9f, 0.8f, 0.4f, 1.0f};
            if (t == "portal") return {0.6f, 0.5f, 1.0f, 1.0f};
            if (t == "spawn") return {0.5f, 0.7f, 0.9f, 1.0f};
            if (t == "pet") return {0.9f, 0.6f, 0.9f, 1.0f};
            return {0.8f, 0.8f, 0.8f, 1.0f};
        };

        // Bucket groups into categories so the panel reads top-down by purpose
        // rather than spawn order. World defaults OFF -- tiles/obstacles drown
        // out everything else when scenes have hundreds of them.
        enum CatIdx { CAT_PLAYER = 0, CAT_BOSSES, CAT_NPCS, CAT_PORTALS, CAT_MOBS, CAT_WORLD, CAT_OTHER, CAT_COUNT };
        static const char* kCatNames[CAT_COUNT] = { "Player", "Bosses", "NPCs & Sites", "Portals", "Mobs", "World", "Other" };
        static const char* kCatTagSample[CAT_COUNT] = { "player", "boss", "npc", "portal", "mob", "obstacle", "" };
        static bool kCatEnabled[CAT_COUNT] = { true, true, true, true, true, false, true };

        auto categoryFor = [](const std::string& tag, const std::string& name) -> int {
            // Lowercase compare -- scene files have both "Portal"/"portal", "Spawn"/"spawn"
            std::string t = tag;
            std::transform(t.begin(), t.end(), t.begin(), ::tolower);
            if (t == "player") return CAT_PLAYER;
            if (t == "boss") return CAT_BOSSES;
            if (t == "mob") return CAT_MOBS;
            if (t == "obstacle" || t == "ground") return CAT_WORLD;
            if (t == "portal") return CAT_PORTALS;
            if (t == "npc" || t == "interact_site" ||
                t == "spawn" || t == "pet") return CAT_NPCS;
            // Defensive name-prefix fallback for entities with missing/wrong tags
            // in scene data (e.g. Portal_To_SolisVillage shipped with tag="")
            if (name.rfind("Portal_", 0) == 0) return CAT_PORTALS;
            if (name == "MapSpawnPoint") return CAT_NPCS;
            return CAT_OTHER;
        };

        std::vector<GroupInfo*> bucketed[CAT_COUNT];
        size_t bucketTotals[CAT_COUNT] = {0};
        for (auto& g : groups) {
            int c = categoryFor(g.tag, g.name);
            bucketed[c].push_back(&g);
            bucketTotals[c] += g.entities.size();
        }
        for (auto& bucket : bucketed) {
            std::sort(bucket.begin(), bucket.end(),
                [](GroupInfo* a, GroupInfo* b) { return a->name < b->name; });
        }

        // Filter row: tinted checkbox label per category. Empty cats greyed out.
        for (int i = 0; i < CAT_COUNT; ++i) {
            if (i > 0) ImGui::SameLine(0, 8);
            bool empty = bucketed[i].empty();
            char id[16]; snprintf(id, sizeof(id), "##cat%d", i);
            if (empty) ImGui::BeginDisabled();
            ImGui::Checkbox(id, &kCatEnabled[i]);
            ImGui::SameLine(0, 3);
            ImVec4 col = getTagColor(kCatTagSample[i]);
            if (empty) col.w = 0.4f;
            ImGui::TextColored(col, "%s", kCatNames[i]);
            if (empty) ImGui::EndDisabled();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        for (int catIdx = 0; catIdx < CAT_COUNT; ++catIdx) {
            if (!kCatEnabled[catIdx]) continue;
            if (bucketed[catIdx].empty()) continue;

            char header[128];
            snprintf(header, sizeof(header), "%s  (%zu)##header%d",
                kCatNames[catIdx], bucketTotals[catIdx], catIdx);

            ImVec4 catCol = getTagColor(kCatTagSample[catIdx]);
            ImGui::PushStyleColor(ImGuiCol_Text, catCol);
            bool catOpen = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopStyleColor();
            if (!catOpen) continue;

        for (auto* groupPtr : bucketed[catIdx]) {
            auto& group = *groupPtr;
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
                // not SpriteComponent::texture --don't flag them as errors
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

                // Force-collapse fat groups by default so a 644-tile group
                // doesn't blow up the panel on first paint. User can still open.
                if (group.entities.size() > 40) {
                    ImGui::SetNextItemOpen(false, ImGuiCond_FirstUseEver);
                }
                bool open = ImGui::TreeNodeEx(groupLabel, groupFlags);
                if (hasTag) ImGui::PopStyleColor();

                if (open) {
                    // Sort children nearest-first relative to player. One std::sort per
                    // expanded group per frame -- microseconds for any realistic count.
                    if (hasPlayer) {
                        std::sort(group.entities.begin(), group.entities.end(),
                            [&playerPos](Entity* a, Entity* b) {
                                auto* ta = a->getComponent<Transform>();
                                auto* tb = b->getComponent<Transform>();
                                if (!ta && !tb) return false;
                                if (!ta) return false;
                                if (!tb) return true;
                                return (ta->position - playerPos).lengthSq()
                                     < (tb->position - playerPos).lengthSq();
                            });
                    }

                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float indentX = ImGui::GetCursorScreenPos().x - ImGui::GetStyle().IndentSpacing * 0.5f + 4.0f;
                    float startY = ImGui::GetCursorScreenPos().y;

                    for (auto* entity : group.entities) {
                        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                                 | ImGuiTreeNodeFlags_SpanAvailWidth;
                        if (entity == selectedEntity_) flags |= ImGuiTreeNodeFlags_Selected;

                        auto* t = entity->getComponent<Transform>();
                        if (t && hasPlayer) {
                            float distTiles = (t->position - playerPos).length() / 32.0f;
                            ImGui::TreeNodeEx((void*)(intptr_t)entity->id(), flags,
                                "%s  (%d, %d)  %.0ft",
                                entity->name().c_str(),
                                (int)t->position.x, (int)t->position.y, distTiles);
                        } else if (t) {
                            ImGui::TreeNodeEx((void*)(intptr_t)entity->id(), flags,
                                "%s  (%d, %d)",
                                entity->name().c_str(),
                                (int)t->position.x, (int)t->position.y);
                        } else {
                            ImGui::TreeNodeEx((void*)(intptr_t)entity->id(), flags, "%s", entity->name().c_str());
                        }

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
    }
    ImGui::End();
}

// Inspector methods moved to editor_inspector.cpp
// (drawReflectedComponent, captureInspectorUndo, drawInspector)


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

    if (!open_) return;

    auto scancode = event.key.keysym.scancode;
    bool ctrl = (event.key.keysym.mod & KMOD_CTRL) != 0;
    bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;

    // Non-modifier shortcuts (W/E/R/B/X/Delete) only fire when paused.
    // In Play mode the game owns the keyboard --tool switching would
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
#ifdef FATE_HAS_GAME
        if (uiManager_) uiEditorPanel_.revalidateSelection(*uiManager_);
#endif
    }
    // Ctrl+Y or Ctrl+Shift+Z = Redo
    if ((ctrl && scancode == SDL_SCANCODE_Y) ||
        (ctrl && shift && scancode == SDL_SCANCODE_Z)) {
        UndoSystem::instance().redo(world);
        refreshSelection(world);
#ifdef FATE_HAS_GAME
        if (uiManager_) uiEditorPanel_.revalidateSelection(*uiManager_);
#endif
    }
    // Ctrl+S = save every dirty domain (UI / scene / player prefab) as
    // independent branches, each clearing its own dirty state only after the
    // intended write actually succeeded. Pre-fix this block fired all three
    // saves on every keystroke; that's the bug that wrote unrelated prefab +
    // scene + UI files when only a UI offset had changed.
    if (ctrl && scancode == SDL_SCANCODE_S) {
        // Track per-domain success across all three branches. A later
        // success must NOT overwrite an earlier failure's status — the
        // designer needs to see that *something* didn't get to disk, even
        // if other domains did. We also need `attempted` so an aggregate
        // success on retry can clear a stale failure HUD: once a Ctrl+S
        // touches at least one dirty domain and every domain it touched
        // succeeded, the prior failure is no longer current.
        bool overallOk = true;
        bool attempted = false;
        std::string firstFailureStatus;

        auto recordFailure = [&](const std::string& status) {
            if (overallOk) firstFailureStatus = status;
            overallOk = false;
        };

#ifdef FATE_HAS_GAME
        // UI: save *every* dirty (screenId, class) pair, regardless of the
        // current selection or active device class. flushDirtyUIScreens
        // also runs from UIManager::beforeReloadCallback on device switch.
        // Note: UISerializer::serializeScreen still emits the entire live
        // screen tree — runtime-only fields like `visible` and `activeTab`
        // therefore appear in the diff alongside authored offset changes.
        // That's a UISerializer concern, not gating; tracked separately.
        if (!dirtyScreens_.empty()) {
            attempted = true;
            if (!flushDirtyUIScreens()) {
                // Capture the per-screen failure that flushDirtyUIScreens
                // already wrote into lastSaveStatus_ before any later
                // branch can overwrite it. The dirty bits for failed
                // screens stay set so the next Ctrl+S retries.
                recordFailure(lastSaveStatus_);
            }
        }
#endif // FATE_HAS_GAME

        // Player prefab: independent of scene save. Use flushDirtyPlayerPrefab
        // for symmetry with the scene-load path — same logic, same failure
        // semantics (false return means dirty bit stays set).
        if (playerPrefabDirty_) {
            attempted = true;
            if (!flushDirtyPlayerPrefab(world)) {
                recordFailure(lastSaveStatus_);
            }
        }

        // Scene. Allowed in play mode too — saveScene's isReplicated() +
        // tag-fallback filter keeps runtime ghosts (mobs, pets, dropped
        // items, server-replicated NPCs) out of the .json.
        if (sceneDirty_) {
            attempted = true;
            if (!currentScenePath_.empty()) {
                if (saveScene(world, currentScenePath_)) {
                    LOG_INFO("Editor", "Ctrl+S: saved scene to %s", currentScenePath_.c_str());
                    sceneDirty_ = false;
                } else {
                    LOG_ERROR("Editor", "Ctrl+S: scene save did not complete — %s", lastSaveStatus_.c_str());
                    recordFailure(lastSaveStatus_);
                }
            } else {
                // New scene with no path — surface in HUD too so it's not just a
                // log line the user has to dig for. Reuses the save-error HUD.
                std::string status = "Save As required: this scene has no path yet (File > Save As...)";
                LOG_WARN("Editor", "Ctrl+S: %s", status.c_str());
                recordFailure(status);
            }
        }

        // Commit the aggregate result.
        // - Aggregate failure: capture the first-failure status (so the
        //   designer sees the domain that actually broke) and mark
        //   lastSaveSucceeded_ false. A later success in this same Ctrl+S
        //   cannot overwrite the failure.
        // - Aggregate success after at least one attempt: clear any stale
        //   failure HUD from a previous Ctrl+S. Without this, a one-time
        //   I/O glitch would leave the HUD red even after the user
        //   successfully retried and every dirty domain saved.
        // - No domain attempted (nothing dirty): leave HUD alone. Silent
        //   no-op — designers press Ctrl+S reflexively and a HUD message
        //   every time would be worse than nothing.
        if (!overallOk) {
            lastSaveStatus_ = firstFailureStatus;
            lastSaveSucceeded_ = false;
        } else if (attempted) {
            lastSaveStatus_.clear();
            lastSaveSucceeded_ = true;
        }
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
    // W = Move tool (paused only --W is move-up in Play mode)
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

#else // !FATE_HAS_GAME --demo build with engine-only panels

void Editor::renderScene(SpriteBatch* batch, Camera* camera) {
    if (!open_ || !batch || !camera) return;

    if (paused_ && dockWorld_) {
        applyLayerVisibility(dockWorld_);
    }

    // Render entity sprites first so overlays draw on top.
    renderTilemap(batch, camera);

    if (showGrid_ && paused_) {
        drawSceneGridShader(camera);
        if (!gridShaderLoaded_) {
            drawSceneGrid(batch, camera);
        }
    }

    if (paused_) {
        drawSelectionOutlines(batch, camera);
        drawTileBrushPreview(batch, camera);
    }
}
void Editor::renderUI(World* world, Camera* camera, SpriteBatch*, FrameArena* frameArena) {
    if (!frameStarted_) return;

    dockWorld_ = world;
    dockCamera_ = camera;

    drawDockSpace();
    drawMenuBar(world);
    drawSceneViewport();   // always render — this is "what we observe"
    if (!editorChromeHidden_) {
        drawHierarchy(world);
        drawInspector();
        drawDebugInfoPanel(world);
        LogViewer::instance().draw();
        drawAssetBrowser(world, nullptr);
        dialogueEditor_.draw();
        animationEditor_.draw();
        if (showTilePalette_) {
            drawTilePalette(world, dockCamera_);
        }
        if (showHotReloadPanel_) drawHotReloadPanel();

#ifdef FATE_HAS_GAME
        if (uiManager_) {
            uiEditorPanel_.draw(*uiManager_);
        }
#endif
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
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                  ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("##DockSpaceHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");

    if (resetLayout_ || ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        resetLayout_ = false;
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID dockMain = dockspaceId;
        ImGuiID dockLeft   = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.15f, nullptr, &dockMain);
        ImGuiID dockRight  = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.22f, nullptr, &dockMain);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.22f, nullptr, &dockMain);

        ImGui::DockBuilderDockWindow("Hierarchy",  dockLeft);
        ImGui::DockBuilderDockWindow("Scene",      dockMain);
        ImGui::DockBuilderDockWindow("Inspector",  dockRight);
        ImGui::DockBuilderDockWindow("Project",    dockBottom);
        ImGui::DockBuilderDockWindow("Log",        dockBottom);
        ImGui::DockBuilderDockWindow("Debug Info", dockBottom);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}
void Editor::drawMenuBar(World*) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", nullptr, false, !inPlayMode_)) {
                if (dockWorld_) {
                    dockWorld_->forEachEntity([&](Entity* e) {
                        dockWorld_->destroyEntity(e->handle());
                    });
                    dockWorld_->processDestroyQueue("editor_new_scene");
                    selectedEntity_ = nullptr;
                    selectedHandle_ = {};
                    currentScenePath_.clear();
                    LOG_INFO("Editor", "New scene");
                }
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Open Scene", !inPlayMode_ || paused_ || isObserving_)) {
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
            // Save -- enabled when a scene path is set (from Open Scene or async load)
            const bool canSaveScene = !currentScenePath_.empty();
            if (ImGui::MenuItem("Save", "Ctrl+S", false, canSaveScene)) {
                if (saveScene(dockWorld_, currentScenePath_)) {
                    sceneDirty_ = false;
                } else {
                    LOG_WARN("Editor", "Menu save did not complete: %s", lastSaveStatus_.c_str());
                }
            }
            // Save As -- always prompts for a new name
            if (ImGui::BeginMenu("Save As...", !inPlayMode_ || paused_)) {
                static char saveNameBuf[64] = "NewScene";
                ImGui::InputText("Name", saveNameBuf, sizeof(saveNameBuf));
                if (ImGui::Button("Save")) {
                    if (isValidAssetName(saveNameBuf)) {
                        std::string path = std::string("assets/scenes/") + saveNameBuf + ".json";
                        if (!saveScene(dockWorld_, path)) {
                            LOG_WARN("Editor", "Save As did not complete: %s", lastSaveStatus_.c_str());
                        }
                        ImGui::CloseCurrentPopup();
                    } else {
                        LOG_WARN("Editor", "Invalid scene name: %s", saveNameBuf);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Tile Palette", nullptr, &showTilePalette_);
            bool animOpen = animationEditor_.isOpen();
            if (ImGui::MenuItem("Animation Editor", nullptr, &animOpen)) {
                animationEditor_.setOpen(animOpen);
            }
            ImGui::Separator();
            ImGui::MenuItem("Post Process", nullptr, &showPostProcessPanel_);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) { resetLayout_ = true; }
            ImGui::Separator();
            ImGui::MenuItem("Hot Reload", nullptr, &showHotReloadPanel_);
            ImGui::MenuItem("ImGui Demo", nullptr, &showDemoWindow_);
#if defined(ENGINE_MEMORY_DEBUG)
            ImGui::MenuItem("Memory", nullptr, &showMemoryPanel_);
#endif
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("About")) {
            ImGui::Text("FateMMO Engine v3.0");
            ImGui::Separator();
            ImGui::Text("C++23 | OpenGL 3.3 | Custom UDP");
            ImGui::Text("github.com/wFate/FateMMO_GameEngine");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}
void Editor::drawSceneViewport() {
    namespace fs = std::filesystem;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("Scene")) {
        // ---- Toolbar ----
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 3.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.145f, 0.145f, 0.157f, 1.00f));

            float toolbarHeight = ImGui::GetFrameHeight() + 6.0f;
            ImGui::BeginChild("##ViewportToolbar", ImVec2(0, toolbarHeight), false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            float btnH = ImGui::GetFrameHeight();
            float btnW = 60.0f;

            // Play / Resume / Pause / Stop
            if (!inPlayMode_) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.50f, 0.20f, 1.00f));
                if (ImGui::Button("Play", ImVec2(btnW, btnH))) {
                    if (dockCamera_) {
                        savedCamPos_ = dockCamera_->position();
                        savedCamZoom_ = dockCamera_->zoom();
                        dockCamera_->setZoom(1.0f);
                    }
                    enterPlayMode(dockWorld_);
                }
                ImGui::PopStyleColor();
            } else {
                if (paused_) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.50f, 0.20f, 1.00f));
                    if (ImGui::Button("Resume", ImVec2(btnW, btnH))) {
                        if (dockCamera_) {
                            dockCamera_->setZoom(1.0f);
                        }
                        paused_ = false;
                    }
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.50f, 0.20f, 1.00f));
                    if (ImGui::Button("Pause", ImVec2(btnW, btnH))) {
                        paused_ = true;
                    }
                    ImGui::PopStyleColor();
                }
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.20f, 0.20f, 1.00f));
                if (ImGui::Button("Stop", ImVec2(btnW, btnH))) {
                    if (dockCamera_) {
                        dockCamera_->setPosition(savedCamPos_);
                        dockCamera_->setZoom(savedCamZoom_);
                    }
                    exitPlayMode(dockWorld_);
                }
                ImGui::PopStyleColor();
            }

            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            // Observe / Stop Obs
            if (isObserving_) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.30f, 0.30f, 1.00f));
                if (ImGui::Button("Stop Obs", ImVec2(70.0f, btnH))) {
                    if (onObserveStop) onObserveStop();
                    isObserving_ = false;
                }
                ImGui::PopStyleColor(2);
            } else if (!inPlayMode_) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.35f, 0.55f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.45f, 0.65f, 1.00f));
                if (ImGui::Button("Observe", ImVec2(70.0f, btnH))) {
                    if (onObserveRequested) onObserveRequested();
                    isObserving_ = true;
                }
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Run scene live without snapshot. Override via AppConfig::onObserveStart.");
                }
            }

            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            // Scene dropdown
            {
                std::string sceneName = "(none)";
                if (!currentScenePath_.empty()) {
                    size_t slash = currentScenePath_.find_last_of("/\\");
                    size_t dot = currentScenePath_.rfind('.');
                    size_t start = (slash != std::string::npos) ? slash + 1 : 0;
                    if (dot != std::string::npos && dot > start)
                        sceneName = currentScenePath_.substr(start, dot - start);
                    else
                        sceneName = currentScenePath_.substr(start);
                }
                bool canSwitch = !inPlayMode_ || paused_ || isObserving_;
                if (!canSwitch) ImGui::BeginDisabled();
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::BeginCombo("##Scene", sceneName.c_str(), ImGuiComboFlags_HeightLarge)) {
                    std::string scenesDir = "assets/scenes";
                    if (fs::exists(scenesDir)) {
                        for (auto& entry : fs::directory_iterator(scenesDir)) {
                            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
                            std::string name = entry.path().stem().string();
                            bool selected = (name == sceneName);
                            if (ImGui::Selectable(name.c_str(), selected)) {
                                loadScene(dockWorld_, entry.path().string());
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                if (!canSwitch) ImGui::EndDisabled();
            }

            // Right-aligned FPS readout
            {
                ImGuiIO& io = ImGui::GetIO();
                char stats[64];
                snprintf(stats, sizeof(stats), "%.0f FPS", io.Framerate);
                float textW = ImGui::CalcTextSize(stats).x;
                float regionW = ImGui::GetContentRegionAvail().x;
                if (regionW > textW + 8.0f) {
                    ImGui::SameLine(ImGui::GetCursorPosX() + regionW - textW - 4.0f);
                    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.50f, 1.0f), "%s", stats);
                }
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }

        // ---- FBO viewport image ----
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
void Editor::handleSceneClick(World* world, Camera* camera, const Vec2& screenPos,
                              int windowWidth, int windowHeight) {
    dispatchTileSceneClick(world, camera, screenPos, windowWidth, windowHeight);
}
void Editor::handleSceneDrag(Camera* camera, const Vec2& screenPos,
                             int windowWidth, int windowHeight) {
    dispatchTileSceneDrag(camera, screenPos, windowWidth, windowHeight);
}
void Editor::handleMouseUp() {
    finishTileMouseUp(dockWorld_);
}
bool Editor::handleSpawnZoneScroll(float) { return false; }
void Editor::scanAssets() {}
void Editor::drawAssetBrowser(World* world, Camera* camera) {
    if (ImGui::Begin("Project")) {
        assetBrowser_.draw(world, camera);
    }
    ImGui::End();
}
void Editor::drawImGuizmo(Camera*) {}
void Editor::drawHUD(World*) {}
void Editor::drawToolbar(World*) {}
void Editor::drawConsole(World*) {}
void Editor::executeCommand(World*, const std::string&) {}
void Editor::handleKeyShortcuts(World* world, const SDL_Event& event) {
    if (!world) return;
    if (event.type != SDL_KEYDOWN) return;

    auto scancode = event.key.keysym.scancode;
    bool ctrl = (event.key.keysym.mod & KMOD_CTRL) != 0;

    // Ctrl+S = Save current scene
    if (ctrl && scancode == SDL_SCANCODE_S && !inPlayMode_ && !currentScenePath_.empty()) {
        if (!saveScene(world, currentScenePath_)) {
            LOG_WARN("Editor", "Ctrl+S: save did not complete -- %s", lastSaveStatus_.c_str());
        }
    }
}

#endif // FATE_HAS_GAME

// ============================================================================
// Play-in-Editor: Snapshot / Restore (compiled in BOTH demo and game builds)
// ============================================================================

void Editor::enterPlayMode(World* world) {
    if (inPlayMode_ || !world) return;

    try {
        playModeSnapshot_ = nlohmann::json::array();
        world->forEachEntity([&](Entity* e) {
            // Skip transient runtime entities -- same filter as saveScene
            std::string tag = e->tag();
            if (tag == "mob" || tag == "boss" || tag == "player" ||
                tag == "ghost" || tag == "dropped_item" || tag == "pet") return;
            playModeSnapshot_.push_back(PrefabLibrary::entityToJson(e));
        });
    } catch (const std::exception& ex) {
        LOG_ERROR("Editor", "enterPlayMode snapshot failed: %s", ex.what());
        playModeSnapshot_ = nlohmann::json();
        return;
    }

#ifdef FATE_HAS_GAME
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

#if FATE_ENABLE_HOT_RELOAD
    // Snapshot restore destroys every entity in this world and recreates
    // them from JSON. Tear down any bound behaviors first so the module
    // sees onDestroy with valid handles + state pointers, before the
    // archetype storage is reorganized.
    HotReloadManager::instance().onWorldUnload(*world);
#endif

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
    try {
        for (auto& entityJson : playModeSnapshot_) {
            PrefabLibrary::jsonToEntity(entityJson, *world);
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("Editor", "exitPlayMode restore failed: %s", ex.what());
    }
    playModeSnapshot_ = nlohmann::json();

    paused_ = true;
    inPlayMode_ = false;

    // Clear selection (entities were recreated with new handles)
    clearSelection();
}

// ============================================================================
// Observer mode default implementation (compiled in BOTH demo and game builds)
// ============================================================================

void Editor::beginLocalObserve() {
    if (dockCamera_) {
        savedCamPos_ = dockCamera_->position();
        savedCamZoom_ = dockCamera_->zoom();
    }
    paused_ = false;
    editorChromeHidden_ = true;
}

void Editor::endLocalObserve() {
    if (dockCamera_) {
        dockCamera_->setPosition(savedCamPos_);
        dockCamera_->setZoom(savedCamZoom_);
    }
    paused_ = true;
    editorChromeHidden_ = false;
}

} // namespace fate
