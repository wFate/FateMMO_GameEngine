#include "game/game_app.h"
#include <filesystem>

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    // Ensure CWD is the project root so relative paths (assets/, etc.) resolve correctly.
    // VS sets an unpredictable CWD; FATE_SOURCE_DIR is the canonical project root from CMake.
#ifdef FATE_SOURCE_DIR
    std::filesystem::current_path(FATE_SOURCE_DIR);
#endif

    fate::GameApp game;

    fate::AppConfig config;
#ifdef FATE_SHIPPING
    config.title = "FateMMO";
#else
    config.title = "FateMMO Engine";
#endif
    config.windowWidth = 1280;
    config.windowHeight = 720;
    config.vsync = true;
    config.targetFPS = 120;
    config.fixedTimestep = 1.0f / 30.0f;

    if (!game.init(config))
    {
        return 1;
    }

    game.run();
    return 0;
}
