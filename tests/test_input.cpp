#include <doctest/doctest.h>
#include "engine/input/action_map.h"
#include "engine/input/input_buffer.h"

using namespace fate;

TEST_CASE("ActionMap: default bindings") {
    ActionMap map;

    const auto& moveUp = map.binding(ActionId::MoveUp);
    CHECK(moveUp.primary   == SDL_SCANCODE_W);
    CHECK(moveUp.secondary == SDL_SCANCODE_UP);

    const auto& attack = map.binding(ActionId::Attack);
    CHECK(attack.primary == SDL_SCANCODE_SPACE);
    CHECK(attack.secondary == SDL_SCANCODE_UNKNOWN);
}

TEST_CASE("ActionMap: press and hold") {
    ActionMap map;

    map.onKeyDown(SDL_SCANCODE_SPACE);
    CHECK(map.isPressed(ActionId::Attack));
    CHECK(map.isHeld(ActionId::Attack));

    // After beginFrame, pressed clears but held remains
    map.beginFrame();
    CHECK_FALSE(map.isPressed(ActionId::Attack));
    CHECK(map.isHeld(ActionId::Attack));
}

TEST_CASE("ActionMap: release") {
    ActionMap map;

    map.onKeyDown(SDL_SCANCODE_SPACE);
    map.beginFrame();
    map.onKeyUp(SDL_SCANCODE_SPACE);

    CHECK(map.isReleased(ActionId::Attack));
    CHECK_FALSE(map.isHeld(ActionId::Attack));
}

TEST_CASE("ActionMap: secondary key triggers action") {
    ActionMap map;

    map.onKeyDown(SDL_SCANCODE_UP);
    CHECK(map.isPressed(ActionId::MoveUp));
    CHECK(map.isHeld(ActionId::MoveUp));
}

TEST_CASE("ActionMap: chat context suppresses gameplay actions") {
    ActionMap map;

    map.setContext(InputContext::Chat);
    map.onKeyDown(SDL_SCANCODE_W);

    CHECK_FALSE(map.isPressed(ActionId::MoveUp));
    CHECK_FALSE(map.isHeld(ActionId::MoveUp));

    // SubmitChat (Return) should still work in Chat context
    map.onKeyDown(SDL_SCANCODE_RETURN);
    CHECK(map.isPressed(ActionId::SubmitChat));
}

// ============================================================================
// InputBuffer tests
// ============================================================================

TEST_CASE("InputBuffer: record and consume") {
    InputBuffer buf;
    buf.record(ActionId::Attack);

    // First consume succeeds.
    CHECK(buf.consume(ActionId::Attack));

    // Second consume fails — already consumed.
    CHECK_FALSE(buf.consume(ActionId::Attack));
}

TEST_CASE("InputBuffer: expires after window") {
    InputBuffer buf;
    buf.record(ActionId::Attack);

    // Advance 7 frames — beyond the default 6-frame window.
    for (int i = 0; i < 7; ++i) buf.advanceFrame();

    CHECK_FALSE(buf.consume(ActionId::Attack));
}

TEST_CASE("InputBuffer: within window") {
    InputBuffer buf;
    buf.record(ActionId::Attack);

    // Advance 4 frames — still inside the 6-frame window.
    for (int i = 0; i < 4; ++i) buf.advanceFrame();

    CHECK(buf.consume(ActionId::Attack));
}

TEST_CASE("InputBuffer: multiple actions") {
    InputBuffer buf;
    buf.record(ActionId::Attack);
    buf.record(ActionId::SkillSlot1);

    // Both are independently consumable.
    CHECK(buf.consume(ActionId::Attack));
    CHECK(buf.consume(ActionId::SkillSlot1));

    // Neither can be consumed again.
    CHECK_FALSE(buf.consume(ActionId::Attack));
    CHECK_FALSE(buf.consume(ActionId::SkillSlot1));
}
