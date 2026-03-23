#pragma once
#include "engine/ui/ui_node.h"
#include <string>
#include <vector>
#include <cstdint>

namespace fate {

enum class ToastType : uint8_t { Info, Success, Warning, Error };

struct ToastEntry {
    std::string text;
    ToastType type = ToastType::Info;
    float lifetime = 3.0f;    // total display time
    float elapsed = 0.0f;     // time since shown
    float alpha = 0.0f;       // current opacity (for fade in/out)
};

class NotificationToast : public UINode {
public:
    NotificationToast(const std::string& id);

    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;

    void addToast(const std::string& text, ToastType type = ToastType::Info, float lifetime = 3.0f);
    void update(float dt);

    // Public properties (configurable from JSON or code)
    float toastHeight = 28.0f;
    float toastSpacing = 4.0f;
    float fadeInTime = 0.3f;
    float fadeOutTime = 0.5f;
    int maxToasts = 5;

    // Read-only access for tests
    const std::vector<ToastEntry>& toasts() const { return toasts_; }

private:
    std::vector<ToastEntry> toasts_;

    static Color accentColor(ToastType type);
};

} // namespace fate
