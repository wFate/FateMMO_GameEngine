#pragma once
#include "engine/ecs/reflect.h"
#include <functional>
#include <span>

namespace fate {

// Auto-generate an ImGui inspector from PropertyInfo metadata.
// Groups fields by category, sorts by order, emits appropriate controls.
// undoCapture is called after each ImGui widget (for undo snapshot integration).
void drawPropertyInspector(void* instance,
                           std::span<const PropertyInfo> properties,
                           const std::function<void()>& undoCapture);

} // namespace fate
