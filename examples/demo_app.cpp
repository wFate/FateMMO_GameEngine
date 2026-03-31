// examples/demo_app.cpp — Minimal FateEngine demo
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

using namespace fate;

class DemoApp : public App {
public:
    void onInit() override {
        SceneManager::instance().createEmptyScene("demo");

#ifndef FATE_SHIPPING
        Editor::instance().setAssetRoot(config_.assetsDir);
        Editor::instance().setPaused(true);
#endif

        camera().setPosition({0.0f, 0.0f});
        camera().setZoom(1.0f);

        LOG_INFO("Demo", "FateEngine demo initialized — explore the editor!");
    }

    void onRender(SpriteBatch& batch, Camera& cam) override {
        float tileSize = 32.0f;
        int range = 20;
        Mat4 proj = cam.projectionMatrix();

        batch.begin(proj);
        for (int y = -range; y < range; ++y) {
            for (int x = -range; x < range; ++x) {
                bool dark = (x + y) % 2 == 0;
                Color c = dark ? Color{0.15f, 0.18f, 0.25f, 1.0f}
                               : Color{0.2f, 0.24f, 0.32f, 1.0f};
                batch.drawRect(
                    {x * tileSize, y * tileSize},
                    {tileSize, tileSize},
                    c
                );
            }
        }
        batch.end();
    }

    void onUpdate(float dt) override {}
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
#endif

    if (!app.init(config)) {
        return 1;
    }

    app.run();
    return 0;
}
