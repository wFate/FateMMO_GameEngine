#include "engine/input/input.h"
#include "imgui.h"

namespace fate {

void Input::beginFrame() {
    actionMap_.beginFrame();
    inputBuffer_.advanceFrame();

    // Transition pressed → down, released → up
    for (auto& [key, state] : keys_) {
        if (state == KeyState::Pressed) state = KeyState::Down;
        else if (state == KeyState::Released) state = KeyState::Up;
    }
    for (auto& [btn, state] : mouseButtons_) {
        if (state == KeyState::Pressed) state = KeyState::Down;
        else if (state == KeyState::Released) state = KeyState::Up;
    }
    for (auto& [id, touch] : touches_) {
        if (touch.state == KeyState::Pressed) touch.state = KeyState::Down;
        else if (touch.state == KeyState::Released) touch.state = KeyState::Up;
    }
    mouseDelta_ = {0, 0};
}

void Input::processEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_KEYDOWN:
            if (!event.key.repeat) {
                keys_[event.key.keysym.scancode] = KeyState::Pressed;
                actionMap_.onKeyDown(event.key.keysym.scancode);
                // Buffer combat actions
                SDL_Scancode sc = event.key.keysym.scancode;
                auto checkBuffer = [&](ActionId id) {
                    const auto& b = actionMap_.binding(id);
                    if (sc == b.primary || sc == b.secondary) inputBuffer_.record(id);
                };
                checkBuffer(ActionId::Attack);
                checkBuffer(ActionId::SkillSlot1);
                checkBuffer(ActionId::SkillSlot2);
                checkBuffer(ActionId::SkillSlot3);
                checkBuffer(ActionId::SkillSlot4);
                checkBuffer(ActionId::SkillSlot5);
            }
            break;

        case SDL_KEYUP:
            keys_[event.key.keysym.scancode] = KeyState::Released;
            actionMap_.onKeyUp(event.key.keysym.scancode);
            break;

        case SDL_MOUSEMOTION:
            prevMousePos_ = mousePos_;
            mousePos_ = {(float)event.motion.x, (float)event.motion.y};
            mouseDelta_ = mousePos_ - prevMousePos_;
            break;

        case SDL_MOUSEBUTTONDOWN:
            mouseButtons_[event.button.button] = KeyState::Pressed;
            break;

        case SDL_MOUSEBUTTONUP:
            mouseButtons_[event.button.button] = KeyState::Released;
            break;

        case SDL_FINGERDOWN: {
            int id = (int)event.tfinger.fingerId;
            touches_[id].state = KeyState::Pressed;
            touches_[id].position = {event.tfinger.x * windowWidth_, event.tfinger.y * windowHeight_};
            break;
        }

        case SDL_FINGERUP: {
            int id = (int)event.tfinger.fingerId;
            touches_[id].state = KeyState::Released;
            break;
        }

        case SDL_FINGERMOTION: {
            int id = (int)event.tfinger.fingerId;
            touches_[id].position = {event.tfinger.x * windowWidth_, event.tfinger.y * windowHeight_};
            break;
        }

        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                windowWidth_ = event.window.data1;
                windowHeight_ = event.window.data2;
            }
            break;
    }
}

bool Input::isKeyDown(SDL_Scancode key) const {
    auto it = keys_.find(key);
    if (it == keys_.end()) return false;
    return it->second == KeyState::Down || it->second == KeyState::Pressed;
}

bool Input::isKeyPressed(SDL_Scancode key) const {
    auto it = keys_.find(key);
    return it != keys_.end() && it->second == KeyState::Pressed;
}

bool Input::isKeyReleased(SDL_Scancode key) const {
    auto it = keys_.find(key);
    return it != keys_.end() && it->second == KeyState::Released;
}

bool Input::isMouseDown(int button) const {
    auto it = mouseButtons_.find(button);
    if (it == mouseButtons_.end()) return false;
    return it->second == KeyState::Down || it->second == KeyState::Pressed;
}

bool Input::isMousePressed(int button) const {
    auto it = mouseButtons_.find(button);
    return it != mouseButtons_.end() && it->second == KeyState::Pressed;
}

bool Input::isMouseReleased(int button) const {
    auto it = mouseButtons_.find(button);
    return it != mouseButtons_.end() && it->second == KeyState::Released;
}

bool Input::isTouchDown(int finger) const {
    auto it = touches_.find(finger);
    if (it == touches_.end()) return false;
    return it->second.state == KeyState::Down || it->second.state == KeyState::Pressed;
}

bool Input::isTouchPressed(int finger) const {
    auto it = touches_.find(finger);
    return it != touches_.end() && it->second.state == KeyState::Pressed;
}

Vec2 Input::touchPosition(int finger) const {
    auto it = touches_.find(finger);
    if (it == touches_.end()) return Vec2::zero();
    return it->second.position;
}

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

void Input::setChatMode(bool enabled) {
    if (enabled) {
        actionMap_.setContext(InputContext::Chat);
        SDL_StartTextInput();
    } else {
        actionMap_.setContext(InputContext::Gameplay);
#ifndef FATE_SHIPPING
        // Editor build: keep text input active so ImGui widgets remain editable
        SDL_StartTextInput();
#else
        SDL_StopTextInput();
#endif
    }
}

} // namespace fate
