#pragma once
#include "engine/render/sprite_batch.h"
#include "engine/render/camera.h"
#include "engine/render/render_graph.h"
#include "engine/render/lighting.h"
#include "engine/render/post_process.h"
#include "engine/render/fullscreen_quad.h"
#include "engine/scene/scene_manager.h"
#include "engine/editor/editor.h"
#include "engine/input/input.h"
#include "engine/asset/file_watcher.h"
#include "engine/asset/asset_registry.h"
#include "engine/asset/loaders.h"
#include "engine/memory/arena.h"
#include <SDL.h>
#include <string>

namespace fate {

struct AppConfig {
    std::string title = "FateEngine";
    int windowWidth = 1600;
    int windowHeight = 900;
    bool fullscreen = false;
    bool vsync = true;
    int targetFPS = 60;
    float fixedTimestep = 1.0f / 30.0f; // 30Hz fixed update
    std::string assetsDir;  // absolute path to assets/ directory (set by game)
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
    virtual void onFixedUpdate(float fixedDeltaTime) {}
    virtual void onRender(SpriteBatch& batch, Camera& camera) {}
    virtual void onShutdown() {}

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

protected:
    AppConfig config_;

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext glContext_ = nullptr;
    SpriteBatch spriteBatch_;
    Camera camera_;
    RenderGraph renderGraph_;
    LightingConfig lightingConfig_;
    PostProcessConfig postProcessConfig_;

    bool running_ = false;
    float deltaTime_ = 0.0f;
    float fps_ = 0.0f;
    float fixedTimeAccumulator_ = 0.0f;
    FrameArena frameArena_;
    FileWatcher fileWatcher_;
    float elapsedTime_ = 0.0f;  // for reload debounce timestamps
    std::string assetsDir_;     // cached from config

    void processEvents();
    void update();
    void render();
    void shutdown();
};

} // namespace fate
