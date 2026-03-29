# Unified Reflection System & Responsive Layout Design

## Overview

Extend the existing `FATE_REFLECT` / `FieldInfo` system into a unified engine-wide property metadata system. The same property definitions drive inspector UI generation, JSON serialization/deserialization, undo/redo tracking, and runtime property values. Additionally, add responsive layout primitives (min/max size, safe area, aspect ratio capping) and a comprehensive device simulation system to the editor.

## Current State

- `reflect.h` defines `FieldInfo` (name, offset, size, type), `FATE_FIELD`, `FATE_REFLECT` macros
- `FATE_FIELD` and `FATE_REFLECT` are unused — zero invocations in the codebase
- `FATE_REFLECT_EMPTY` has 2 usages (ParticleEmitterComponent, PointLightComponent)
- `component_meta.h/cpp` provides `autoToJson`/`autoFromJson` from `FieldInfo` spans
- `drawReflectedComponent` in `editor_inspector.cpp` auto-generates ImGui from `FieldInfo`
- ~50 UI widget types with hand-written serializer (1300 lines), deserializer (1600 lines), and inspector (1800 lines) — all centralized `dynamic_cast` chains
- `UIAnchor` handles positioning via presets + pixel/percent offset + reference-height scaling (900px)
- `DisplayPreset` array has 11 devices with name/width/height only, defaults to Free Aspect, no safe area data

---

## Part 1: Unified Reflection System

### 1.1 Core Data Structures

Replace `FieldInfo` with `PropertyInfo` — a single struct carrying data layout AND editor metadata.

```cpp
enum class EditorControl : uint8_t {
    Auto,           // infer from FieldType (default)
    Slider,         // DragFloat/DragInt with min/max
    ColorPicker,    // ColorEdit4
    Checkbox,       // bool toggle
    Dropdown,       // enum combo box
    TextInput,      // InputText
    TextMultiline,  // InputTextMultiline
    ReadOnly,       // display-only, no editing
    Hidden          // not shown in inspector at all
};

struct PropertyInfo {
    // --- Data layout (replaces old FieldInfo) ---
    const char*   name;         // field name (used as JSON key)
    size_t        offset;
    size_t        size;
    FieldType     type;

    // --- Editor metadata ---
    const char*   displayName = nullptr;  // pretty name for inspector (null = use name)
    const char*   category    = nullptr;  // group header ("Layout", "Colors", etc.)
    const char*   tooltip     = nullptr;
    int16_t       order       = 0;        // display order within category
    EditorControl control     = EditorControl::Auto;
    float         min         = 0.0f;
    float         max         = 0.0f;     // 0,0 = no range constraint
    float         step        = 0.0f;     // drag speed (0 = default)

    // --- Enum support ---
    const char* const* enumNames = nullptr;  // null-terminated string array
    int                enumCount = 0;
};

// Backward compatibility
using FieldInfo = PropertyInfo;
```

All POD, no heap allocations. Everything is const char* literals and floats stored in static const arrays.

### 1.2 Macros

```cpp
// Declare reflected properties for a type
#define FATE_REFLECT(Type, ...)                                    \
    template<> struct fate::Reflection<Type> {                     \
        using _ReflType = Type;                                    \
        static std::span<const fate::PropertyInfo> fields() {      \
            static const fate::PropertyInfo _fields[] = {          \
                __VA_ARGS__                                        \
            };                                                     \
            return std::span<const fate::PropertyInfo>(_fields);   \
        }                                                          \
    };

// Basic field (backward compat, no editor metadata)
#define FATE_FIELD(fieldName, fieldType)                            \
    fate::PropertyInfo {                                            \
        #fieldName,                                                \
        offsetof(_ReflType, fieldName),                            \
        sizeof(decltype(std::declval<_ReflType>().fieldName)),     \
        fate::FieldType::fieldType                                 \
    }

// Rich property with editor metadata via C++20 designated initializers
#define FATE_PROPERTY(fieldName, fieldType, ...)                    \
    fate::PropertyInfo {                                            \
        #fieldName,                                                \
        offsetof(_ReflType, fieldName),                            \
        sizeof(decltype(std::declval<_ReflType>().fieldName)),     \
        fate::FieldType::fieldType,                                \
        __VA_ARGS__                                                \
    }

// Marker types with no fields
#define FATE_REFLECT_EMPTY(Type) \
    template<> struct fate::Reflection<Type> { \
        static std::span<const fate::PropertyInfo> fields() { return {}; } \
    };
```

Example usage:

```cpp
// In button.h, after the class definition:
FATE_REFLECT(fate::Button,
    FATE_PROPERTY(label,        String,  .displayName="Label",     .category="Content",    .order=0),
    FATE_PROPERTY(fontSize,     Float,   .displayName="Font Size", .category="Appearance", .order=0,
                  .control=fate::EditorControl::Slider, .min=6.0f, .max=72.0f, .step=0.5f),
    FATE_PROPERTY(textColor,    Color,   .displayName="Text",      .category="Colors",     .order=0),
    FATE_PROPERTY(pressedColor, Color,   .displayName="Pressed",   .category="Colors",     .order=1),
    FATE_PROPERTY(hoverColor,   Color,   .displayName="Hover",     .category="Colors",     .order=2),
    FATE_PROPERTY(cornerRadius, Float,   .displayName="Radius",    .category="Appearance", .order=1,
                  .min=0.0f, .max=32.0f)
)
```

### 1.3 UINode Virtual Interface

Add to UINode base class:

```cpp
// ui_node.h
virtual std::span<const PropertyInfo> reflectedProperties() const { return {}; }

virtual void serializeProperties(nlohmann::json& j) const {
    auto fields = reflectedProperties();
    if (!fields.empty()) autoToJson(this, j, fields);
}

virtual void deserializeProperties(const nlohmann::json& j) {
    auto fields = reflectedProperties();
    if (!fields.empty()) autoFromJson(j, this, fields);
}
```

Migrated widgets override `reflectedProperties()`:

```cpp
// button.h
std::span<const PropertyInfo> reflectedProperties() const override {
    return Reflection<Button>::fields();
}
```

Widgets that need special serialization logic can override `serializeProperties`/`deserializeProperties`:

```cpp
void MyWidget::serializeProperties(nlohmann::json& j) const {
    UINode::serializeProperties(j);  // auto-serialize reflected fields
    j["direction"] = directionToString(direction);  // handle special case
}
```

### 1.4 Auto-Inspector

A single generic function replaces the 1800-line dynamic_cast chain:

```cpp
// engine/editor/property_inspector.h
void drawPropertyInspector(void* instance, std::span<const PropertyInfo> properties,
                           std::function<void()> undoCapture);
```

The function:

1. Groups fields by `category` string. Null category = "General" (rendered first, ungrouped).
2. Sorts within each group by `order`, with declaration order as tiebreaker.
3. Renders each group as `ImGui::TreeNodeEx` collapsible header (default open).
4. Emits ImGui controls based on FieldType + EditorControl:

| FieldType + EditorControl | ImGui Widget |
|---|---|
| Float + Auto | `DragFloat(displayName, ptr, step)` |
| Float + Slider | `DragFloat(displayName, ptr, step, min, max)` |
| Int + Auto | `DragInt(displayName, ptr)` |
| Bool + Auto | `Checkbox(displayName, ptr)` |
| Color + Auto/ColorPicker | `ColorEdit4(displayName, ptr)` |
| Vec2 + Auto | `DragFloat2(displayName, ptr)` |
| String + Auto/TextInput | `InputText(displayName, buf)` |
| Enum + Dropdown | `Combo(displayName, ptr, enumNames)` |
| Any + ReadOnly | `TextDisabled(value)` |
| Any + Hidden | skip |

5. Calls `undoCapture` callback after every ImGui widget.
6. Shows tooltip via `ImGui::SetItemTooltip` if `tooltip != nullptr`.

### 1.5 Integration with Existing Code

The legacy code coexists with the new system via a simple dispatch:

**Inspector (`UIEditorPanel::drawInspector`):**
```cpp
// After common properties (anchor, style, visibility)...
auto fields = node->reflectedProperties();
if (!fields.empty()) {
    drawPropertyInspector(node, fields, [&]() { checkUndoCapture(uiMgr); });
} else {
    drawLegacyWidgetInspector(node, uiMgr);  // existing dynamic_cast chain
}
```

**Serializer (`UISerializer::serializeNode`):**
```cpp
auto fields = node->reflectedProperties();
if (!fields.empty()) {
    node->serializeProperties(j);
} else {
    // existing dynamic_cast chain for unmigrated widgets
}
```

**Deserializer (`UIManager::parseNode`):**
```cpp
// After creating widget from "type" string...
auto fields = widget->reflectedProperties();
if (!fields.empty()) {
    widget->deserializeProperties(j);
} else {
    // existing hand-written parsing for unmigrated widgets
}
```

### 1.6 Serialization Format

No change. `autoToJson` writes `j["fontSize"] = 14.0f` and `autoFromJson` reads `j.value("fontSize", default)` — identical key/value pairs to the hand-written serializer. Existing `.json` screen files load without modification.

### 1.7 ECS Component Compatibility

`ComponentMeta::fields` is `span<const FieldInfo>`, which is now `span<const PropertyInfo>` via the alias. The `autoToJson`/`autoFromJson` functions work unchanged — they only read name/offset/size/type. Components with custom `toJson`/`fromJson` lambdas keep using them. `drawReflectedComponent` in the ECS inspector works unchanged but can optionally be upgraded to use `drawPropertyInspector` to get category grouping and richer controls.

---

## Part 2: Responsive Layout Improvements

### 2.1 UIAnchor Additions

```cpp
struct UIAnchor {
    // --- Existing fields (unchanged) ---
    AnchorPreset preset = AnchorPreset::TopLeft;
    Vec2 offset;
    Vec2 size;
    Vec2 offsetPercent;
    Vec2 sizePercent;
    Vec4 margin;
    Vec4 padding;

    // --- New: responsive layout ---
    Vec2  minSize;                // min width/height in reference pixels (0 = no min)
    Vec2  maxSize;                // max width/height in reference pixels (0 = no max)
    bool  useSafeArea = false;    // shrink root rect by platform insets
    float maxAspectRatio = 0.0f;  // cap content width on ultrawide (0 = off)
};
```

All new fields default to zero/false, so existing layouts are unaffected.

### 2.2 computeLayout Changes

After resolving `w` and `h` in `UINode::computeLayout()`, add clamping:

```cpp
if (anchor_.minSize.x > 0) w = std::max(w, anchor_.minSize.x * scale);
if (anchor_.minSize.y > 0) h = std::max(h, anchor_.minSize.y * scale);
if (anchor_.maxSize.x > 0) w = std::min(w, anchor_.maxSize.x * scale);
if (anchor_.maxSize.y > 0) h = std::min(h, anchor_.maxSize.y * scale);
```

### 2.3 Safe Area System

```cpp
// engine/ui/ui_safe_area.h
struct SafeAreaInsets {
    float top = 0, right = 0, bottom = 0, left = 0;
};
SafeAreaInsets getPlatformSafeArea();
```

- **Editor play mode:** Returns the selected DeviceProfile's safe area values (simulated)
- **Shipped builds on real devices:** Queries SDL/iOS/Android API for real insets
- **Shipped builds on desktop:** Returns zeros

Applied in `UIManager::computeLayout()` at the root level:

```cpp
Rect screenRect{0, 0, screenWidth, screenHeight};
if (root->anchor().useSafeArea) {
    auto insets = getPlatformSafeArea();
    screenRect.x += insets.left;
    screenRect.y += insets.top;
    screenRect.w -= (insets.left + insets.right);
    screenRect.h -= (insets.top + insets.bottom);
}
```

### 2.4 Aspect Ratio Capping

Applied at root level before layout:

```cpp
if (root->anchor().maxAspectRatio > 0) {
    float currentAspect = screenRect.w / screenRect.h;
    if (currentAspect > root->anchor().maxAspectRatio) {
        float targetW = screenRect.h * root->anchor().maxAspectRatio;
        float excess = screenRect.w - targetW;
        screenRect.x += excess * 0.5f;
        screenRect.w = targetW;
    }
}
```

---

## Part 3: Device Simulation Revamp

### 3.1 DeviceProfile Struct

Replace `DisplayPreset` with:

```cpp
struct DeviceProfile {
    const char* name;
    const char* category;     // "Apple iPhone", "Apple iPad", "Android", "Desktop"
    int    width;             // native pixels
    int    height;
    float  scaleFactor;       // Retina/DPI scale
    float  safeTop;           // safe area insets in logical points
    float  safeBottom;
    float  safeLeft;
    float  safeRight;
    bool   hasNotch;
    bool   hasDynamicIsland;
};
```

### 3.2 Device Database

```
Apple iPhone:
  iPhone SE (3rd gen)     1334x750    2x   safe: 20/0/0/0
  iPhone 14               2532x1170   3x   safe: 47/34/0/0    notch
  iPhone 14 Plus          2778x1284   3x   safe: 47/34/0/0    notch
  iPhone 14 Pro           2556x1179   3x   safe: 59/34/0/0    dynamic island
  iPhone 14 Pro Max       2796x1290   3x   safe: 59/34/0/0    dynamic island
  iPhone 15               2556x1179   3x   safe: 59/34/0/0    dynamic island
  iPhone 15 Plus          2796x1290   3x   safe: 59/34/0/0    dynamic island
  iPhone 15 Pro           2556x1179   3x   safe: 59/34/0/0    dynamic island
  iPhone 15 Pro Max       2796x1290   3x   safe: 59/34/0/0    dynamic island
  iPhone 16               2556x1179   3x   safe: 59/34/0/0    dynamic island
  iPhone 16 Plus          2796x1290   3x   safe: 59/34/0/0    dynamic island
  iPhone 16 Pro           2622x1206   3x   safe: 59/34/0/0    dynamic island
  iPhone 16 Pro Max       2868x1320   3x   safe: 59/34/0/0    dynamic island
  iPhone 17 Pro           2740x1264   3x   safe: 59/34/0/0    dynamic island  [DEFAULT]

Apple iPad:
  iPad (10th gen)         2360x1640   2x   safe: 20/0/0/0
  iPad Air (M3)           2360x1640   2x   safe: 20/0/0/0
  iPad Pro 11" (M4)       2420x1668   2x   safe: 20/0/0/0
  iPad Pro 13" (M4)       2752x2064   2x   safe: 20/0/0/0

Android:
  Pixel 9                 2424x1080   2.6x  safe: 24/24/0/0
  Pixel 9 Pro             2856x1280   2.8x  safe: 24/24/0/0
  Samsung S24             2340x1080   3x    safe: 24/24/0/0
  Samsung S24 Ultra       3120x1440   3x    safe: 24/24/0/0
  Samsung S25             2340x1080   3x    safe: 24/24/0/0

Desktop:
  720p                    1280x720    1x    safe: 0/0/0/0
  1080p                   1920x1080   1x    safe: 0/0/0/0
  1440p                   2560x1440   1x    safe: 0/0/0/0
  4K                      3840x2160   1x    safe: 0/0/0/0
  Ultrawide 1080p         2560x1080   1x    safe: 0/0/0/0

Free Aspect               0x0        1x    safe: 0/0/0/0
```

### 3.3 Editor Toolbar

- Flat combo replaced with categorized dropdown (category headers as separators)
- Selected device displays as `"iPhone 17 Pro (2740x1264)"` in toolbar
- Default selection: iPhone 17 Pro (not Free Aspect)

### 3.4 Safe Area Visualization

When a device with safe area insets is selected and play mode is active:
- Semi-transparent red overlay bars drawn at viewport edges showing safe area boundaries
- Toggle button in toolbar to enable/disable overlay (on by default)
- Gives immediate visual feedback when UI extends into unsafe regions

---

## Part 4: Implementation Plan

### Phase 1: Foundation (no visible changes)
1. Replace `FieldInfo` with `PropertyInfo` in `reflect.h`, add `EditorControl` enum, add `using FieldInfo = PropertyInfo;` alias
2. Add `FATE_PROPERTY` macro alongside existing `FATE_FIELD`
3. Add `reflectedProperties()` / `serializeProperties()` / `deserializeProperties()` virtuals to `UINode`
4. Write `drawPropertyInspector()` function (~150 lines)

### Phase 2: Wire the dispatch (no visible changes)
5. In `UIEditorPanel::drawInspector()`, add `reflectedProperties()` check before dynamic_cast chain
6. Same dispatch in `UISerializer::serializeNode()` and `UIManager::parseNode()`

### Phase 3: Layout improvements
7. Add `minSize`, `maxSize`, `useSafeArea`, `maxAspectRatio` to `UIAnchor`
8. Update `computeLayout()` with clamping and safe area logic
9. Add `getPlatformSafeArea()` with editor simulation path
10. Update anchor editor and serializer for new fields

### Phase 4: Device simulation revamp
11. Replace `DisplayPreset` with `DeviceProfile` and expanded device database
12. Update toolbar combo to categorized dropdown, default to iPhone 17 Pro
13. Add safe area overlay visualization in play mode

### Future: Widget migration (separate effort)
- Migrate widgets one at a time: add `FATE_REFLECT` to header, override `reflectedProperties()`, delete legacy branch
- Order: simple widgets first (Button, Label, Checkbox), then medium, then complex
- Each widget is an independent commit
- Delete legacy chains once all 50 widgets migrated

---

## Future Extensibility

This design enables without redesign:
- **Multi-select editing:** drawPropertyInspector with merged property view for conflicting values
- **Animation keyframing:** PropertyInfo reference + offset for reading/writing any reflected field
- **Scripting exposure:** Iterate reflectedProperties() to auto-generate Lua/script bindings
- **Prefab variants:** Field-by-field diff using property offsets to produce override patches
- **Copy/paste properties:** Serialize reflected fields as JSON snippet, paste onto same-type widget
