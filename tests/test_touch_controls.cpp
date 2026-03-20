#include <doctest/doctest.h>
#include "game/ui/touch_controls.h"
#include "engine/input/action_map.h"

using namespace fate;

TEST_CASE("ActionMap programmatic injection: setActionPressed") {
    ActionMap am;
    am.beginFrame();

    am.setActionPressed(ActionId::Attack);
    CHECK(am.isPressed(ActionId::Attack));
    CHECK(am.isHeld(ActionId::Attack));
    CHECK_FALSE(am.isReleased(ActionId::Attack));
}

TEST_CASE("ActionMap programmatic injection: setActionReleased") {
    ActionMap am;
    am.beginFrame();

    // Press then release
    am.setActionPressed(ActionId::MoveUp);
    am.setActionReleased(ActionId::MoveUp);
    CHECK(am.isReleased(ActionId::MoveUp));
    CHECK_FALSE(am.isHeld(ActionId::MoveUp));
}

TEST_CASE("ActionMap programmatic injection: setActionHeld") {
    ActionMap am;
    am.beginFrame();

    am.setActionHeld(ActionId::MoveLeft, true);
    CHECK(am.isHeld(ActionId::MoveLeft));
    CHECK_FALSE(am.isPressed(ActionId::MoveLeft));

    am.setActionHeld(ActionId::MoveLeft, false);
    CHECK_FALSE(am.isHeld(ActionId::MoveLeft));
}

TEST_CASE("TouchControls enabled/disabled toggle") {
    auto& tc = TouchControls::instance();
    tc.setEnabled(false);
    CHECK_FALSE(tc.isEnabled());
    tc.setEnabled(true);
    CHECK(tc.isEnabled());
    // Restore default (disabled on desktop)
    tc.setEnabled(false);
}

TEST_CASE("TouchControls isInsideCircle") {
    auto& tc = TouchControls::instance();
    // Point exactly at center
    CHECK(tc.isInsideCircle(100.0f, 100.0f, 100.0f, 100.0f, 50.0f));
    // Point on the edge
    CHECK(tc.isInsideCircle(150.0f, 100.0f, 100.0f, 100.0f, 50.0f));
    // Point outside
    CHECK_FALSE(tc.isInsideCircle(151.0f, 100.0f, 100.0f, 100.0f, 50.0f));
    // Point inside at diagonal
    CHECK(tc.isInsideCircle(130.0f, 130.0f, 100.0f, 100.0f, 50.0f));
    // Point outside at diagonal
    CHECK_FALSE(tc.isInsideCircle(140.0f, 140.0f, 100.0f, 100.0f, 50.0f));
}

TEST_CASE("TouchControls classifyDpadDirection") {
    auto& tc = TouchControls::instance();
    tc.setEnabled(true);

    // First call update to set dpad layout positions (1280x720 viewport)
    ActionMap am;
    tc.update(am, 0, 0, 1280, 720);

    float cx = tc.dpadCenterX();
    float cy = tc.dpadCenterY();
    float r  = tc.dpadRadius();

    // Right: touch to the right of center
    CHECK(tc.classifyDpadDirection(cx + r * 0.5f, cy) == Direction::Right);
    // Left: touch to the left of center
    CHECK(tc.classifyDpadDirection(cx - r * 0.5f, cy) == Direction::Left);
    // Down: touch below center (positive Y = down in screen space)
    CHECK(tc.classifyDpadDirection(cx, cy + r * 0.5f) == Direction::Down);
    // Up: touch above center
    CHECK(tc.classifyDpadDirection(cx, cy - r * 0.5f) == Direction::Up);
    // Dead zone: touch very close to center returns None
    CHECK(tc.classifyDpadDirection(cx, cy) == Direction::None);

    tc.setEnabled(false);
}

TEST_CASE("TouchControls update with no touches does not set world tap") {
    auto& tc = TouchControls::instance();
    tc.setEnabled(true);

    ActionMap am;
    tc.update(am, 0, 0, 1280, 720);
    CHECK_FALSE(tc.hasWorldTap());

    tc.setEnabled(false);
}

TEST_CASE("TouchControls disabled skips update") {
    auto& tc = TouchControls::instance();
    tc.setEnabled(false);

    ActionMap am;
    am.beginFrame();
    // When disabled, update should be a no-op — no actions injected
    am.setActionHeld(ActionId::MoveUp, false);
    tc.update(am, 0, 0, 1280, 720);
    CHECK_FALSE(am.isHeld(ActionId::MoveUp));
    CHECK_FALSE(am.isHeld(ActionId::MoveDown));
    CHECK_FALSE(am.isHeld(ActionId::MoveLeft));
    CHECK_FALSE(am.isHeld(ActionId::MoveRight));
}
