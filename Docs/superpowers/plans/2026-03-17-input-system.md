# Input System Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace raw SDL key checks throughout the game with a logical action map system, adding input buffering for responsive TWOM-style combat.

**Architecture:** `ActionMap` translates SDL scancodes → `ActionId` enums with press/hold/release states. `InputBuffer` records combat action presses in a 6-frame circular buffer for skill queuing during GCD. Chat mode switching suppresses gameplay actions while text input is active. The existing `Input` class gains these as members; raw SDL API stays available for editor/ImGui.

**Tech Stack:** C++23, SDL2, doctest

**Spec:** `Docs/superpowers/specs/2026-03-17-input-system-design.md`

**Build:** `"/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build out/build --config Debug 2>&1 | tail -50`
**Tests:** `out/build/Debug/fate_tests.exe`
**CRITICAL: NEVER create .bat files. They hang indefinitely.**

---

## File Structure

### New Files
| File | Responsibility |
|---|---|
| `engine/input/action_map.h` | ActionId enum, ActionBinding, ActionMap class with default bindings and context switching |
| `engine/input/input_buffer.h` | InputBuffer circular buffer with record/consume/advance |
| `tests/test_input.cpp` | Unit tests for action map and input buffer |

### Modified Files
| File | Changes |
|---|---|
| `engine/input/input.h` | Add ActionMap + InputBuffer members, add convenience API (isActionPressed, isActionHeld, consumeBuffered, getCardinalDirection rewrite) |
| `engine/input/input.cpp` | Wire SDL events into ActionMap, update buffer on key-down, update getCardinalDirection to use actions |
| `game/systems/combat_action_system.h` | Replace raw SDL key checks with action queries and consumeBuffered |
| `game/systems/movement_system.h` | Replace raw SDL key checks with isActionHeld |
| `game/game_app.cpp` | Replace raw key checks for UI toggles with action queries |

---

## Task 1: ActionMap — ActionId Enum and Core Class

**Files:**
- Create: `engine/input/action_map.h`
- Create: `tests/test_input.cpp`

- [ ] **Step 1: Create `engine/input/action_map.h`**

```cpp
#pragma once
#include <SDL.h>
#include <cstdint>
#include <array>
#include <vector>

namespace fate {

enum class ActionId : uint16_t {
    MoveUp, MoveDown, MoveLeft, MoveRight,
    Attack,
    SkillSlot1, SkillSlot2, SkillSlot3, SkillSlot4, SkillSlot5,
    SkillPagePrev, SkillPageNext,
    Interact, TargetNearest,
    ToggleInventory, ToggleSkillBar, ToggleQuestLog, ToggleEditor,
    OpenChat, SubmitChat,
    Confirm, Cancel, Pause,
    COUNT
};

struct ActionBinding {
    SDL_Scancode primary = SDL_SCANCODE_UNKNOWN;
    SDL_Scancode secondary = SDL_SCANCODE_UNKNOWN;
};

enum class InputContext : uint8_t {
    Gameplay,
    Chat
};

class ActionMap {
public:
    ActionMap() { initDefaultBindings(); }

    // Call at start of frame — transition Pressed→Down, Released→Up
    void beginFrame() {
        for (auto& s : pressed_) s = false;
        for (auto& s : released_) s = false;
    }

    // Process a key event — translate scancode to action
    void onKeyDown(SDL_Scancode scancode) {
        if (context_ == InputContext::Chat) return; // suppress in chat mode
        for (int i = 0; i < (int)ActionId::COUNT; ++i) {
            auto& b = bindings_[i];
            if (scancode == b.primary || scancode == b.secondary) {
                pressed_[i] = true;
                held_[i] = true;
            }
        }
    }

    void onKeyUp(SDL_Scancode scancode) {
        for (int i = 0; i < (int)ActionId::COUNT; ++i) {
            auto& b = bindings_[i];
            if (scancode == b.primary || scancode == b.secondary) {
                released_[i] = true;
                held_[i] = false;
            }
        }
    }

    bool isPressed(ActionId id) const { return pressed_[(int)id]; }
    bool isHeld(ActionId id) const { return held_[(int)id]; }
    bool isReleased(ActionId id) const { return released_[(int)id]; }

    // Context switching
    void setContext(InputContext ctx) { context_ = ctx; }
    InputContext context() const { return context_; }

    const ActionBinding& getBinding(ActionId id) const { return bindings_[(int)id]; }

private:
    static constexpr int N = (int)ActionId::COUNT;
    std::array<ActionBinding, N> bindings_{};
    std::array<bool, N> pressed_{};
    std::array<bool, N> held_{};
    std::array<bool, N> released_{};
    InputContext context_ = InputContext::Gameplay;

    void initDefaultBindings() {
        auto bind = [&](ActionId id, SDL_Scancode primary, SDL_Scancode secondary = SDL_SCANCODE_UNKNOWN) {
            bindings_[(int)id] = { primary, secondary };
        };
        bind(ActionId::MoveUp,    SDL_SCANCODE_W, SDL_SCANCODE_UP);
        bind(ActionId::MoveDown,  SDL_SCANCODE_S, SDL_SCANCODE_DOWN);
        bind(ActionId::MoveLeft,  SDL_SCANCODE_A, SDL_SCANCODE_LEFT);
        bind(ActionId::MoveRight, SDL_SCANCODE_D, SDL_SCANCODE_RIGHT);
        bind(ActionId::Attack,    SDL_SCANCODE_SPACE);
        bind(ActionId::SkillSlot1, SDL_SCANCODE_1);
        bind(ActionId::SkillSlot2, SDL_SCANCODE_2);
        bind(ActionId::SkillSlot3, SDL_SCANCODE_3);
        bind(ActionId::SkillSlot4, SDL_SCANCODE_4);
        bind(ActionId::SkillSlot5, SDL_SCANCODE_5);
        bind(ActionId::SkillPagePrev, SDL_SCANCODE_LEFTBRACKET);
        bind(ActionId::SkillPageNext, SDL_SCANCODE_RIGHTBRACKET);
        bind(ActionId::Interact,       SDL_SCANCODE_E);
        bind(ActionId::TargetNearest,  SDL_SCANCODE_TAB);
        bind(ActionId::ToggleInventory, SDL_SCANCODE_I);
        bind(ActionId::ToggleSkillBar,  SDL_SCANCODE_K);
        bind(ActionId::ToggleQuestLog,  SDL_SCANCODE_L);
        bind(ActionId::ToggleEditor,    SDL_SCANCODE_F3);
        bind(ActionId::OpenChat,  SDL_SCANCODE_RETURN);
        bind(ActionId::SubmitChat, SDL_SCANCODE_RETURN);
        bind(ActionId::Confirm,   SDL_SCANCODE_RETURN);
        bind(ActionId::Cancel,    SDL_SCANCODE_ESCAPE);
        bind(ActionId::Pause,     SDL_SCANCODE_ESCAPE);
    }
};

} // namespace fate
```

- [ ] **Step 2: Create `tests/test_input.cpp`**

```cpp
#include <doctest/doctest.h>
#include "engine/input/action_map.h"

TEST_CASE("ActionMap default bindings") {
    fate::ActionMap map;
    auto b = map.getBinding(fate::ActionId::MoveUp);
    CHECK(b.primary == SDL_SCANCODE_W);
    CHECK(b.secondary == SDL_SCANCODE_UP);
}

TEST_CASE("ActionMap press and hold") {
    fate::ActionMap map;
    map.onKeyDown(SDL_SCANCODE_SPACE);
    CHECK(map.isPressed(fate::ActionId::Attack));
    CHECK(map.isHeld(fate::ActionId::Attack));

    map.beginFrame();
    CHECK_FALSE(map.isPressed(fate::ActionId::Attack)); // edge cleared
    CHECK(map.isHeld(fate::ActionId::Attack));           // still held
}

TEST_CASE("ActionMap release") {
    fate::ActionMap map;
    map.onKeyDown(SDL_SCANCODE_W);
    map.beginFrame();
    map.onKeyUp(SDL_SCANCODE_W);
    CHECK(map.isReleased(fate::ActionId::MoveUp));
    CHECK_FALSE(map.isHeld(fate::ActionId::MoveUp));
}

TEST_CASE("ActionMap secondary key works") {
    fate::ActionMap map;
    map.onKeyDown(SDL_SCANCODE_UP); // secondary for MoveUp
    CHECK(map.isPressed(fate::ActionId::MoveUp));
    CHECK(map.isHeld(fate::ActionId::MoveUp));
}

TEST_CASE("ActionMap chat mode suppresses gameplay") {
    fate::ActionMap map;
    map.setContext(fate::InputContext::Chat);
    map.onKeyDown(SDL_SCANCODE_W);
    CHECK_FALSE(map.isPressed(fate::ActionId::MoveUp));
    CHECK_FALSE(map.isHeld(fate::ActionId::MoveUp));
}
```

- [ ] **Step 3: Build and run tests**

- [ ] **Step 4: Commit**

```bash
git add engine/input/action_map.h tests/test_input.cpp
git commit -m "feat(input): ActionMap with ActionId enum, default bindings, context switching"
```

---

## Task 2: InputBuffer — Circular Buffer with Consume

**Files:**
- Create: `engine/input/input_buffer.h`
- Modify: `tests/test_input.cpp`

- [ ] **Step 1: Create `engine/input/input_buffer.h`**

```cpp
#pragma once
#include "engine/input/action_map.h"
#include <array>
#include <cstdint>

namespace fate {

class InputBuffer {
public:
    void record(ActionId action) {
        entries_[writePos_] = { frameCounter_, action, false };
        writePos_ = (writePos_ + 1) % CAPACITY;
        if (count_ < CAPACITY) ++count_;
    }

    // Check for an unconsumed entry within the last windowFrames.
    // If found, marks consumed and returns true.
    bool consume(ActionId action, int windowFrames = 6) {
        uint32_t minFrame = (frameCounter_ >= (uint32_t)windowFrames)
            ? frameCounter_ - windowFrames : 0;

        // Scan backward from most recent
        for (int i = 0; i < (int)count_; ++i) {
            int idx = ((int)writePos_ - 1 - i + CAPACITY) % CAPACITY;
            auto& e = entries_[idx];
            if (e.frame < minFrame) break; // too old
            if (e.action == action && !e.consumed) {
                e.consumed = true;
                return true;
            }
        }
        return false;
    }

    void advanceFrame() { ++frameCounter_; }
    uint32_t currentFrame() const { return frameCounter_; }

private:
    static constexpr int CAPACITY = 32;

    struct Entry {
        uint32_t frame = 0;
        ActionId action = ActionId::COUNT;
        bool consumed = false;
    };

    std::array<Entry, CAPACITY> entries_{};
    int writePos_ = 0;
    int count_ = 0;
    uint32_t frameCounter_ = 0;
};

} // namespace fate
```

- [ ] **Step 2: Add input buffer tests to `tests/test_input.cpp`**

```cpp
#include "engine/input/input_buffer.h"

TEST_CASE("InputBuffer record and consume") {
    fate::InputBuffer buf;
    buf.record(fate::ActionId::Attack);
    CHECK(buf.consume(fate::ActionId::Attack));
    CHECK_FALSE(buf.consume(fate::ActionId::Attack)); // already consumed
}

TEST_CASE("InputBuffer expires after window") {
    fate::InputBuffer buf;
    buf.record(fate::ActionId::Attack);
    for (int i = 0; i < 7; ++i) buf.advanceFrame();
    CHECK_FALSE(buf.consume(fate::ActionId::Attack, 6)); // too old
}

TEST_CASE("InputBuffer consumes within window") {
    fate::InputBuffer buf;
    buf.record(fate::ActionId::SkillSlot1);
    for (int i = 0; i < 4; ++i) buf.advanceFrame();
    CHECK(buf.consume(fate::ActionId::SkillSlot1, 6)); // still valid
}

TEST_CASE("InputBuffer multiple actions") {
    fate::InputBuffer buf;
    buf.record(fate::ActionId::Attack);
    buf.advanceFrame();
    buf.record(fate::ActionId::SkillSlot1);
    CHECK(buf.consume(fate::ActionId::SkillSlot1));
    CHECK(buf.consume(fate::ActionId::Attack));
}
```

- [ ] **Step 3: Build, run tests, commit**

```bash
git add engine/input/input_buffer.h tests/test_input.cpp
git commit -m "feat(input): InputBuffer circular buffer with 6-frame consume window"
```

---

## Task 3: Wire ActionMap + InputBuffer into Input Class

**Files:**
- Modify: `engine/input/input.h`
- Modify: `engine/input/input.cpp`

- [ ] **Step 1: Read current input.h and input.cpp**

- [ ] **Step 2: Add ActionMap and InputBuffer members to Input class**

In `input.h`, add includes and members:
```cpp
#include "engine/input/action_map.h"
#include "engine/input/input_buffer.h"

// In class Input, public:
// Action-based API (game systems use these)
bool isActionPressed(ActionId id) const { return actionMap_.isPressed(id); }
bool isActionHeld(ActionId id) const { return actionMap_.isHeld(id); }
bool isActionReleased(ActionId id) const { return actionMap_.isReleased(id); }
bool consumeBuffered(ActionId id, int window = 6) { return inputBuffer_.consume(id, window); }

// Context switching
void setChatMode(bool enabled);
bool isChatMode() const { return actionMap_.context() == InputContext::Chat; }

// In class Input, private:
ActionMap actionMap_;
InputBuffer inputBuffer_;
```

- [ ] **Step 3: Update Input::beginFrame()**

Add at the start of `beginFrame()`:
```cpp
actionMap_.beginFrame();
inputBuffer_.advanceFrame();
```

- [ ] **Step 4: Update Input::processEvent() to feed ActionMap**

In the `SDL_KEYDOWN` case, after updating `keys_`, add:
```cpp
if (!event.key.repeat) {
    actionMap_.onKeyDown(event.key.keysym.scancode);
    // Buffer combat actions
    ActionId action = scancodeToBufferedAction(event.key.keysym.scancode);
    if (action != ActionId::COUNT) {
        inputBuffer_.record(action);
    }
}
```

In the `SDL_KEYUP` case, add:
```cpp
actionMap_.onKeyUp(event.key.keysym.scancode);
```

Add a private helper `scancodeToBufferedAction()` that checks if the scancode matches any of Attack, SkillSlot1-5 bindings and returns the ActionId (or COUNT if not buffered).

- [ ] **Step 5: Rewrite getCardinalDirection() to use actions**

```cpp
Direction Input::getCardinalDirection() const {
    bool up    = actionMap_.isHeld(ActionId::MoveUp);
    bool down  = actionMap_.isHeld(ActionId::MoveDown);
    bool left  = actionMap_.isHeld(ActionId::MoveLeft);
    bool right = actionMap_.isHeld(ActionId::MoveRight);

    if (up && !down) return Direction::Up;
    if (down && !up) return Direction::Down;
    if (left && !right) return Direction::Left;
    if (right && !left) return Direction::Right;
    return Direction::None;
}
```

- [ ] **Step 6: Implement setChatMode()**

```cpp
void Input::setChatMode(bool enabled) {
    if (enabled) {
        actionMap_.setContext(InputContext::Chat);
        SDL_StartTextInput();
    } else {
        actionMap_.setContext(InputContext::Gameplay);
        SDL_StopTextInput();
    }
}
```

- [ ] **Step 7: Build, run tests, commit**

```bash
git add engine/input/input.h engine/input/input.cpp
git commit -m "feat(input): wire ActionMap and InputBuffer into Input class"
```

---

## Task 4: Migrate Game Systems to Action API

**Files:**
- Modify: `game/systems/movement_system.h`
- Modify: `game/systems/combat_action_system.h`
- Modify: `game/game_app.cpp`

- [ ] **Step 1: Read each file to find raw SDL key checks**

Search for `isKeyDown`, `isKeyPressed`, `SDL_SCANCODE_` in each file.

- [ ] **Step 2: Migrate movement_system.h**

Replace raw `input.isKeyDown(SDL_SCANCODE_W)` etc. with `input.isActionHeld(ActionId::MoveUp)` etc. Or if it uses `getCardinalDirection()`, it already works since that was rewritten in Task 3.

- [ ] **Step 3: Migrate combat_action_system.h**

Replace:
- `input.isKeyDown(SDL_SCANCODE_SPACE)` → `input.consumeBuffered(ActionId::Attack)`
- Any skill key checks → `input.consumeBuffered(ActionId::SkillSlotN)`
- Keep mouse click checks (`isMousePressed`) as raw SDL — not migrated in this phase

- [ ] **Step 4: Migrate game_app.cpp**

Replace UI toggle checks:
- `isKeyPressed(SDL_SCANCODE_I)` → `input.isActionPressed(ActionId::ToggleInventory)`
- `isKeyPressed(SDL_SCANCODE_K)` → `input.isActionPressed(ActionId::ToggleSkillBar)`
- `isKeyPressed(SDL_SCANCODE_L)` → `input.isActionPressed(ActionId::ToggleQuestLog)`
- `isKeyPressed(SDL_SCANCODE_F3)` → `input.isActionPressed(ActionId::ToggleEditor)`

- [ ] **Step 5: Build, run game, verify all inputs work**

Test: WASD moves player, Space attacks, 1-5 skills, I/K/L toggles, F3 editor. Verify combat feels the same (or better with buffering).

- [ ] **Step 6: Commit**

```bash
git add game/systems/movement_system.h game/systems/combat_action_system.h game/game_app.cpp
git commit -m "feat(game): migrate all game systems from raw SDL to action map API"
```

---

## Task 5: Final Integration & Verification

- [ ] **Step 1: Full clean build**

- [ ] **Step 2: Run all tests**

- [ ] **Step 3: Verify gameplay**

- Movement: WASD and arrow keys both work
- Combat: Space to attack, 1-5 for skills, input buffering queues during GCD
- UI: I inventory, K skill bar, L quest log, F3 editor
- Chat mode: Enter opens chat, typing doesn't move player, Enter/Escape exits chat
- Editor: F3 opens editor, gameplay actions suppressed while editor active

- [ ] **Step 4: Commit any remaining fixes**

```bash
git commit -m "feat(input): complete input system with action maps and combat buffering"
```
