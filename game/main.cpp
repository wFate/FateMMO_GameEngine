#include "game/game_app.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    fate::GameApp game;

    fate::AppConfig config;
    config.title = "FateMMO Engine";
    config.windowWidth = 1280;
    config.windowHeight = 720;
    config.vsync = true;
    config.targetFPS = 60;
    config.fixedTimestep = 1.0f / 30.0f;

    if (!game.init(config)) {
        return 1;
    }

    game.run();
    return 0;
}
