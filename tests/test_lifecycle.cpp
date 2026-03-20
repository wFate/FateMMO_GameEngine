#include <doctest/doctest.h>
#include "engine/app.h"

class TestApp : public fate::App {
public:
    int bgCount = 0;
    int fgCount = 0;
    int lowMemCount = 0;

    void onEnterBackground() override { bgCount++; }
    void onEnterForeground() override { fgCount++; }
    void onLowMemory() override { lowMemCount++; }
};

TEST_CASE("App lifecycle state starts Active") {
    TestApp app;
    CHECK(app.lifecycleState() == fate::AppLifecycleState::Active);
    CHECK_FALSE(app.isBackgrounded());
}

TEST_CASE("App lifecycle transitions to Background") {
    TestApp app;
    SDL_Event e{};
    e.type = SDL_APP_WILLENTERBACKGROUND;
    app.handleLifecycleEvent(e);

    CHECK(app.lifecycleState() == fate::AppLifecycleState::Background);
    CHECK(app.isBackgrounded());
    CHECK(app.bgCount == 1);
}

TEST_CASE("App lifecycle transitions back to Active") {
    TestApp app;

    SDL_Event bg{};
    bg.type = SDL_APP_WILLENTERBACKGROUND;
    app.handleLifecycleEvent(bg);
    CHECK(app.isBackgrounded());

    SDL_Event fg{};
    fg.type = SDL_APP_DIDENTERFOREGROUND;
    app.handleLifecycleEvent(fg);
    CHECK_FALSE(app.isBackgrounded());
    CHECK(app.lifecycleState() == fate::AppLifecycleState::Active);
    CHECK(app.fgCount == 1);
}

TEST_CASE("App low memory callback fires without state change") {
    TestApp app;
    SDL_Event e{};
    e.type = SDL_APP_LOWMEMORY;
    app.handleLifecycleEvent(e);

    CHECK(app.lowMemCount == 1);
    CHECK(app.lifecycleState() == fate::AppLifecycleState::Active);
}
