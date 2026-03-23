#pragma once
#include <SDL.h>
#include <array>
#include <cstdint>

namespace fate {

// ============================================================================
// ActionId — every bindable game action
// ============================================================================
enum class ActionId : uint8_t {
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Attack,
    SkillSlot1,
    SkillSlot2,
    SkillSlot3,
    SkillSlot4,
    SkillSlot5,
    SkillPagePrev,
    SkillPageNext,
    Interact,
    TargetNearest,
    ToggleInventory,
    ToggleSkillBar,
    ToggleQuestLog,
    OpenChat,
    SubmitChat,
    Confirm,
    Cancel,
    Pause,
    COUNT
};

constexpr size_t ACTION_COUNT = static_cast<size_t>(ActionId::COUNT);

// ============================================================================
// ActionBinding — primary + secondary scancode for one action
// ============================================================================
struct ActionBinding {
    SDL_Scancode primary   = SDL_SCANCODE_UNKNOWN;
    SDL_Scancode secondary = SDL_SCANCODE_UNKNOWN;
};

// ============================================================================
// InputContext — which actions are active
// ============================================================================
enum class InputContext : uint8_t {
    Gameplay,
    Chat
};

// ============================================================================
// ActionMap — translates scancodes into action state via bindings
// ============================================================================
class ActionMap {
public:
    ActionMap() { initDefaultBindings(); }

    // --- per-frame lifecycle ------------------------------------------------

    /// Call at the start of every frame to clear edge-triggered flags.
    void beginFrame() {
        pressed_.fill(false);
        released_.fill(false);
    }

    /// Translate a key-down event into action state.
    void onKeyDown(SDL_Scancode sc) {
        for (size_t i = 0; i < ACTION_COUNT; ++i) {
            const auto& b = bindings_[i];
            if (sc != b.primary && sc != b.secondary) continue;

            // In Chat context, suppress everything except chat-related actions
            if (context_ == InputContext::Chat) {
                auto id = static_cast<ActionId>(i);
                if (id != ActionId::SubmitChat &&
                    id != ActionId::Cancel) {
                    continue;
                }
            }

            pressed_[i] = true;
            held_[i]    = true;
        }
    }

    /// Translate a key-up event into action state.
    void onKeyUp(SDL_Scancode sc) {
        for (size_t i = 0; i < ACTION_COUNT; ++i) {
            const auto& b = bindings_[i];
            if (sc != b.primary && sc != b.secondary) continue;

            released_[i] = true;
            held_[i]     = false;
        }
    }

    // --- programmatic input injection (touch controls, gamepad, etc.) -------

    void setActionPressed(ActionId id) {
        size_t i = static_cast<size_t>(id);
        pressed_[i] = true;
        held_[i] = true;
    }
    void setActionReleased(ActionId id) {
        size_t i = static_cast<size_t>(id);
        released_[i] = true;
        held_[i] = false;
    }
    void setActionHeld(ActionId id, bool held) {
        held_[static_cast<size_t>(id)] = held;
    }

    // --- queries ------------------------------------------------------------

    bool isPressed(ActionId id)  const { return pressed_[static_cast<size_t>(id)]; }
    bool isHeld(ActionId id)     const { return held_[static_cast<size_t>(id)]; }
    bool isReleased(ActionId id) const { return released_[static_cast<size_t>(id)]; }

    // --- context ------------------------------------------------------------

    void         setContext(InputContext ctx) { context_ = ctx; }
    InputContext context() const              { return context_; }

    // --- binding access -----------------------------------------------------

    const ActionBinding& binding(ActionId id) const {
        return bindings_[static_cast<size_t>(id)];
    }

private:
    void initDefaultBindings() {
        auto bind = [&](ActionId id, SDL_Scancode primary,
                        SDL_Scancode secondary = SDL_SCANCODE_UNKNOWN) {
            bindings_[static_cast<size_t>(id)] = {primary, secondary};
        };

        // Movement
        bind(ActionId::MoveUp,    SDL_SCANCODE_W, SDL_SCANCODE_UP);
        bind(ActionId::MoveDown,  SDL_SCANCODE_S, SDL_SCANCODE_DOWN);
        bind(ActionId::MoveLeft,  SDL_SCANCODE_A, SDL_SCANCODE_LEFT);
        bind(ActionId::MoveRight, SDL_SCANCODE_D, SDL_SCANCODE_RIGHT);

        // Combat
        bind(ActionId::Attack, SDL_SCANCODE_SPACE);

        // Skills
        bind(ActionId::SkillSlot1, SDL_SCANCODE_1);
        bind(ActionId::SkillSlot2, SDL_SCANCODE_2);
        bind(ActionId::SkillSlot3, SDL_SCANCODE_3);
        bind(ActionId::SkillSlot4, SDL_SCANCODE_4);
        bind(ActionId::SkillSlot5, SDL_SCANCODE_5);
        bind(ActionId::SkillPagePrev, SDL_SCANCODE_LEFTBRACKET);
        bind(ActionId::SkillPageNext, SDL_SCANCODE_RIGHTBRACKET);

        // Interaction / targeting
        bind(ActionId::Interact,      SDL_SCANCODE_E);
        bind(ActionId::TargetNearest, SDL_SCANCODE_TAB);

        // UI toggles
        bind(ActionId::ToggleInventory, SDL_SCANCODE_I);
        bind(ActionId::ToggleSkillBar,  SDL_SCANCODE_K);
        bind(ActionId::ToggleQuestLog,  SDL_SCANCODE_L);

        // Chat / confirm / cancel
        bind(ActionId::OpenChat,   SDL_SCANCODE_RETURN);
        bind(ActionId::SubmitChat, SDL_SCANCODE_RETURN);
        bind(ActionId::Confirm,    SDL_SCANCODE_RETURN);
        bind(ActionId::Cancel,     SDL_SCANCODE_ESCAPE);
        bind(ActionId::Pause,      SDL_SCANCODE_ESCAPE);
    }

    std::array<ActionBinding, ACTION_COUNT> bindings_{};
    std::array<bool, ACTION_COUNT> pressed_{};
    std::array<bool, ACTION_COUNT> held_{};
    std::array<bool, ACTION_COUNT> released_{};
    InputContext context_ = InputContext::Gameplay;
};

} // namespace fate
