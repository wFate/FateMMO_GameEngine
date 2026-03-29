#include "engine/ui/ui_safe_area.h"

namespace fate {

static SafeAreaInsets s_simulatedInsets;

void setSimulatedSafeArea(const SafeAreaInsets& insets) {
    s_simulatedInsets = insets;
}

SafeAreaInsets getPlatformSafeArea() {
#ifdef EDITOR_BUILD
    return s_simulatedInsets;
#else
    // TODO: query real platform insets (SDL_GetDisplayUsableBounds, iOS safeAreaInsets)
    return {};
#endif
}

} // namespace fate
