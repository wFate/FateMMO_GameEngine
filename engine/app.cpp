#include "engine/app.h"
// Direct GL used for initialization (glGetString, initial state, window resize)
// and FBO blit pass (interleaved with ImGui) — intentionally outside RHI.
#include "engine/render/gfx/backend/gl/gl_loader.h"
#include "engine/render/gfx/device.h"
#include "engine/render/shader.h"
#include "engine/core/logger.h"
#include "engine/editor/undo.h"
#include "engine/editor/log_viewer.h"
#include "engine/profiling/tracy_zones.h"
#include "engine/job/job_system.h"
#if defined(ENGINE_MEMORY_DEBUG)
#include "engine/memory/allocator_registry.h"
#endif
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

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;
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

    gfx::Device::instance().init();

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

    JobSystem::instance().init(4);

    // Initialize fullscreen quad (used by lighting + post-process passes)
    FullscreenQuad::instance().init();

    // Initialize editor (Dear ImGui)
    Editor::instance().init(window_, glContext_);
    Editor::instance().setPostProcessConfig(&postProcessConfig_);

    // Hook log viewer into logger
    Logger::instance().setLogCallback([](const std::string& msg, int level) {
        LogViewer::instance().addMessage(msg, level);
    });

    assetsDir_ = config.assetsDir;

    // Register asset loaders BEFORE onInit() — game creates entities that load textures
    AssetRegistry::instance().registerLoader(makeTextureLoader());
    AssetRegistry::instance().registerLoader(makeJsonLoader());
    AssetRegistry::instance().registerLoader(makeShaderLoader());

    // Game registers its scene passes (tiles, entities, etc.) in onInit()
    onInit();

    // Register engine render passes AFTER game passes, so the graph order is:
    // [game scene passes] -> Lighting -> BloomExtract -> BloomBlur -> PostProcess
    registerLightingPass(renderGraph_, lightingConfig_);
    registerPostProcessPasses(renderGraph_, postProcessConfig_);

    // Start file watcher on assets directory
    if (!assetsDir_.empty()) {
        fileWatcher_.start(assetsDir_, [this](const std::string& relativePath) {
            std::string fullPath = assetsDir_ + "/" + relativePath;
            AssetRegistry::instance().queueReload(fullPath);
        });
    }

#if defined(ENGINE_MEMORY_DEBUG)
    AllocatorRegistry::instance().add({
        .name = "FrameArena",
        .type = AllocatorType::FrameArena,
        .getUsed = [this]() -> size_t { return frameArena_.current().position(); },
        .getCommitted = [this]() -> size_t { return frameArena_.current().committed(); },
        .getReserved = [this]() -> size_t { return frameArena_.current().reserved(); },
    });
#endif

    running_ = true;
    LOG_INFO("App", "Engine initialized successfully");
    return true;
}

void App::run() {
    Uint64 lastTick = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();

    while (running_) {
        frameArena_.swap();
        Uint64 currentTick = SDL_GetPerformanceCounter();
        deltaTime_ = (float)(currentTick - lastTick) / (float)freq;
        lastTick = currentTick;

        if (deltaTime_ > 0.1f) deltaTime_ = 0.1f;
        fps_ = (deltaTime_ > 0.0f) ? 1.0f / deltaTime_ : 0.0f;

        processEvents();
        update();
        render();

        FATE_FRAME_MARK;
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

        // Keyboard routing: when paused (editing), editor captures keyboard.
        // When playing, game keys go through unless an ImGui text field has focus.
        bool editorWantsKeyboard = Editor::instance().wantsKeyboard() && Editor::instance().isPaused();
        if (!editorWantsKeyboard) {
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
                if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    Editor::instance().cancelPlacement();
                    // Escape no longer quits — use window close (X) button instead.
                    // Game systems (combat targeting) use Escape for gameplay actions.
                }
                // Forward all keyboard shortcuts to editor
                {
                    auto* scene = SceneManager::instance().currentScene();
                    if (scene) {
                        Editor::instance().handleKeyShortcuts(&scene->world(), event);
                    }
                }
                break;

            case SDL_MOUSEWHEEL:
                // Scroll wheel zoom (works when editor is open)
                if (Editor::instance().isViewportHovered()) {
                    float zoom = camera_.zoom();
                    if (event.wheel.y > 0) zoom *= 1.15f;  // scroll up = zoom in
                    else if (event.wheel.y < 0) zoom *= 0.87f; // scroll down = zoom out
                    // Clamp zoom range
                    if (zoom < 0.05f) zoom = 0.05f;  // can see ~19,200x10,800px = 600x337 tiles
                    if (zoom > 8.0f) zoom = 8.0f;    // zoomed in to ~120x67px view
                    camera_.setZoom(zoom);
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT &&
                    Editor::instance().isViewportHovered() &&
                    Editor::instance().isPaused()) {
                    auto* scene = SceneManager::instance().currentScene();
                    if (scene) {
                        Vec2 vpPos = Editor::instance().viewportPos();
                        Vec2 vpSize = Editor::instance().viewportSize();
                        Vec2 screenPos = {
                            (float)event.button.x - vpPos.x,
                            (float)event.button.y - vpPos.y
                        };
                        int vpW = (int)vpSize.x;
                        int vpH = (int)vpSize.y;

                        if (Editor::instance().isTilePaintMode()) {
                            Editor::instance().paintTileAt(
                                &scene->world(), &camera_, screenPos, vpW, vpH);
                        } else if (Editor::instance().isEraseMode()) {
                            Editor::instance().eraseTileAt(
                                &scene->world(), &camera_, screenPos, vpW, vpH);
                        } else {
                            Editor::instance().handleSceneClick(
                                &scene->world(), &camera_, screenPos, vpW, vpH);
                        }
                    }
                }
                break;

            case SDL_MOUSEMOTION:
                if (Editor::instance().isViewportHovered()) {
                    Vec2 vpPos = Editor::instance().viewportPos();
                    Vec2 vpSize = Editor::instance().viewportSize();
                    Vec2 localPos = {
                        (float)event.motion.x - vpPos.x,
                        (float)event.motion.y - vpPos.y
                    };
                    int vpW = (int)vpSize.x;
                    int vpH = (int)vpSize.y;

                    // Right-click drag: pan camera
                    if (event.motion.state & SDL_BUTTON_RMASK) {
                        float scaleX = camera_.virtualWidth() / (float)vpW / camera_.zoom();
                        float scaleY = Camera::VIRTUAL_HEIGHT / (float)vpH / camera_.zoom();
                        Vec2 panDelta = {
                            -(float)event.motion.xrel * scaleX,
                            (float)event.motion.yrel * scaleY
                        };
                        camera_.setPosition(camera_.position() + panDelta);
                    }
                    // Left-click drag: paint tiles or move entity (only when paused/editing)
                    else if ((event.motion.state & SDL_BUTTON_LMASK) && Editor::instance().isPaused()) {
                        if (Editor::instance().isTilePaintMode()) {
                            auto* scene = SceneManager::instance().currentScene();
                            if (scene) {
                                Editor::instance().paintTileAt(
                                    &scene->world(), &camera_, localPos, vpW, vpH);
                            }
                        } else if (Editor::instance().isEraseMode()) {
                            auto* scene = SceneManager::instance().currentScene();
                            if (scene) {
                                Editor::instance().eraseTileAt(
                                    &scene->world(), &camera_, localPos, vpW, vpH);
                            }
                        } else {
                            Editor::instance().handleSceneDrag(
                                &camera_, localPos, vpW, vpH);
                        }
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
    // Process asset reloads unconditionally (hot-reload works while editing)
    elapsedTime_ += deltaTime_;
    AssetRegistry::instance().processReloads(elapsedTime_);

    onUpdate(deltaTime_);

    // Always process destroy queue (so editor delete works while paused)
    auto* activeScene = SceneManager::instance().currentScene();
    if (activeScene) {
        activeScene->world().processDestroyQueue();
    }

    // Skip game logic if editor is paused
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
    auto* scene = SceneManager::instance().currentScene();
    World* world = scene ? &scene->world() : nullptr;
    auto& editor = Editor::instance();

    auto& editorFbo = editor.viewportFbo();
    int vpW = editorFbo.width();
    int vpH = editorFbo.height();

    if (vpW > 0 && vpH > 0 && editorFbo.isValid()) {
        // Adapt camera projection to FBO aspect ratio
        camera_.setViewportSize(vpW, vpH);

        // Execute render graph — all passes render into internal FBOs
        RenderPassContext ctx;
        ctx.spriteBatch = &spriteBatch_;
        ctx.camera = &camera_;
        ctx.world = world;
        ctx.viewportWidth = vpW;
        ctx.viewportHeight = vpH;
        renderGraph_.execute(ctx);

        // Blit final result (PostProcess FBO) to editor viewport FBO
        editorFbo.bind();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        auto& postFbo = renderGraph_.getFBO("PostProcess", vpW, vpH);

        static Shader s_blitShader;
        static bool s_blitLoaded = false;
        if (!s_blitLoaded) {
            s_blitLoaded = s_blitShader.loadFromFile(
                "assets/shaders/fullscreen_quad.vert",
                "assets/shaders/blit.frag"
            );
        }

        if (s_blitLoaded) {
            // Blit is a full copy — disable blending so prior GL state cannot corrupt it
            glDisable(GL_BLEND);
            s_blitShader.bind();
            s_blitShader.setInt("u_texture", 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, postFbo.textureId());
            FullscreenQuad::instance().draw();
            s_blitShader.unbind();
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        // Legacy onRender callback (for any remaining direct rendering)
        onRender(spriteBatch_, camera_);

        // Editor overlays (grid, selection, etc.) drawn on top
        editor.renderScene(&spriteBatch_, &camera_);

        editorFbo.unbind();
    }

    // Editor UI fills the window
    glViewport(0, 0, config_.windowWidth, config_.windowHeight);
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    editor.renderUI(world, &camera_, &spriteBatch_, &frameArena_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    SDL_GL_SwapWindow(window_);
}

void App::shutdown() {
    if (shutdownComplete_) return;
    shutdownComplete_ = true;
    LOG_INFO("App", "Shutting down...");
#if defined(ENGINE_MEMORY_DEBUG)
    AllocatorRegistry::instance().remove("FrameArena");
#endif

    // Clear render passes first — their lambda captures reference game objects
    // (tilemap_, renderSystem_, etc.) that onShutdown() is about to destroy.
    renderGraph_.clearPasses();

    onShutdown();

    // Destroy the current scene (and its World) now, while all singletons are
    // still alive.  If we leave this to SceneManager's static destructor, the
    // AllocatorRegistry singleton may already be destroyed, causing a
    // "vector erase iterator outside range" assertion in World::~World().
    SceneManager::instance().unloadScene();

    // Destroy render graph FBOs while GL context is still alive
    renderGraph_.clearFBOs();
    FullscreenQuad::instance().shutdown();

    Editor::instance().shutdown();
    spriteBatch_.shutdown();
    fileWatcher_.stop();
    TextureCache::instance().clear();
    AssetRegistry::instance().clear();

    JobSystem::instance().shutdown();

    gfx::Device::instance().shutdown();

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
