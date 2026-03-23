#include "engine/ui/widgets/notification_toast.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <algorithm>

namespace fate {

NotificationToast::NotificationToast(const std::string& id) : UINode(id, "notification_toast") {}

void NotificationToast::addToast(const std::string& text, ToastType type, float lifetime) {
    ToastEntry entry;
    entry.text = text;
    entry.type = type;
    entry.lifetime = lifetime;
    entry.elapsed = 0.0f;
    entry.alpha = 0.0f;

    // Insert at front (newest first)
    toasts_.insert(toasts_.begin(), entry);

    // Trim to max
    while (static_cast<int>(toasts_.size()) > maxToasts) {
        toasts_.pop_back();
    }
}

void NotificationToast::update(float dt) {
    for (auto& toast : toasts_) {
        toast.elapsed += dt;

        // Compute alpha: fade in, hold, fade out
        if (toast.elapsed < fadeInTime) {
            // Fade in
            toast.alpha = (fadeInTime > 0.0f) ? (toast.elapsed / fadeInTime) : 1.0f;
        } else if (toast.elapsed < toast.lifetime - fadeOutTime) {
            // Full
            toast.alpha = 1.0f;
        } else if (toast.elapsed < toast.lifetime) {
            // Fade out
            float remaining = toast.lifetime - toast.elapsed;
            toast.alpha = (fadeOutTime > 0.0f) ? (remaining / fadeOutTime) : 0.0f;
        } else {
            toast.alpha = 0.0f;
        }

        // Clamp
        if (toast.alpha < 0.0f) toast.alpha = 0.0f;
        if (toast.alpha > 1.0f) toast.alpha = 1.0f;
    }

    // Remove expired toasts
    toasts_.erase(
        std::remove_if(toasts_.begin(), toasts_.end(),
            [](const ToastEntry& t) { return t.elapsed >= t.lifetime; }),
        toasts_.end());
}

Color NotificationToast::accentColor(ToastType type) {
    switch (type) {
        case ToastType::Info:    return Color(0.3f, 0.5f, 0.9f, 1.0f);
        case ToastType::Success: return Color(0.3f, 0.8f, 0.3f, 1.0f);
        case ToastType::Warning: return Color(0.9f, 0.8f, 0.3f, 1.0f);
        case ToastType::Error:   return Color(0.9f, 0.3f, 0.3f, 1.0f);
        default:                 return Color(0.3f, 0.5f, 0.9f, 1.0f);
    }
}

bool NotificationToast::onPress(const Vec2&) {
    // Toasts are non-interactive -- let clicks pass through
    return false;
}

void NotificationToast::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    float accentWidth = 4.0f;
    float textLeftMargin = accentWidth + 8.0f;
    float textFontSize = 12.0f;

    float currentY = rect.y;

    for (const auto& toast : toasts_) {
        if (toast.alpha <= 0.0f) continue;

        float a = toast.alpha * style.opacity;

        // Toast background
        Color bg(0.08f, 0.08f, 0.10f, a * 0.9f);
        batch.drawRect({rect.x + rect.w * 0.5f, currentY + toastHeight * 0.5f},
                       {rect.w, toastHeight}, bg, d);

        // Left accent strip
        Color accent = accentColor(toast.type);
        accent.a = a;
        batch.drawRect({rect.x + accentWidth * 0.5f, currentY + toastHeight * 0.5f},
                       {accentWidth, toastHeight}, accent, d + 0.05f);

        // Text
        if (!toast.text.empty()) {
            Color tc = Color::white();
            tc.a = a;
            Vec2 ts = sdf.measure(toast.text, textFontSize);
            float textY = currentY + (toastHeight - ts.y) * 0.5f;
            sdf.drawScreen(batch, toast.text, {rect.x + textLeftMargin, textY},
                           textFontSize, tc, d + 0.1f);
        }

        currentY += toastHeight + toastSpacing;
    }

    renderChildren(batch, sdf);
}

} // namespace fate
