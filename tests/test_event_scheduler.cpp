#include <doctest/doctest.h>
#include "game/shared/event_scheduler.h"

using namespace fate;

TEST_SUITE("EventScheduler") {

TEST_CASE("Event starts in Idle state") {
    EventScheduler scheduler;
    auto id = scheduler.registerEvent({"battlefield", 7200.0f, 300.0f, 900.0f});
    CHECK(scheduler.getState(id) == EventState::Idle);
}

TEST_CASE("Transitions Idle to Signup after interval") {
    EventScheduler scheduler;
    bool signupFired = false;
    auto id = scheduler.registerEvent({"bf", 100.0f, 30.0f, 60.0f});
    scheduler.setCallback(id, EventCallback::OnSignupStart, [&]{ signupFired = true; });
    scheduler.tick(101.0f);
    CHECK(scheduler.getState(id) == EventState::Signup);
    CHECK(signupFired);
}

TEST_CASE("Transitions Signup to Active after signup duration") {
    EventScheduler scheduler;
    bool startFired = false;
    auto id = scheduler.registerEvent({"bf", 10.0f, 5.0f, 15.0f});
    scheduler.setCallback(id, EventCallback::OnEventStart, [&]{ startFired = true; });
    scheduler.tick(11.0f); // Idle -> Signup
    scheduler.tick(16.0f); // Signup -> Active (11 + 5)
    CHECK(scheduler.getState(id) == EventState::Active);
    CHECK(startFired);
}

TEST_CASE("Transitions Active to Idle after active duration") {
    EventScheduler scheduler;
    bool endFired = false;
    auto id = scheduler.registerEvent({"bf", 10.0f, 5.0f, 15.0f});
    scheduler.setCallback(id, EventCallback::OnEventEnd, [&]{ endFired = true; });
    scheduler.tick(11.0f); // -> Signup
    scheduler.tick(16.0f); // -> Active
    scheduler.tick(31.0f); // -> Idle (16 + 15)
    CHECK(scheduler.getState(id) == EventState::Idle);
    CHECK(endFired);
}

TEST_CASE("Cycles repeat") {
    EventScheduler scheduler;
    int signupCount = 0;
    auto id = scheduler.registerEvent({"bf", 10.0f, 2.0f, 3.0f});
    scheduler.setCallback(id, EventCallback::OnSignupStart, [&]{ signupCount++; });
    scheduler.tick(11.0f);  // cycle 1 signup
    scheduler.tick(13.0f);  // cycle 1 active
    scheduler.tick(16.0f);  // cycle 1 end -> idle
    scheduler.tick(27.0f);  // cycle 2 signup (16 + 10 = 26)
    CHECK(signupCount == 2);
}

TEST_CASE("getTimeRemaining returns correct value") {
    EventScheduler scheduler;
    auto id = scheduler.registerEvent({"bf", 10.0f, 5.0f, 15.0f});
    scheduler.tick(11.0f); // -> Signup at t=11, ends at t=16
    float remaining = scheduler.getTimeRemaining(id);
    CHECK(remaining == doctest::Approx(5.0f));
}

TEST_CASE("Multiple events run independently") {
    EventScheduler scheduler;
    auto bf = scheduler.registerEvent({"bf", 100.0f, 10.0f, 20.0f});
    auto ad = scheduler.registerEvent({"ad", 50.0f, 5.0f, 10.0f});
    scheduler.tick(51.0f);
    CHECK(scheduler.getState(bf) == EventState::Idle);
    CHECK(scheduler.getState(ad) == EventState::Signup);
}

} // TEST_SUITE
