#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace fate {

enum class EventState : uint8_t { Idle = 0, Signup = 1, Active = 2 };
enum class EventCallback : uint8_t { OnSignupStart = 0, OnEventStart = 1, OnEventEnd = 2, OnTick = 3 };

struct EventConfig {
    std::string eventId;
    float intervalSeconds = 7200.0f;  // time between events (Idle duration)
    float signupDuration = 300.0f;    // signup window before active
    float activeDuration = 900.0f;    // how long event runs
};

class EventScheduler {
public:
    std::string registerEvent(const EventConfig& config) {
        ScheduledEvent evt;
        evt.config = config;
        evt.state = EventState::Idle;
        evt.nextTransitionTime = config.intervalSeconds;
        events_[config.eventId] = evt;
        return config.eventId;
    }

    void setCallback(const std::string& eventId, EventCallback type, std::function<void()> cb) {
        auto it = events_.find(eventId);
        if (it == events_.end()) return;
        it->second.callbacks[static_cast<int>(type)] = std::move(cb);
    }

    void tick(float currentTime) {
        lastTickTime_ = currentTime;
        for (auto& [id, evt] : events_) {
            if (currentTime < evt.nextTransitionTime) continue;
            switch (evt.state) {
                case EventState::Idle:
                    evt.state = EventState::Signup;
                    evt.nextTransitionTime = currentTime + evt.config.signupDuration;
                    if (evt.callbacks[0]) evt.callbacks[0]();
                    break;
                case EventState::Signup:
                    evt.state = EventState::Active;
                    evt.nextTransitionTime = currentTime + evt.config.activeDuration;
                    if (evt.callbacks[1]) evt.callbacks[1]();
                    break;
                case EventState::Active:
                    evt.state = EventState::Idle;
                    evt.nextTransitionTime = currentTime + evt.config.intervalSeconds;
                    if (evt.callbacks[2]) evt.callbacks[2]();
                    break;
            }
        }
    }

    EventState getState(const std::string& eventId) const {
        auto it = events_.find(eventId);
        return it != events_.end() ? it->second.state : EventState::Idle;
    }

    float getTimeRemaining(const std::string& eventId) const {
        auto it = events_.find(eventId);
        if (it == events_.end()) return 0.0f;
        return it->second.nextTransitionTime - lastTickTime_;
    }

private:
    struct ScheduledEvent {
        EventConfig config;
        EventState state = EventState::Idle;
        float nextTransitionTime = 0.0f;
        std::function<void()> callbacks[4];
    };
    std::unordered_map<std::string, ScheduledEvent> events_;
    float lastTickTime_ = 0.0f;
};

} // namespace fate
