#include "engine/app.h"
#include "engine/render/gl_loader.h"
#include "engine/core/logger.h"
#include <SDL.h>

namespace fate {

App::~App() {
    shutdown();
}

bool App::init(const AppConfig& config) {
    config_ = config;

    Logger::instance().init("fate_engine.log");
    LOG_INFO("App", "=== FateEngine v%d.%d.%d starting ===", 0, 1, 0);
    LOG_INFO("App", "Window: %dx%d, VSync: %s, Target FPS: %d",
             config.windowWidth, config.windowHeight,
             config.vsync ? "ON" : "OFF", config.targetFPS);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        LOG_FATAL("App", "SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (config.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    window_ = SDL_CreateWindow(
        config.title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        config.windowWidth, config.windowHeight,
        flags
    );
    if (!window_) {
        LOG_FATAL("App", "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    glContext_ = SDL_GL_CreateContext(window_);
    if (!glContext_) {
        LOG_FATAL("App", "SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetSwapInterval(config.vsync ? 1 : 0);

    if (!loadGLFunctions()) {
        LOG_FATAL("App", "Failed to load OpenGL functions");
        return false;
    }

    LOG_INFO("App", "OpenGL Vendor:   %s", glGetString(GL_VENDOR));
    LOG_INFO("App", "OpenGL Renderer: %s", glGetString(GL_RENDERER));
    LOG_INFO("App", "OpenGL Version:  %s", glGetString(GL_VERSION));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    if (!spriteBatch_.init()) {
        LOG_FATAL("App", "Failed to initialize SpriteBatch");
        return false;
    }

    // Initialize editor (Dear ImGui)
    Editor::instance().init(window_, glContext_);

    onInit();

    running_ = true;
    LOG_INFO("App", "Engine initialized successfully");
    return true;
}

void App::run() {
    Uint64 lastTick = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();

    while (running_) {
        Uint64 currentTick = SDL_GetPerformanceCounter();
        deltaTime_ = (float)(currentTick - lastTick) / (float)freq;
        lastTick = currentTick;

        if (deltaTime_ > 0.1f) deltaTime_ = 0.1f;
        fps_ = (deltaTime_ > 0.0f) ? 1.0f / deltaTime_ : 0.0f;

        processEvents();
        update();
        render();
    }
}

void App::quit() {
    running_ = false;
}

void App::processEvents() {
    Input::instance().beginFrame();
    Editor::instance().beginFrame();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Editor gets events first
        Editor::instance().processEvent(event);

        // Only pass to game input if editor doesn't want it
        if (!Editor::instance().wantsInput()) {
            Input::instance().processEvent(event);
        } else {
            // Still process window events and quit
            if (event.type == SDL_QUIT) {
                running_ = false;
            } else if (event.type == SDL_WINDOWEVENT &&
                       event.window.event == SDL_WINDOWEVENT_RESIZED) {
                config_.windowWidth = event.window.data1;
                config_.windowHeight = event.window.data2;
                glViewport(0, 0, config_.windowWidth, config_.windowHeight);
            }
            // Always forward key events so keys don't get stuck
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                Input::instance().processEvent(event);
            }
        }

        switch (event.type) {
            case SDL_QUIT:
                running_ = false;
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    config_.windowWidth = event.window.data1;
                    config_.windowHeight = event.window.data2;
                    glViewport(0, 0, config_.windowWidth, config_.windowHeight);
                }
                break;

            case SDL_KEYDOWN:
                if (event.key.keysym.scancode == SDL_SCANCODE_F3) {
                    Editor::instance().toggle();
                    // Auto-pause when editor opens, auto-resume when it closes
                    Editor::instance().setPaused(Editor::instance().isOpen());
                    LOG_INFO("App", "Editor: %s", Editor::instance().isOpen() ? "OPEN (paused)" : "CLOSED (resumed)");
                }
                if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    if (Editor::instance().isOpen()) {
                        Editor::instance().cancelPlacement();
                    } else {
                        running_ = false;
                    }
                }
                // Delete selected entity
                if (event.key.keysym.scancode == SDL_SCANCODE_DELETE &&
                    Editor::instance().isOpen() && Editor::instance().selectedEntity()) {
                    auto* scene = SceneManager::instance().currentScene();
                    if (scene) {
                        scene->world().destroyEntity(Editor::instance().selectedEntity()->id());
                        Editor::instance().clearSelection();
                    }
                }
                break;

            case SDL_MOUSEWHEEL:
                // Scroll wheel zoom (works when editor is open)
                if (Editor::instance().isOpen() && !Editor::instance().wantsMouse()) {
                    float zoom = camera_.zoom();
                    if (event.wheel.y > 0) zoom *= 1.15f;  // scroll up = zoom in
                    else if (event.wheel.y < 0) zoom *= 0.87f; // scroll down = zoom out
                    // Clamp zoom range
                    if (zoom < 0.25f) zoom = 0.25f;
                    if (zoom > 4.0f) zoom = 4.0f;
                    camera_.setZoom(zoom);
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
                // Scene interaction: click in viewport when editor is open
                if (event.button.button == SDL_BUTTON_LEFT &&
                    Editor::instance().isOpen() &&
                    !Editor::instance().wantsMouse()) {
                    auto* scene = SceneManager::instance().currentScene();
                    if (scene) {
                        Vec2 screenPos = {(float)event.button.x, (float)event.button.y};
                        Editor::instance().handleSceneClick(
                            &scene->world(), &camera_,
                            screenPos, config_.windowWidth, config_.windowHeight);
                    }
                }
                break;

            case SDL_MOUSEMOTION:
                if (Editor::instance().isOpen() && !Editor::instance().wantsMouse()) {
                    // Right-click drag: pan camera
                    if (event.motion.state & SDL_BUTTON_RMASK) {
                        float scaleX = Camera::VIRTUAL_WIDTH / (float)config_.windowWidth / camera_.zoom();
                        float scaleY = Camera::VIRTUAL_HEIGHT / (float)config_.windowHeight / camera_.zoom();
                        Vec2 panDelta = {
                            -(float)event.motion.xrel * scaleX,
                            (float)event.motion.yrel * scaleY  // screen Y-down, world Y-up
                        };
                        camera_.setPosition(camera_.position() + panDelta);
                    }
                    // Left-click drag: move selected entity
                    else if (event.motion.state & SDL_BUTTON_LMASK) {
                        Vec2 screenPos = {(float)event.motion.x, (float)event.motion.y};
                        Editor::instance().handleSceneDrag(
                            &camera_, screenPos,
                            config_.windowWidth, config_.windowHeight);
                    }
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    Editor::instance().handleMouseUp();
                }
                break;
        }
    }
}

void App::update() {
    onUpdate(deltaTime_);

    // Skip game updates if editor is paused
    if (Editor::instance().isPaused()) return;

    fixedTimeAccumulator_ += deltaTime_;
    while (fixedTimeAccumulator_ >= config_.fixedTimestep) {
        onFixedUpdate(config_.fixedTimestep);

        auto* scene = SceneManager::instance().currentScene();
        if (scene) {
            scene->world().fixedUpdate(config_.fixedTimestep);
        }
        fixedTimeAccumulator_ -= config_.fixedTimestep;
    }

    auto* scene = SceneManager::instance().currentScene();
    if (scene) {
        scene->world().update(deltaTime_);
        scene->world().lateUpdate(deltaTime_);
        scene->world().processDestroyQueue();
    }
}

void App::render() {
    glClear(GL_COLOR_BUFFER_BIT);

    // Game rendering
    onRender(spriteBatch_, camera_);

    // Editor overlay (renders ImGui on top of game)
    auto* scene = SceneManager::instance().currentScene();
    World* world = scene ? &scene->world() : nullptr;
    Editor::instance().render(world, &camera_, &spriteBatch_);

    SDL_GL_SwapWindow(window_);
}

void App::shutdown() {
    LOG_INFO("App", "Shutting down...");
    onShutdown();

    Editor::instance().shutdown();
    spriteBatch_.shutdown();
    TextureCache::instance().clear();

    if (glContext_) {
        SDL_GL_DeleteContext(glContext_);
        glContext_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    SDL_Quit();
    Logger::instance().shutdown();
}

} // namespace fate
