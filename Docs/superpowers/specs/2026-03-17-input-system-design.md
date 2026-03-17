# Input System Design Spec

**Date:** 2026-03-17
**Scope:** Action map abstraction, input buffering for combat, chat mode switching
**Phase:** Phase 2a of research gap implementation
**Depends on:** Core engine (completed)

---

## Overview

Replaces raw SDL key checks throughout the game with a logical action map system. Physical inputs (SDL scancodes) map to `ActionId` enums. Game systems query actions, never raw keys. A circular input buffer records combat action presses for 6 frames, enabling queued skill activation during GCD/animations. Chat mode switching suppresses gameplay actions while the text input is active.

**Design decisions:**
- Hardcoded default bindings (no keybinding file, no player rebinding — game targets mobile)
- Keyboard only for now; action map abstraction enables touch input later with zero game code changes
- Input buffering for combat actions only (Attack, SkillSlots); movement and UI toggles are not buffered
- Skill slot assignment (which skills go where) is a separate UI feature already implemented in skill_bar_ui.cpp

---

## Section 1: Action Map

### ActionId Enum

```cpp
enum class ActionId : uint16_t {
    // Movement (held, not buffered)
    MoveUp, MoveDown, MoveLeft, MoveRight,

    // Combat (buffered)
    Attack,
    SkillSlot1, SkillSlot2, SkillSlot3, SkillSlot4, SkillSlot5,
    SkillPagePrev, SkillPageNext,

    // Interaction (press)
    Interact, TargetNearest,

    // UI toggles (press)
    ToggleInventory, ToggleSkillBar, ToggleQuestLog, ToggleEditor,

    // Chat
    OpenChat, SubmitChat,

    // System
    Confirm, Cancel, Pause,

    COUNT
};
```

### Action States

Each action has three queryable states per frame:
- `isPressed(ActionId)` — true on the frame the key goes down (edge-triggered)
- `isHeld(ActionId)` — true every frame the key is held (level-triggered)
- `isReleased(ActionId)` — true on the frame the key goes up (edge-triggered)
- `value(ActionId) → float` — 0.0 or 1.0 for digital; reserved for future analog touch D-Pad. No game system should call this now.
- `consumeBuffered(ActionId)` — convenience on Input, delegates to `InputBuffer::consume()`. For combat actions queued during GCD.
- `getCardinalDirection()` — retained on Input, reimplemented to read `isHeld(MoveUp/Down/Left/Right)` instead of raw SDL. Preserves TWOM priority logic (vertical > horizontal, opposites cancel).

### Action Maps (Context Switching)

Two maps with mutual exclusion:

**Gameplay map** (default active):
- All movement, combat, interaction, and UI toggle actions are active
- Suppressed when chat is open. Editor suppression is handled upstream: the existing `app.cpp` event-level check (`Editor::instance().wantsKeyboard()`) prevents key events from reaching the action map. The action map has NO dependency on Editor.

**Chat map:**
- Activated when `ActionId::OpenChat` fires (Enter key)
- Calls `SDL_StartTextInput()` to enable text composition
- All keyboard input routes to ImGui text buffer
- In Chat mode, `processEvent()` skips action map translation for `SDL_KEYDOWN`/`SDL_KEYUP` events entirely, while still forwarding `SDL_TEXTINPUT` events to ImGui for text composition
- Deactivated on Enter (sends message via `SubmitChat`) or Escape (cancel)
- Calls `SDL_StopTextInput()` and re-activates Gameplay map

### Default Bindings

Hardcoded in a `getDefaultBindings()` function, no external file:

| Action | Primary Key | Notes |
|---|---|---|
| MoveUp | W / Up Arrow | WASD + arrow keys |
| MoveDown | S / Down Arrow | |
| MoveLeft | A / Left Arrow | |
| MoveRight | D / Right Arrow | |
| Attack | Space | TWOM Option B |
| SkillSlot1-5 | 1-5 | Matches skill bar slots |
| SkillPagePrev | [ | |
| SkillPageNext | ] | |
| Interact | E | Click NPC |
| TargetNearest | Tab | |
| ToggleInventory | I | |
| ToggleSkillBar | K | |
| ToggleQuestLog | L | |
| ToggleEditor | F3 | |
| OpenChat | Return | |
| Confirm | Return | |
| Cancel | Escape | |
| Pause | Escape | |

**Key disambiguation rules:**
- **Return key:** `OpenChat` fires only in Gameplay map when no modal/dialogue is active. `Confirm` fires only when a dialogue/confirmation modal is active. In Chat map, Return fires `SubmitChat`.
- **Escape key:** `Cancel` fires first — clears target if one is selected, closes any open modal/dialogue. `Pause` fires only if no target is selected and no modal is open (opens pause/menu). In Chat map, Escape closes chat and switches back to Gameplay map.
- **Multiple keys per action:** Movement supports both WASD and arrow keys as bindings for the same actions (MoveUp = W or Up arrow, etc.). The binding table supports a primary and secondary key per action.

---

## Section 2: Input Buffer

### Circular Buffer Design

A fixed-size circular buffer (capacity 32) of input events:

```cpp
struct BufferedInput {
    uint32_t frame;     // frame number when recorded
    ActionId action;    // which action was pressed
    bool consumed;      // set true after consume() matches it
};
```

Buffer depth: **6 frames** (~100ms at 60fps). This matches fighting game standards (Smash Ultimate uses 9, Street Fighter 6 uses 4; 6 is a good middle ground for MMORPG skill queuing).

### API

```cpp
class InputBuffer {
    void record(ActionId action, uint32_t frame);  // called on key-down
    bool consume(ActionId action, int windowFrames = 6);  // check + consume
    void advanceFrame();  // increment frame counter
};
```

`consume(action, window)` scans backward through the buffer for an unconsumed entry matching `action` within the last `window` frames. If found, marks it consumed and returns true. This prevents double-firing.

### What Gets Buffered

- **Buffered:** Attack, SkillSlot1-5 — actions that can be queued during GCD/animation locks
- **NOT buffered:** Movement (continuous hold state, not discrete presses), UI toggles (instant, no GCD), chat input, system actions

### Integration with Combat

The `CombatActionSystem` currently checks:
```cpp
if (input.isKeyDown(SDL_SCANCODE_SPACE)) { /* attack */ }
```

This becomes:
```cpp
if (input.consumeBuffered(ActionId::Attack)) { /* attack */ }
```

The GCD/animation lock already exists in the combat system. Buffering just means the player's press is remembered — when the lock expires, the buffered action fires on the next frame instead of requiring a new keypress.

---

## Section 3: Integration & Migration

### New Files

| File | Responsibility |
|---|---|
| `engine/input/action_map.h` | ActionId enum, ActionState per action, ActionMap class, default bindings, context switching |
| `engine/input/input_buffer.h` | InputBuffer circular buffer, record/consume/advance |

### Modified Files

| File | Changes |
|---|---|
| `engine/input/input.h` / `input.cpp` | Add ActionMap + InputBuffer as members, process SDL events into action states each frame, update buffer on key-down |
| `game/systems/combat_action_system.h` | Replace `isKeyDown(SDL_SCANCODE_SPACE)` with `input.consumeBuffered(ActionId::Attack)`, replace skill key checks with `consumeBuffered(ActionId::SkillSlotN)` |
| `game/systems/movement_system.h` | Replace `isKeyDown(SDL_SCANCODE_W)` with `input.isHeld(ActionId::MoveUp)` etc. |
| `game/game_app.cpp` | Replace raw key checks for UI toggles (I, K, L, F3) with action queries |

**Explicitly NOT migrated (stays as raw SDL):**
- Mouse input (`isMousePressed`, `getMousePosition`) — used by `combat_action_system.h` for click-to-target and `npc_interaction_system.h` for click-to-interact. These remain raw SDL since the action map is keyboard-only in this phase. Touch equivalents will be added when mobile input is implemented.
- ImGui input — Dear ImGui reads raw SDL events directly via its backend. No migration needed.

**Note on `gameplay_system.h`:** This file has no raw input calls. It is purely tick-based logic (regen, PK decay, status effects). No changes needed.

**Note on skill slot activation:** `SkillSlot1-5` actions are defined and buffered, but the consume sites (skill activation logic in CombatActionSystem) may not exist yet for all skills. The implementer should add `consumeBuffered(ActionId::SkillSlotN)` calls alongside the existing attack logic in CombatActionSystem, wiring them to `SkillManager::tryActivateSlot()`.

### Migration Strategy

The existing `Input` class gains `ActionMap` and `InputBuffer` as members. Raw SDL state (`isKeyDown`, `getMousePosition`, `isMouseButtonDown`) remains available for editor/ImGui use — these are not migrated since ImGui needs raw input.

Game systems migrate from:
```cpp
input.isKeyDown(SDL_SCANCODE_W)  →  input.isHeld(ActionId::MoveUp)
input.isKeyDown(SDL_SCANCODE_I)  →  input.isPressed(ActionId::ToggleInventory)
input.isKeyDown(SDL_SCANCODE_SPACE)  →  input.consumeBuffered(ActionId::Attack)
```

### Frame Loop Integration

In `Input::update()` (called once per frame before systems):
1. `actionMap_.beginFrame()` — reset pressed/released edge flags
2. Process SDL events — for each `SDL_KEYDOWN`/`SDL_KEYUP`, look up the action binding and update state
3. For key-down events on buffered actions, call `inputBuffer_.record(action, frameCount)`
4. `inputBuffer_.advanceFrame()`

---

## What This Does NOT Include

- Player keybinding customization (game targets mobile, bindings are fixed)
- Touch input routing (deferred to mobile build — action map abstraction supports it)
- Input recording/replay (future automated testing feature)
- Gamepad support (future)
- Composite bindings (Shift+key combos — not needed for TWOM-style)
- Analog movement (future touch D-Pad provides analog; keyboard is digital 0/1)
