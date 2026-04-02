// examples/demo_app.cpp — FateEngine editor demo
//
// Showcases the engine's editor UI, rendering pipeline, and tooling.
// Builds with zero external dependencies via CMake FetchContent.
#include "engine/app.h"
#include "engine/scene/scene.h"
#include "engine/scene/scene_manager.h"
#include "engine/ecs/world.h"
#include "engine/render/camera.h"
#include "engine/render/sprite_batch.h"
#include "engine/core/logger.h"

#ifndef FATE_SHIPPING
#include "engine/editor/editor.h"
#endif

#include <SDL.h>
#include <cmath>

using namespace fate;

class DemoApp : public App {
public:
    void onInit() override {
        SceneManager::instance().registerScene("demo", [](Scene&) {});
        SceneManager::instance().switchScene("demo");

#ifndef FATE_SHIPPING
        // Point the asset browser at the project root so users can explore
        // the engine source tree, shaders, and example code.
        Editor::instance().setAssetRoot(projectRoot_);
        Editor::instance().setPaused(true);
#endif

        camera().setPosition({0.0f, 0.0f});
        camera().setZoom(1.0f);

        LOG_INFO("Demo", "FateEngine demo initialized — explore the editor!");
    }

    void onRender(SpriteBatch& batch, Camera& cam) override {
        Mat4 proj = cam.getViewProjection();
        float zoom = cam.zoom();

        // Large fixed range — the GPU clips via the projection matrix so
        // we don't need to compute exact visible bounds.
        constexpr int RANGE = 512; // tiles from origin in each direction
        float extent = RANGE * TILE_SIZE;

        // World-space width for ~1 screen pixel.
        // VIRTUAL_HEIGHT (270) maps to the full viewport height, so one
        // screen pixel ≈ 270 / viewportHeightPx.  A typical viewport is
        // ~900px, giving ~0.3 world units.  Scale by 1/zoom so the line
        // stays 1px at any zoom level.
        float px = 0.3f / zoom;

        // Fade minor grid when zoomed out (matches grid.frag zoomFade)
        float zoomFade = std::min(zoom, 1.0f);
        Color minorColor{1.0f, 1.0f, 1.0f, 0.08f * zoomFade};
        Color majorColor{1.0f, 1.0f, 1.0f, 0.16f};

        batch.begin(proj);

        // drawRect position is the CENTER of the quad
        float span = extent * 2.0f;
        for (int i = -RANGE; i <= RANGE; ++i) {
            bool major = (i % 10 == 0);
            const Color& c = major ? majorColor : minorColor;
            float pos = i * TILE_SIZE;
            // Vertical line centered at (pos, 0)
            batch.drawRect({pos, 0.0f}, {px, span}, c);
            // Horizontal line centered at (0, pos)
            batch.drawRect({0.0f, pos}, {span, px}, c);
        }

        // Origin axes (matches grid.frag: red=X, green=Y, 0.9 alpha)
        batch.drawRect({0.0f, 0.0f}, {span, px}, Color{0.8f, 0.2f, 0.2f, 0.9f});
        batch.drawRect({0.0f, 0.0f}, {px, span}, Color{0.2f, 0.8f, 0.2f, 0.9f});

        batch.end();
    }

    void onUpdate(float dt) override {
        (void)dt;
    }

private:
    static constexpr float TILE_SIZE = 32.0f;
    std::string projectRoot_;

    friend int main(int, char**);
};

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    DemoApp app;
    AppConfig config;
    config.title = "FateEngine Demo";
    config.windowWidth = 1600;
    config.windowHeight = 900;

#ifdef FATE_SOURCE_DIR
    config.assetsDir = std::string(FATE_SOURCE_DIR) + "/assets";
    app.projectRoot_ = std::string(FATE_SOURCE_DIR);
#endif

    if (!app.init(config)) {
        return 1;
    }

    app.run();
    return 0;
}
