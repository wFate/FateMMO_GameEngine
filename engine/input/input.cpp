#include "engine/input/input.h"

namespace fate {

void Input::beginFrame() {
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
            }
            break;

        case SDL_KEYUP:
            keys_[event.key.keysym.scancode] = KeyState::Released;
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
    bool up    = isKeyDown(SDL_SCANCODE_W) || isKeyDown(SDL_SCANCODE_UP);
    bool down  = isKeyDown(SDL_SCANCODE_S) || isKeyDown(SDL_SCANCODE_DOWN);
    bool left  = isKeyDown(SDL_SCANCODE_A) || isKeyDown(SDL_SCANCODE_LEFT);
    bool right = isKeyDown(SDL_SCANCODE_D) || isKeyDown(SDL_SCANCODE_RIGHT);

    // TWOM-style: only one direction at a time, vertical takes priority
    if (up && !down) return Direction::Up;
    if (down && !up) return Direction::Down;
    if (left && !right) return Direction::Left;
    if (right && !left) return Direction::Right;

    return Direction::None;
}

} // namespace fate
