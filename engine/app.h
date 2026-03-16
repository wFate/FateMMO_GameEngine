#pragma once
#include "engine/render/sprite_batch.h"
#include "engine/render/camera.h"
#include "engine/scene/scene_manager.h"
#include "engine/editor/editor.h"
#include "engine/input/input.h"
#include <SDL.h>
#include <string>

namespace fate {

struct AppConfig {
    std::string title = "FateEngine";
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;
    bool vsync = true;
    int targetFPS = 60;
    float fixedTimestep = 1.0f / 30.0f; // 30Hz fixed update
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

protected:
    AppConfig config_;

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext glContext_ = nullptr;
    SpriteBatch spriteBatch_;
    Camera camera_;

    bool running_ = false;
    float deltaTime_ = 0.0f;
    float fps_ = 0.0f;
    float fixedTimeAccumulator_ = 0.0f;

    void processEvents();
    void update();
    void render();
    void shutdown();
};

} // namespace fate
