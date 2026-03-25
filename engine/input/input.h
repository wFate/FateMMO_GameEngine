#pragma once
#include "engine/core/types.h"
#include "engine/input/action_map.h"
#include "engine/input/input_buffer.h"
#include <SDL.h>
#include <unordered_map>

namespace fate {

// Keyboard key state
enum class KeyState { Up, Down, Pressed, Released };

class Input {
public:
    static Input& instance() {
        static Input s_instance;
        return s_instance;
    }

    // Call once per frame before SDL event polling
    void beginFrame();

    // Process an SDL event
    void processEvent(const SDL_Event& event);

    // Keyboard
    bool isKeyDown(SDL_Scancode key) const;
    bool isKeyPressed(SDL_Scancode key) const;   // just pressed this frame
    bool isKeyReleased(SDL_Scancode key) const;  // just released this frame

    // Mouse
    Vec2 mousePosition() const { return mousePos_; }
    Vec2 mouseDelta() const { return mouseDelta_; }
    bool isMouseDown(int button) const;
    bool isMousePressed(int button) const;
    bool isMouseReleased(int button) const;

    // Touch (for mobile, index 0 = primary finger)
    bool isTouchDown(int finger = 0) const;
    bool isTouchPressed(int finger = 0) const;
    Vec2 touchPosition(int finger = 0) const;

    // Action-based API (game systems use these)
    bool isActionPressed(ActionId id) const { return actionMap_.isPressed(id); }
    bool isActionHeld(ActionId id) const { return actionMap_.isHeld(id); }
    bool isActionReleased(ActionId id) const { return actionMap_.isReleased(id); }
    bool consumeBuffered(ActionId id, int window = 6) { return inputBuffer_.consume(id, window); }

    // Programmatic action injection (touch buttons, gamepad) — records in both
    // the action map (for isPressed/isHeld) and input buffer (for consumeBuffered)
    void injectAction(ActionId id) {
        actionMap_.setActionPressed(id);
        inputBuffer_.record(id);
    }

    // Context switching for chat
    void setChatMode(bool enabled);
    bool isChatMode() const { return actionMap_.context() == InputContext::Chat; }

    // UI panel blocking (set by GameApp each frame — blocks movement & nameplates)
    bool isUIBlocking() const { return uiBlocking_; }
    void setUIBlocking(bool v) { uiBlocking_ = v; }

    // Consume press state so downstream code sees "held" instead of "just pressed"
    void consumeMousePress(int button) {
        auto it = mouseButtons_.find(button);
        if (it != mouseButtons_.end() && it->second == KeyState::Pressed)
            it->second = KeyState::Down;
    }
    void consumeKeyPress(SDL_Scancode key) {
        auto it = keys_.find(key);
        if (it != keys_.end() && it->second == KeyState::Pressed)
            it->second = KeyState::Down;
    }

    // Access for advanced use
    ActionMap& actionMap() { return actionMap_; }
    const ActionMap& actionMap() const { return actionMap_; }

    // TWOM-style cardinal direction from WASD/arrows
    Direction getCardinalDirection() const;

    // Window dimensions (updated on resize)
    int windowWidth() const { return windowWidth_; }
    int windowHeight() const { return windowHeight_; }

private:
    Input() = default;

    std::unordered_map<SDL_Scancode, KeyState> keys_;
    std::unordered_map<int, KeyState> mouseButtons_;

    Vec2 mousePos_;
    Vec2 mouseDelta_;
    Vec2 prevMousePos_;

    struct TouchInfo {
        Vec2 position;
        KeyState state = KeyState::Up;
    };
    std::unordered_map<int, TouchInfo> touches_;

    ActionMap actionMap_;
    InputBuffer inputBuffer_;
    bool uiBlocking_ = false;

    int windowWidth_ = 1280;
    int windowHeight_ = 720;
};

} // namespace fate
