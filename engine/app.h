#pragma once
#include "engine/render/sprite_batch.h"
#include "engine/render/camera.h"
#include "engine/render/render_graph.h"
#include "engine/render/lighting.h"
#include "engine/render/post_process.h"
#include "engine/render/fullscreen_quad.h"
#include "engine/render/loading_screen.h"
#include "engine/scene/scene_manager.h"
#ifndef FATE_SHIPPING
#include "engine/editor/editor.h"
#endif
#include "engine/input/input.h"
#include "engine/asset/file_watcher.h"
#include "engine/asset/asset_registry.h"
#include "engine/asset/loaders.h"
#include "engine/module/hot_reload_manager.h"
#ifdef FATE_HAS_GAME
#include "engine/ui/ui_manager.h"
#else
#include "engine/ui_stubs.h"
#endif
#include "engine/memory/arena.h"
#include <SDL.h>
#include <atomic>
#include <string>
#include <functional>

namespace fate {

struct AppConfig {
    std::string title = "FateEngine";
    int windowWidth = 1600;
    int windowHeight = 900;
    bool fullscreen = false;
    bool vsync = true;
    int targetFPS = 0; // 0 = auto-detect from display refresh rate
    float fixedTimestep = 1.0f / 30.0f; // 30Hz fixed update
    std::string assetsDir;  // absolute path to assets/ directory (set by game)

    // Optional Observer hooks. Leave unset for the default local-preview
    // behavior (run systems live, hide editor chrome). Set both to wire
    // your own implementation (e.g., network spectate).
    std::function<void()> onObserveStart;
    std::function<void()> onObserveStop;
};

enum class AppLifecycleState {
    Active,         // Game is running normally
    Background,     // App is backgrounded (mobile) or minimized (desktop)
    Suspended       // App is about to be terminated (iOS only)
};

// Base application class - game inherits from this
class App {
public:
    App() = default;
    virtual ~App();

    bool init(const AppConfig& config = AppConfig());
    void run();
    void quit();

    // Override these in your game
    virtual void onInit() {}
    virtual void onUpdate(float deltaTime) {}
    virtual void onLoadingUpdate(float deltaTime) {}   // called even while isLoading_
    virtual void onFixedUpdate(float fixedDeltaTime) {}
    virtual void onRender(SpriteBatch& batch, Camera& camera) {}
    virtual void onShutdown() {}

    // Mobile lifecycle callbacks — override in game for custom behavior
    virtual void onEnterBackground() {}
    virtual void onEnterForeground() {}
    virtual void onLowMemory() {}

    // Lifecycle state queries
    AppLifecycleState lifecycleState() const { return lifecycleState_; }
    bool isBackgrounded() const { return lifecycleState_ != AppLifecycleState::Active; }

    // Public for testing — dispatches lifecycle events to state + callbacks
    void handleLifecycleEvent(const SDL_Event& event);

    // Accessors
    SpriteBatch& spriteBatch() { return spriteBatch_; }
    Camera& camera() { return camera_; }
    SDL_Window* window() { return window_; }
    int windowWidth() const { return config_.windowWidth; }
    int windowHeight() const { return config_.windowHeight; }
    float deltaTime() const { return deltaTime_; }
    float fps() const { return fps_; }
    bool isRunning() const { return running_; }
    FrameArena& frameArena() { return frameArena_; }
    RenderGraph& renderGraph() { return renderGraph_; }
    LightingConfig& lightingConfig() { return lightingConfig_; }
    PostProcessConfig& postProcessConfig() { return postProcessConfig_; }
    UIManager& uiManager() { return uiManager_; }
    void setLoadingScreen(LoadingScreen* ls) { loadingScreen_ = ls; }
    void setLoadingProgress(float p) { loadingProgress_ = p; }
    void setIsLoading(bool loading) { isLoading_ = loading; }
    bool isLoading() const { return isLoading_; }

protected:
    AppConfig config_;

private:
    SDL_Window* window_ = nullptr;
#ifndef FATEMMO_METAL
    SDL_GLContext glContext_ = nullptr;
#endif
#ifdef FATEMMO_METAL
    void* metalView_ = nullptr;    // SDL_MetalView
    void* metalLayer_ = nullptr;   // CAMetalLayer*
#endif
    SpriteBatch spriteBatch_;
    Camera camera_;
    RenderGraph renderGraph_;
    LightingConfig lightingConfig_;
    PostProcessConfig postProcessConfig_;

    bool running_ = false;
    bool shutdownComplete_ = false;
    bool sdlInitialized_ = false;
    float deltaTime_ = 0.0f;
    float fps_ = 0.0f;

    // Rolling frame-time stats. Logs max/avg over every 5s window if any
    // frame exceeded 25ms (i.e. drops below 40 FPS equivalent). Catches
    // periodic hitches without spamming per-frame logs.
    float frameStatsFlushTimer_ = 0.0f;
    float frameStatsMaxMs_ = 0.0f;
    float frameStatsSumMs_ = 0.0f;
    int   frameStatsCount_ = 0;
    int   frameStatsOver25msCount_ = 0;
    // Per-phase max within window (update/render) so we can tell whether
    // hitches come from game logic vs GL rendering.
    float frameStatsMaxUpdateMs_ = 0.0f;
    float frameStatsMaxRenderMs_ = 0.0f;
    float fixedTimeAccumulator_ = 0.0f;
    FrameArena frameArena_;
    UIManager uiManager_;
    FileWatcher fileWatcher_;
    std::atomic<bool> themeReloadPending_{false};
    float themeReloadRequestedAt_ = 0.0f;  // elapsedTime when pending was flipped on (debounce)
    float elapsedTime_ = 0.0f;  // for reload debounce timestamps
    std::string assetsDir_;     // cached from config
    AppLifecycleState lifecycleState_ = AppLifecycleState::Active;
    LoadingScreen* loadingScreen_ = nullptr;
    float loadingProgress_ = 0.0f;
    bool isLoading_ = false;
    static int SDLCALL lifecycleEventFilter(void* userdata, SDL_Event* event);

    void processEvents();
    void update();
    void render();
    void shutdown();
#ifdef FATE_HAS_GAME
    void reloadGameTheme_();
#endif
};

} // namespace fate
