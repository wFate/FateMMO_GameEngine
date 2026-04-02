#include "engine/app.h"
#ifndef FATEMMO_METAL
// Direct GL used for initialization (glGetString, initial state, window resize)
// and FBO blit pass (interleaved with ImGui) — intentionally outside RHI.
#include "engine/render/gfx/backend/gl/gl_loader.h"
#endif
#ifdef FATEMMO_METAL
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <fstream>
#include <sstream>
#endif
#include "engine/render/gfx/device.h"
#include "engine/render/shader.h"
#include "engine/core/logger.h"
#include "engine/platform/device_info.h"
#ifndef FATE_SHIPPING
#include "engine/editor/undo.h"
#include "engine/editor/log_viewer.h"
#endif
#ifndef FATE_SHIPPING
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#ifndef FATEMMO_METAL
#include "imgui_impl_opengl3.h"
#else
#include "imgui_impl_metal.h"
#endif
#endif // !FATE_SHIPPING
#include "engine/render/sdf_text.h"
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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        LOG_FATAL("App", "SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    sdlInitialized_ = true;

    // Resolve auto-detect target FPS from display refresh rate
    if (config_.targetFPS <= 0) {
        config_.targetFPS = DeviceInfo::getDisplayRefreshRate();
    }
    LOG_INFO("App", "Window: %dx%d, VSync: %s, Target FPS: %d (display: %dHz)",
             config_.windowWidth, config_.windowHeight,
             config_.vsync ? "ON" : "OFF", config_.targetFPS,
             DeviceInfo::getDisplayRefreshRate());

    // Register lifecycle event filter — catches mobile background/foreground
    // events before the main event loop (critical for iOS 5-second deadline)
    SDL_SetEventFilter(lifecycleEventFilter, this);

#ifndef FATEMMO_METAL
#ifdef FATEMMO_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
#endif // !FATEMMO_METAL

#ifdef FATEMMO_METAL
    Uint32 flags = SDL_WINDOW_METAL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_ALLOW_HIGHDPI;
#else
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_ALLOW_HIGHDPI;
#endif
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

#ifdef FATEMMO_METAL
    metalView_ = SDL_Metal_CreateView(window_);
    metalLayer_ = (__bridge void*)(__bridge CAMetalLayer*)SDL_Metal_GetLayer((SDL_MetalView)metalView_);
    CAMetalLayer* layer = (__bridge CAMetalLayer*)metalLayer_;
    layer.device = MTLCreateSystemDefaultDevice();
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
#ifdef FATEMMO_PLATFORM_IOS
    layer.maximumDrawableCount = 3;  // Triple buffering for ProMotion 120fps
#endif
    gfx::Device::instance().initMetal(metalLayer_);

    // Populate the Metal shader library — every shader creation depends on this.
#ifdef FATE_SHIPPING
    if (!gfx::Device::instance().loadMetalShaderLibrary("assets/shaders/metal/default.metallib")) {
        LOG_FATAL("App", "Metal: failed to load default.metallib");
        return false;
    }
#else
    {
        auto compileShader = [](const std::string& path) {
            std::ifstream f(path);
            if (!f.is_open()) {
                LOG_WARN("App", "Metal: shader source not found: %s", path.c_str());
                return;
            }
            std::ostringstream ss;
            ss << f.rdbuf();
            gfx::Device::instance().compileMetalShaderSource(ss.str(), path);
        };
        compileShader("assets/shaders/metal/sprite.metal");
        compileShader("assets/shaders/metal/tile_chunk.metal");
        compileShader("assets/shaders/metal/fullscreen_quad.metal");
        compileShader("assets/shaders/metal/blit.metal");
        compileShader("assets/shaders/metal/light.metal");
        compileShader("assets/shaders/metal/postprocess.metal");
        compileShader("assets/shaders/metal/bloom_extract.metal");
        compileShader("assets/shaders/metal/blur.metal");
        compileShader("assets/shaders/metal/grid.metal");
    }
#endif // FATE_SHIPPING
#else
    glContext_ = SDL_GL_CreateContext(window_);
    if (!glContext_) {
        LOG_FATAL("App", "SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetSwapInterval(config.vsync ? 1 : 0);

#ifdef FATEMMO_GLES
    // On iOS with Retina displays, the drawable size differs from the window size
    int drawW, drawH;
    SDL_GL_GetDrawableSize(window_, &drawW, &drawH);
    config_.windowWidth = drawW;
    config_.windowHeight = drawH;
    LOG_INFO("App", "Drawable size: %dx%d", drawW, drawH);
#endif

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
#endif // FATEMMO_METAL

    if (!spriteBatch_.init()) {
        LOG_FATAL("App", "Failed to initialize SpriteBatch");
        return false;
    }

    JobSystem::instance().init(4);

    // Initialize fullscreen quad (used by lighting + post-process passes)
    FullscreenQuad::instance().init();

#ifndef FATE_SHIPPING
    // Initialize editor (Dear ImGui)
    Editor::instance().init(window_, glContext_);
    Editor::instance().setPostProcessConfig(&postProcessConfig_);
    Editor::instance().setUIManager(&uiManager_);

    // Hook log viewer into logger
    Logger::instance().setLogCallback([](const std::string& msg, int level) {
        LogViewer::instance().addMessage(msg, level);
    });
#endif

    assetsDir_ = config.assetsDir;

    // Register asset loaders BEFORE onInit() — game creates entities that load textures
    AssetRegistry::instance().registerLoader(makeTextureLoader());
    AssetRegistry::instance().registerLoader(makeJsonLoader());
    AssetRegistry::instance().registerLoader(makeShaderLoader());

    // Game registers its scene passes (tiles, entities, etc.) in onInit()
    onInit();

    // Load default UI theme (non-fatal — screens work without a theme)
    uiManager_.loadTheme("assets/ui/themes/default.json");

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

        // Skip update/render when backgrounded (mobile power saving)
        if (lifecycleState_ != AppLifecycleState::Active) {
            SDL_Delay(100);
            continue;
        }

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
#ifndef FATE_SHIPPING
    Editor::instance().beginFrame();
#endif

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
#ifndef FATE_SHIPPING
        // ---- Input priority chain (editor build) ----
        //
        //   PAUSED (editing):  ImGui → Editor shortcuts → (game gets nothing)
        //   PLAYING:           ImGui → UI text fields → Game Input
        //
        // ImGui always sees events (it needs them for its own panels).
        // Key-UP is always forwarded to Input to prevent stuck keys.
        Editor::instance().processEvent(event);

        bool paused = Editor::instance().isPaused();
        bool isKeyboard = (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ||
                           event.type == SDL_TEXTINPUT);

        if (paused) {
            // Editor owns everything. Only forward key-UP to clear held state.
            if (event.type == SDL_KEYUP) {
                Input::instance().processEvent(event);
            }
        } else if (isKeyboard) {
            // If an ImGui editor widget wants keyboard (InputText, DragFloat,
            // DragInt, Checkbox, etc.), don't route to game UI at all.
            // WantTextInput alone is too narrow — it misses drag/slider widgets.
            // But WantCaptureKeyboard alone is too broad — the game viewport
            // is itself an ImGui window, so it's true whenever the viewport is
            // focused.  Exclude the viewport so keyboard reaches game UI
            // (login fields, chat, etc.) during play.
            if (ImGui::GetIO().WantCaptureKeyboard &&
                !Editor::instance().isViewportHovered()) {
                // Always forward key-UP to clear held state (prevents stuck keys
                // when cursor leaves viewport while a key is held)
                if (event.type == SDL_KEYUP)
                    Input::instance().processEvent(event);
                continue;
            }

            // Playing — UI text fields get first crack at keyboard/text
            bool uiConsumed = false;
            if (uiManager_.focusedNode() && uiManager_.focusedNode()->visible()) {
                if (event.type == SDL_TEXTINPUT) {
                    uiManager_.handleTextInput(event.text.text);
                    uiConsumed = true;
                } else if (event.type == SDL_KEYDOWN) {
                    uiConsumed = uiManager_.focusedNode()->onKeyInput(
                        event.key.keysym.scancode, true);
                } else if (event.type == SDL_KEYUP) {
                    uiManager_.focusedNode()->onKeyInput(
                        event.key.keysym.scancode, false);
                }
            }

            if (!uiConsumed) {
                // Game Input gets the event
                Input::instance().processEvent(event);
            } else if (event.type == SDL_KEYUP) {
                // Always forward key-UP to clear held state
                Input::instance().processEvent(event);
            }
        } else {
            // Non-keyboard events (mouse, window, touch) go to game Input
            Input::instance().processEvent(event);
        }
#else
        // Shipping build: UI text fields still need priority keyboard routing
        bool uiConsumedShipping = false;
        if (uiManager_.focusedNode() && uiManager_.focusedNode()->visible()) {
            if (event.type == SDL_TEXTINPUT) {
                uiManager_.handleTextInput(event.text.text);
                uiConsumedShipping = true;
            } else if (event.type == SDL_KEYDOWN) {
                uiConsumedShipping = uiManager_.focusedNode()->onKeyInput(
                    event.key.keysym.scancode, true);
            } else if (event.type == SDL_KEYUP) {
                uiManager_.focusedNode()->onKeyInput(
                    event.key.keysym.scancode, false);
            }
        }
        if (!uiConsumedShipping) {
            Input::instance().processEvent(event);
        } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            Input::instance().processEvent(event);
            if (event.type == SDL_KEYDOWN) {
                Input::instance().consumeKeyPress(
                    event.key.keysym.scancode);
            }
        }
#endif

        switch (event.type) {
            case SDL_QUIT:
                running_ = false;
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    config_.windowWidth = event.window.data1;
                    config_.windowHeight = event.window.data2;
#ifndef FATEMMO_METAL
                    glViewport(0, 0, config_.windowWidth, config_.windowHeight);
#endif
                }
                break;

            case SDL_APP_WILLENTERBACKGROUND:
            case SDL_APP_DIDENTERBACKGROUND:
            case SDL_APP_WILLENTERFOREGROUND:
            case SDL_APP_DIDENTERFOREGROUND:
            case SDL_APP_LOWMEMORY:
                handleLifecycleEvent(event);
                break;

#ifndef FATE_SHIPPING
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

                        // Check if click lands on a selected UI widget first
#ifdef FATE_HAS_GAME
                        if (Editor::instance().uiEditorPanel().handleViewportClick(screenPos)) {
                            // UI widget drag started — skip entity/tile handling
                        } else {
                            // Click missed the UI widget — clear UI selection so it
                            // doesn't keep intercepting viewport clicks for scene work.
                            if (Editor::instance().uiEditorPanel().hasSelection())
                                Editor::instance().uiEditorPanel().clearSelection();
                        }
                        if (Editor::instance().uiEditorPanel().isDraggingWidget()) {
                            // already handled above
                        } else
#endif
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
#ifdef FATE_HAS_GAME
                        if (Editor::instance().uiEditorPanel().isDraggingWidget()) {
                            Editor::instance().uiEditorPanel().handleViewportDrag(localPos);
                        } else
#endif
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
#ifdef FATE_HAS_GAME
                    Editor::instance().uiEditorPanel().handleViewportRelease(
                        Editor::instance().uiManager());
#endif
                    Editor::instance().handleMouseUp();
                }
                break;
#endif
        }
    }
}

void App::update() {
    // Process asset reloads unconditionally (hot-reload works while editing)
    elapsedTime_ += deltaTime_;
    AssetRegistry::instance().processReloads(elapsedTime_);
    AssetRegistry::instance().processAsyncLoads();

    // Update retained-mode UI system (data binding resolution, hot-reload checks)
    uiManager_.update(deltaTime_);

    // Route mouse/keyboard input to the UI system BEFORE game logic so
    // wantCaptureMouse() is accurate when onUpdate() runs.
#ifndef FATE_SHIPPING
    // Map window-space mouse coords into FBO-space so hit-testing matches
    // the layout computed against FBO dimensions.
    {
        auto& ed = Editor::instance();
        Vec2 vp  = ed.viewportPos();   // top-left of displayed image in window
        Vec2 vs  = ed.viewportSize();  // displayed image size (may differ from FBO)
        auto& fbo = ed.viewportFbo();
        float fboW = static_cast<float>(fbo.width());
        float fboH = static_cast<float>(fbo.height());
        float sx = (vs.x > 0.0f) ? fboW / vs.x : 1.0f;
        float sy = (vs.y > 0.0f) ? fboH / vs.y : 1.0f;
        uiManager_.setInputTransform(vp.x, vp.y, sx, sy);
    }
    // Skip game-UI input when the user is interacting with an ImGui panel
    // (e.g. the UI Inspector).  Otherwise the retained-mode chat panel
    // captures focus/clicks that should go to editor widgets.
    // Also clear stale game-UI focus so it doesn't steal keyboard later.
    // But when the viewport is hovered the user is interacting with the
    // game, not an editor panel — let handleInput() run normally.
    if (Editor::instance().wantsMouse() && !Editor::instance().isViewportHovered()) {
        uiManager_.clearFocus();
    } else {
        uiManager_.handleInput();
    }
#else
    uiManager_.handleInput();
#endif

    // Only consume the mouse press when a UI node actually accepted it
    // (onPress returned true).  Don't use wantCaptureMouse() — that checks
    // hoveredNode_ which is non-null over any StretchAll HUD root.
    if (uiManager_.pressedNode()) {
        Input::instance().consumeMousePress(SDL_BUTTON_LEFT);
    }

    onUpdate(deltaTime_);

    // Always process destroy queue (so editor delete works while paused)
    auto* activeScene = SceneManager::instance().currentScene();
    if (activeScene) {
        activeScene->world().processDestroyQueue();
    }

#ifndef FATE_SHIPPING
    // Skip game logic if editor is paused
    if (Editor::instance().isPaused()) return;
#endif

    // Skip system updates during async scene loading — tickFinalization
    // destroys/creates entities mid-frame, so running systems against a
    // half-built world causes use-after-free crashes.
    if (isLoading_) {
        onLoadingUpdate(deltaTime_);
        return;
    }

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

#ifdef FATEMMO_METAL
    @autoreleasepool {
        CAMetalLayer* layer = (__bridge CAMetalLayer*)metalLayer_;
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (!drawable) return;

        id<MTLCommandQueue> commandQueue = (__bridge id<MTLCommandQueue>)
            gfx::Device::instance().resolveMetalCommandQueue();
        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];

#ifndef FATE_SHIPPING
        auto& editor = Editor::instance();

        auto& editorFbo = editor.viewportFbo();
        int vpW = editorFbo.width();
        int vpH = editorFbo.height();

        if (vpW > 0 && vpH > 0 && editorFbo.isValid()) {
            camera_.setViewportSize(vpW, vpH);

            if (isLoading_) {
                // Loading — skip render graph (world is half-built), render UI only
                gfx::CommandList uiCmdList;
                uiCmdList.begin();
                spriteBatch_.setCommandList(&uiCmdList);

                Mat4 screenProj = SDFText::screenProjection(vpW, vpH);
                spriteBatch_.begin(screenProj);
                uiManager_.computeLayout(static_cast<float>(vpW), static_cast<float>(vpH));
                uiManager_.render(spriteBatch_, SDFText::instance());
                spriteBatch_.end();

                spriteBatch_.setCommandList(nullptr);
                uiCmdList.end();
            } else {
                RenderPassContext ctx;
                ctx.spriteBatch = &spriteBatch_;
                ctx.camera = &camera_;
                ctx.world = world;
                ctx.viewportWidth = vpW;
                ctx.viewportHeight = vpH;
                renderGraph_.execute(ctx);

                // Blit PostProcess result to editor viewport FBO via Metal pipeline
                {
                    MTLRenderPassDescriptor* blitPassDesc = [MTLRenderPassDescriptor renderPassDescriptor];
                    blitPassDesc.colorAttachments[0].texture = drawable.texture;
                    blitPassDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
                    blitPassDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
                    id<MTLRenderCommandEncoder> blitEncoder =
                        [commandBuffer renderCommandEncoderWithDescriptor:blitPassDesc];
                    FullscreenQuad::instance().draw((__bridge void*)blitEncoder);
                    [blitEncoder endEncoding];
                }

                onRender(spriteBatch_, camera_);
                editor.renderScene(&spriteBatch_, &camera_);

                // UI system: render loaded screens in screen-space on top of game world
                {
                    gfx::CommandList uiCmdList;
                    uiCmdList.begin();
                    spriteBatch_.setCommandList(&uiCmdList);

                    Mat4 screenProj = SDFText::screenProjection(vpW, vpH);
                    spriteBatch_.begin(screenProj);
                    uiManager_.computeLayout(static_cast<float>(vpW), static_cast<float>(vpH));
                    uiManager_.render(spriteBatch_, SDFText::instance());
                    spriteBatch_.end();

                    spriteBatch_.setCommandList(nullptr);
                    uiCmdList.end();
                }
            }
        }

        editor.renderUI(world, &camera_, &spriteBatch_, &frameArena_);
#else
        int vpW = config_.windowWidth;
        int vpH = config_.windowHeight;
        camera_.setViewportSize(vpW, vpH);

        if (isLoading_) {
            // Loading — skip render graph, render UI only
            gfx::CommandList uiCmdList;
            uiCmdList.begin();
            spriteBatch_.setCommandList(&uiCmdList);

            Mat4 screenProj = SDFText::screenProjection(vpW, vpH);
            spriteBatch_.begin(screenProj);
            uiManager_.computeLayout(static_cast<float>(vpW), static_cast<float>(vpH));
            uiManager_.render(spriteBatch_, SDFText::instance());
            spriteBatch_.end();

            spriteBatch_.setCommandList(nullptr);
            uiCmdList.end();
        } else {
            RenderPassContext ctx;
            ctx.spriteBatch = &spriteBatch_;
            ctx.camera = &camera_;
            ctx.world = world;
            ctx.viewportWidth = vpW;
            ctx.viewportHeight = vpH;
            renderGraph_.execute(ctx);

            // Blit PostProcess result to drawable via Metal pipeline
            onRender(spriteBatch_, camera_);

            // UI system: render loaded screens in screen-space on top of game world
            {
                gfx::CommandList uiCmdList;
                uiCmdList.begin();
                spriteBatch_.setCommandList(&uiCmdList);

                Mat4 screenProj = SDFText::screenProjection(vpW, vpH);
                spriteBatch_.begin(screenProj);
                uiManager_.computeLayout(static_cast<float>(vpW), static_cast<float>(vpH));
                uiManager_.render(spriteBatch_, SDFText::instance());
                spriteBatch_.end();

                spriteBatch_.setCommandList(nullptr);
                uiCmdList.end();
            }
        }

        MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        passDesc.colorAttachments[0].texture = drawable.texture;
        passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
        passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);
        passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> encoder =
            [commandBuffer renderCommandEncoderWithDescriptor:passDesc];
        // FullscreenQuad blit pass — draw the PostProcess result into the drawable
        FullscreenQuad::instance().draw((__bridge void*)encoder);
        [encoder endEncoding];
#endif

        [commandBuffer presentDrawable:drawable];
        [commandBuffer commit];
    } // @autoreleasepool
#else
#ifndef FATE_SHIPPING
    auto& editor = Editor::instance();

    auto& editorFbo = editor.viewportFbo();
    int vpW = editorFbo.width();
    int vpH = editorFbo.height();

    if (vpW > 0 && vpH > 0 && editorFbo.isValid()) {
        // Adapt camera projection to FBO aspect ratio
        camera_.setViewportSize(vpW, vpH);

        if (isLoading_) {
            // Loading — skip render graph, render UI only into editor FBO
            editorFbo.bind();
            glClearColor(0.102f, 0.102f, 0.180f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            Mat4 screenProj = SDFText::screenProjection(vpW, vpH);
            spriteBatch_.begin(screenProj);
            uiManager_.computeLayout(static_cast<float>(vpW), static_cast<float>(vpH));
            uiManager_.render(spriteBatch_, SDFText::instance());
            spriteBatch_.end();
            editorFbo.unbind();
        } else {
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
            static bool s_blitAttempted = false;
            if (!s_blitLoaded && !s_blitAttempted) {
                s_blitAttempted = true;
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

            // Re-bind editor FBO — getFBO() above may have changed GL
            // framebuffer state if it needed to create or resize the
            // PostProcess FBO (destroy + create binds the new FBO).
            editorFbo.bind();

            // Legacy onRender callback (for any remaining direct rendering)
            onRender(spriteBatch_, camera_);

            // Editor overlays (grid, selection, etc.) drawn on top
            editor.renderScene(&spriteBatch_, &camera_);

            // UI system: render loaded screens in screen-space on top of game world
            {
                gfx::CommandList uiCmdList;
                uiCmdList.begin();
                spriteBatch_.setCommandList(&uiCmdList);

                Mat4 screenProj = SDFText::screenProjection(vpW, vpH);
                spriteBatch_.begin(screenProj);
                uiManager_.computeLayout(static_cast<float>(vpW), static_cast<float>(vpH));
                uiManager_.render(spriteBatch_, SDFText::instance());
                spriteBatch_.end();

                spriteBatch_.setCommandList(nullptr);
                uiCmdList.end();
            }

            editorFbo.unbind();
        }
    }

    // Editor UI fills the window
    glViewport(0, 0, config_.windowWidth, config_.windowHeight);
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    editor.renderUI(world, &camera_, &spriteBatch_, &frameArena_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
#else
    // Shipping: render directly to the default framebuffer
    int vpW = config_.windowWidth;
    int vpH = config_.windowHeight;
    camera_.setViewportSize(vpW, vpH);

    if (isLoading_) {
        // Loading — skip render graph, render UI only
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, vpW, vpH);
        glClearColor(0.102f, 0.102f, 0.180f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        Mat4 screenProj = SDFText::screenProjection(vpW, vpH);
        spriteBatch_.begin(screenProj);
        uiManager_.computeLayout(static_cast<float>(vpW), static_cast<float>(vpH));
        uiManager_.render(spriteBatch_, SDFText::instance());
        spriteBatch_.end();
    } else {
        RenderPassContext ctx;
        ctx.spriteBatch = &spriteBatch_;
        ctx.camera = &camera_;
        ctx.world = world;
        ctx.viewportWidth = vpW;
        ctx.viewportHeight = vpH;
        renderGraph_.execute(ctx);

        // Bind default framebuffer and blit PostProcess result to screen
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, vpW, vpH);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        auto& postFbo = renderGraph_.getFBO("PostProcess", vpW, vpH);

        static Shader s_blitShader;
        static bool s_blitLoaded = false;
        static bool s_blitAttempted = false;
        if (!s_blitLoaded && !s_blitAttempted) {
            s_blitAttempted = true;
            s_blitLoaded = s_blitShader.loadFromFile(
                "assets/shaders/fullscreen_quad.vert",
                "assets/shaders/blit.frag"
            );
        }

        if (s_blitLoaded) {
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

        onRender(spriteBatch_, camera_);

        // UI system: render loaded screens in screen-space on top of game world
        {
            gfx::CommandList uiCmdList;
            uiCmdList.begin();
            spriteBatch_.setCommandList(&uiCmdList);

            Mat4 screenProj = SDFText::screenProjection(vpW, vpH);
            spriteBatch_.begin(screenProj);
            uiManager_.computeLayout(static_cast<float>(vpW), static_cast<float>(vpH));
            uiManager_.render(spriteBatch_, SDFText::instance());
            spriteBatch_.end();

            spriteBatch_.setCommandList(nullptr);
            uiCmdList.end();
        }
    }

#endif

    SDL_GL_SwapWindow(window_);
#endif // FATEMMO_METAL
}

void App::shutdown() {
    if (shutdownComplete_) return;
    shutdownComplete_ = true;
    if (!sdlInitialized_) return;
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

#ifndef FATE_SHIPPING
    Editor::instance().shutdown();
#endif
    spriteBatch_.shutdown();
    fileWatcher_.stop();
    TextureCache::instance().clear();
    AssetRegistry::instance().clear();

    JobSystem::instance().shutdown();

    gfx::Device::instance().shutdown();

#ifdef FATEMMO_METAL
    if (metalView_) {
        SDL_Metal_DestroyView((SDL_MetalView)metalView_);
        metalView_ = nullptr;
    }
#else
    if (glContext_) {
        SDL_GL_DeleteContext(glContext_);
        glContext_ = nullptr;
    }
#endif
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    SDL_Quit();
    Logger::instance().shutdown();
}

int SDLCALL App::lifecycleEventFilter(void* userdata, SDL_Event* event) {
    App* app = static_cast<App*>(userdata);
    if (!app) return 1;

    switch (event->type) {
        case SDL_APP_WILLENTERBACKGROUND:
            LOG_INFO("App", "Lifecycle: entering background");
            app->handleLifecycleEvent(*event);
            return 0;

        case SDL_APP_DIDENTERBACKGROUND:
            LOG_INFO("App", "Lifecycle: entered background");
            return 0;

        case SDL_APP_WILLENTERFOREGROUND:
            LOG_INFO("App", "Lifecycle: entering foreground");
            return 0;

        case SDL_APP_DIDENTERFOREGROUND:
            LOG_INFO("App", "Lifecycle: entered foreground");
            app->handleLifecycleEvent(*event);
            return 0;

        case SDL_APP_LOWMEMORY:
            LOG_WARN("App", "Lifecycle: low memory warning");
            app->handleLifecycleEvent(*event);
            return 0;

        default:
            return 1;
    }
}

void App::handleLifecycleEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_APP_WILLENTERBACKGROUND:
            lifecycleState_ = AppLifecycleState::Background;
            onEnterBackground();
            break;

        case SDL_APP_DIDENTERFOREGROUND:
            lifecycleState_ = AppLifecycleState::Active;
            onEnterForeground();
            break;

        case SDL_APP_LOWMEMORY:
            onLowMemory();
            break;

        default:
            break;
    }
}

} // namespace fate
