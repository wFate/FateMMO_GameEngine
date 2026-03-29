# Unified Reflection & Responsive Layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the engine's reflection system with rich property metadata, wire it into the UI inspector/serializer/deserializer dispatch, add responsive layout primitives (min/max size, safe area, aspect ratio cap), and revamp the device simulation system with 25+ devices and safe area visualization.

**Architecture:** Replace `FieldInfo` with `PropertyInfo` in `reflect.h` (adding editor metadata fields), add virtual `reflectedProperties()` to `UINode`, write a generic `drawPropertyInspector()` that auto-generates ImGui from metadata. Add `minSize`/`maxSize`/`useSafeArea`/`maxAspectRatio` to `UIAnchor`. Replace `DisplayPreset` with `DeviceProfile` carrying safe area insets. No widgets are migrated — the legacy `dynamic_cast` chains remain as fallback.

**Tech Stack:** C++23, doctest, nlohmann/json, ImGui (editor only), SDL2

**Spec:** `Docs/superpowers/specs/2026-03-28-unified-reflection-responsive-layout-design.md`

---

## File Map

| Action | File | Responsibility |
|--------|------|---------------|
| Modify | `engine/ecs/reflect.h` | Replace `FieldInfo` with `PropertyInfo`, add `EditorControl` enum, update macros |
| Modify | `engine/ui/ui_node.h` | Add `reflectedProperties()`, `serializeProperties()`, `deserializeProperties()` virtuals |
| Create | `engine/editor/property_inspector.h` | `drawPropertyInspector()` declaration |
| Create | `engine/editor/property_inspector.cpp` | Generic auto-inspector implementation |
| Modify | `engine/editor/ui_editor_panel.cpp` | Add reflection dispatch before legacy chain |
| Modify | `engine/ui/ui_serializer.cpp` | Add reflection dispatch before legacy chain |
| Modify | `engine/ui/ui_manager.cpp` | Add reflection dispatch in `parseNode`, safe area + aspect ratio in `computeLayout` |
| Modify | `engine/ui/ui_anchor.h` | Add `minSize`, `maxSize`, `useSafeArea`, `maxAspectRatio` |
| Modify | `engine/ui/ui_node.cpp` | Add min/max clamping in `computeLayout()` |
| Create | `engine/ui/ui_safe_area.h` | `SafeAreaInsets` struct, `getPlatformSafeArea()` declaration |
| Create | `engine/ui/ui_safe_area.cpp` | Platform-specific safe area queries + editor simulation |
| Modify | `engine/editor/editor.h` | Replace `DisplayPreset`/`kDisplayPresets` with `DeviceProfile`/`kDeviceProfiles` |
| Modify | `engine/editor/editor.cpp` | Categorized device dropdown, safe area overlay, default iPhone 17 Pro |
| Create | `tests/test_property_info.cpp` | Tests for PropertyInfo round-trip serialization |
| Create | `tests/test_responsive_layout.cpp` | Tests for min/max clamping, safe area, aspect ratio cap |

---

### Task 1: Replace FieldInfo with PropertyInfo

**Files:**
- Modify: `engine/ecs/reflect.h`

- [ ] **Step 1: Replace the FieldInfo struct and macros**

Replace the entire contents of `engine/ecs/reflect.h` with:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace fate {

enum class FieldType : uint8_t {
    Float, Int, UInt, Bool,
    Vec2, Vec3, Vec4, Color, Rect,
    String,
    Enum,
    EntityHandle,
    Direction,
    Custom
};

enum class EditorControl : uint8_t {
    Auto,           // infer from FieldType
    Slider,         // DragFloat/DragInt with min/max
    ColorPicker,    // ColorEdit4
    Checkbox,       // bool toggle
    Dropdown,       // enum combo box
    TextInput,      // InputText
    TextMultiline,  // InputTextMultiline
    ReadOnly,       // display-only
    Hidden          // not shown in inspector
};

struct PropertyInfo {
    // --- Data layout ---
    const char*   name;
    size_t        offset;
    size_t        size;
    FieldType     type;

    // --- Editor metadata ---
    const char*   displayName = nullptr;  // pretty name (null = use name)
    const char*   category    = nullptr;  // group header
    const char*   tooltip     = nullptr;
    int16_t       order       = 0;        // display order within category
    EditorControl control     = EditorControl::Auto;
    float         min         = 0.0f;
    float         max         = 0.0f;     // 0,0 = no range
    float         step        = 0.0f;     // drag speed (0 = default)

    // --- Enum support ---
    const char* const* enumNames = nullptr;
    int                enumCount = 0;
};

// Backward compatibility
using FieldInfo = PropertyInfo;

// Default: no reflection (empty field list)
template<typename T>
struct Reflection {
    static std::span<const PropertyInfo> fields() { return {}; }
};

} // namespace fate

// Suppress MSVC warning for offsetof on non-standard-layout types
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4200)
#endif

#define FATE_FIELD(fieldName, fieldType) \
    fate::PropertyInfo{ #fieldName, offsetof(_ReflType, fieldName), sizeof(decltype(std::declval<_ReflType>().fieldName)), fate::FieldType::fieldType }

#define FATE_PROPERTY(fieldName, fieldType, ...) \
    fate::PropertyInfo{ \
        #fieldName, \
        offsetof(_ReflType, fieldName), \
        sizeof(decltype(std::declval<_ReflType>().fieldName)), \
        fate::FieldType::fieldType, \
        __VA_ARGS__ \
    }

#define FATE_REFLECT(Type, ...) \
    template<> struct fate::Reflection<Type> { \
        using _ReflType = Type; \
        static std::span<const fate::PropertyInfo> fields() { \
            static const fate::PropertyInfo _fields[] = { \
                __VA_ARGS__ \
            }; \
            return std::span<const fate::PropertyInfo>(_fields); \
        } \
    };

#define FATE_REFLECT_EMPTY(Type) \
    template<> struct fate::Reflection<Type> { \
        static std::span<const fate::PropertyInfo> fields() { return {}; } \
    };

#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

- [ ] **Step 2: Build to verify nothing breaks**

```bash
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
```

Touch `engine/ecs/reflect.h` and all files that include it before building. Expected: compiles with zero errors (the `using FieldInfo = PropertyInfo;` alias ensures all existing code works).

- [ ] **Step 3: Run existing tests**

```bash
./out/build/x64-Debug/fate_tests.exe
```

Expected: all existing tests pass.

- [ ] **Step 4: Commit**

```bash
git add engine/ecs/reflect.h
git commit -m "feat: replace FieldInfo with PropertyInfo, add EditorControl enum and FATE_PROPERTY macro"
```

---

### Task 2: Write PropertyInfo round-trip tests

**Files:**
- Create: `tests/test_property_info.cpp`

- [ ] **Step 1: Write tests for autoToJson/autoFromJson with PropertyInfo**

Create `tests/test_property_info.cpp`:

```cpp
#include <doctest/doctest.h>
#include "engine/ecs/reflect.h"
#include "engine/ecs/component_meta.h"
#include "engine/core/types.h"
#include <nlohmann/json.hpp>
#include <string>

using namespace fate;

namespace {

struct TestWidget {
    float fontSize = 14.0f;
    int   maxLines = 5;
    bool  wordWrap = false;
    Color textColor = {1.0f, 0.9f, 0.7f, 1.0f};
    Vec2  padding = {4.0f, 8.0f};
    std::string label = "default";
};

} // namespace

FATE_REFLECT(TestWidget,
    FATE_PROPERTY(fontSize,  Float,  .displayName="Font Size", .category="Appearance",
                  .control=fate::EditorControl::Slider, .min=6.0f, .max=72.0f, .step=0.5f),
    FATE_PROPERTY(maxLines,  Int,    .displayName="Max Lines", .category="Layout"),
    FATE_PROPERTY(wordWrap,  Bool,   .displayName="Word Wrap", .category="Layout"),
    FATE_PROPERTY(textColor, Color,  .displayName="Text Color",.category="Colors"),
    FATE_PROPERTY(padding,   Vec2,   .displayName="Padding",   .category="Layout", .order=1),
    FATE_PROPERTY(label,     String, .displayName="Label",     .category="Content")
)

TEST_CASE("PropertyInfo: Reflection fields() returns correct count") {
    auto fields = Reflection<TestWidget>::fields();
    CHECK(fields.size() == 6);
}

TEST_CASE("PropertyInfo: metadata is preserved") {
    auto fields = Reflection<TestWidget>::fields();
    // fontSize field
    CHECK(std::string(fields[0].name) == "fontSize");
    CHECK(std::string(fields[0].displayName) == "Font Size");
    CHECK(std::string(fields[0].category) == "Appearance");
    CHECK(fields[0].control == EditorControl::Slider);
    CHECK(fields[0].min == doctest::Approx(6.0f));
    CHECK(fields[0].max == doctest::Approx(72.0f));
    CHECK(fields[0].step == doctest::Approx(0.5f));
    CHECK(fields[0].type == FieldType::Float);
}

TEST_CASE("PropertyInfo: autoToJson round-trip") {
    TestWidget w;
    w.fontSize = 24.0f;
    w.maxLines = 10;
    w.wordWrap = true;
    w.textColor = {0.5f, 0.6f, 0.7f, 1.0f};
    w.padding = {12.0f, 16.0f};
    w.label = "hello";

    auto fields = Reflection<TestWidget>::fields();

    nlohmann::json j;
    autoToJson(&w, j, fields);

    CHECK(j["fontSize"].get<float>() == doctest::Approx(24.0f));
    CHECK(j["maxLines"].get<int>() == 10);
    CHECK(j["wordWrap"].get<bool>() == true);
    CHECK(j["textColor"][0].get<float>() == doctest::Approx(0.5f));
    CHECK(j["textColor"][3].get<float>() == doctest::Approx(1.0f));
    CHECK(j["padding"][0].get<float>() == doctest::Approx(12.0f));
    CHECK(j["padding"][1].get<float>() == doctest::Approx(16.0f));
    CHECK(j["label"].get<std::string>() == "hello");

    // Round-trip: deserialize into fresh instance
    TestWidget w2;
    autoFromJson(j, &w2, fields);

    CHECK(w2.fontSize == doctest::Approx(24.0f));
    CHECK(w2.maxLines == 10);
    CHECK(w2.wordWrap == true);
    CHECK(w2.textColor.r == doctest::Approx(0.5f));
    CHECK(w2.padding.x == doctest::Approx(12.0f));
    CHECK(w2.label == "hello");
}

TEST_CASE("PropertyInfo: autoFromJson skips missing fields gracefully") {
    TestWidget w;
    w.fontSize = 99.0f;

    nlohmann::json j;
    j["maxLines"] = 42;
    // fontSize not in JSON — should keep default

    auto fields = Reflection<TestWidget>::fields();
    autoFromJson(j, &w, fields);

    CHECK(w.fontSize == doctest::Approx(99.0f));  // unchanged
    CHECK(w.maxLines == 42);                        // updated
}

TEST_CASE("PropertyInfo: FATE_FIELD still works (no metadata)") {
    // Verify the old macro produces valid PropertyInfo with null metadata
    struct SimpleComp {
        float speed = 1.0f;
    };
    // Can't use FATE_REFLECT in a function scope, so test via manual construction
    PropertyInfo pi{"speed", offsetof(SimpleComp, speed), sizeof(float), FieldType::Float};
    CHECK(pi.displayName == nullptr);
    CHECK(pi.category == nullptr);
    CHECK(pi.control == EditorControl::Auto);
    CHECK(pi.min == 0.0f);
    CHECK(pi.max == 0.0f);
}
```

- [ ] **Step 2: Add test file to CMake**

The test file glob at `CMakeLists.txt` should auto-pick up `tests/test_property_info.cpp` via the existing `file(GLOB TEST_SOURCES tests/*.cpp)` pattern. Verify this by checking the glob line.

- [ ] **Step 3: Build and run tests**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
./out/build/x64-Debug/fate_tests.exe -tc="PropertyInfo*"
```

Expected: all 5 test cases pass.

- [ ] **Step 4: Commit**

```bash
git add tests/test_property_info.cpp
git commit -m "test: add PropertyInfo metadata and round-trip serialization tests"
```

---

### Task 3: Add reflection virtuals to UINode

**Files:**
- Modify: `engine/ui/ui_node.h`

- [ ] **Step 1: Add forward declaration and virtual methods**

In `engine/ui/ui_node.h`, add a forward declaration for `PropertyInfo` after the existing forward declarations (after line 11), and add the JSON forward declaration:

```cpp
struct PropertyInfo;
```

Add three virtual methods to the public section of UINode, after the `render()` virtual (after line 90):

```cpp
    // Property reflection (widgets override to enable auto-inspector/serialization)
    virtual std::span<const PropertyInfo> reflectedProperties() const { return {}; }
    virtual void serializeProperties(nlohmann::json& j) const;
    virtual void deserializeProperties(const nlohmann::json& j);
```

Add the include for `span` if not already present (it is — from `ui_anchor.h` chain). Add the `nlohmann/json_fwd.hpp` forward declaration include at the top of the file:

```cpp
#include <nlohmann/json_fwd.hpp>
```

- [ ] **Step 2: Implement the default virtual methods in ui_node.cpp**

At the end of `engine/ui/ui_node.cpp` (before the closing `} // namespace fate`), add:

```cpp
void UINode::serializeProperties(nlohmann::json& j) const {
    auto fields = reflectedProperties();
    if (!fields.empty()) autoToJson(this, j, fields);
}

void UINode::deserializeProperties(const nlohmann::json& j) {
    auto fields = reflectedProperties();
    if (!fields.empty()) autoFromJson(j, this, fields);
}
```

Add the required includes at the top of `ui_node.cpp`:

```cpp
#include "engine/ecs/component_meta.h"
#include <nlohmann/json.hpp>
```

- [ ] **Step 3: Build**

Touch `engine/ui/ui_node.h`, `engine/ui/ui_node.cpp`, and build both targets:

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
```

Expected: compiles. All existing tests still pass.

- [ ] **Step 4: Commit**

```bash
git add engine/ui/ui_node.h engine/ui/ui_node.cpp
git commit -m "feat: add reflectedProperties/serializeProperties/deserializeProperties virtuals to UINode"
```

---

### Task 4: Implement drawPropertyInspector

**Files:**
- Create: `engine/editor/property_inspector.h`
- Create: `engine/editor/property_inspector.cpp`

- [ ] **Step 1: Create the header**

Create `engine/editor/property_inspector.h`:

```cpp
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
```

- [ ] **Step 2: Create the implementation**

Create `engine/editor/property_inspector.cpp`:

```cpp
#include "engine/editor/property_inspector.h"
#include "engine/core/types.h"
#include <imgui.h>
#include <algorithm>
#include <string>
#include <cstring>
#include <vector>

namespace fate {

void drawPropertyInspector(void* instance,
                           std::span<const PropertyInfo> properties,
                           const std::function<void()>& undoCapture) {
    if (!instance || properties.empty()) return;

    auto* base = static_cast<uint8_t*>(instance);

    // Build sorted list with original indices for stable ordering
    struct Entry {
        const PropertyInfo* prop;
        int declOrder;
    };
    std::vector<Entry> entries;
    entries.reserve(properties.size());
    for (int i = 0; i < static_cast<int>(properties.size()); ++i) {
        if (properties[i].control == EditorControl::Hidden) continue;
        entries.push_back({&properties[i], i});
    }

    // Sort by category (nullptr first as "General"), then order, then declaration order
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        const char* catA = a.prop->category ? a.prop->category : "";
        const char* catB = b.prop->category ? b.prop->category : "";
        int cmp = std::strcmp(catA, catB);
        if (cmp != 0) return cmp < 0;
        if (a.prop->order != b.prop->order) return a.prop->order < b.prop->order;
        return a.declOrder < b.declOrder;
    });

    // Render grouped by category
    const char* currentCategory = nullptr;
    bool categoryOpen = true;

    for (const auto& entry : entries) {
        const auto& prop = *entry.prop;
        void* ptr = base + prop.offset;

        // Category header transition
        const char* cat = prop.category ? prop.category : "";
        bool newCategory = (currentCategory == nullptr || std::strcmp(currentCategory, cat) != 0);
        if (newCategory) {
            // Close previous category if it was a tree node
            if (currentCategory != nullptr && currentCategory[0] != '\0' && categoryOpen) {
                ImGui::TreePop();
            }
            currentCategory = cat;
            if (cat[0] != '\0') {
                categoryOpen = ImGui::TreeNodeEx(cat, ImGuiTreeNodeFlags_DefaultOpen);
            } else {
                categoryOpen = true; // ungrouped fields always shown
            }
        }

        if (!categoryOpen) continue;

        const char* label = prop.displayName ? prop.displayName : prop.name;

        // Determine effective control
        EditorControl ctrl = prop.control;
        if (ctrl == EditorControl::Auto) {
            switch (prop.type) {
                case FieldType::Bool:  ctrl = EditorControl::Checkbox; break;
                case FieldType::Color: ctrl = EditorControl::ColorPicker; break;
                case FieldType::Enum:  ctrl = prop.enumNames ? EditorControl::Dropdown : EditorControl::ReadOnly; break;
                default:
                    ctrl = (prop.min != 0.0f || prop.max != 0.0f)
                         ? EditorControl::Slider : EditorControl::Auto;
                    break;
            }
        }

        // Push unique ID to avoid ImGui label collisions across categories
        ImGui::PushID(entry.declOrder);

        switch (prop.type) {
            case FieldType::Float: {
                float speed = prop.step > 0.0f ? prop.step : 0.1f;
                if (ctrl == EditorControl::Slider || (prop.min != 0.0f || prop.max != 0.0f)) {
                    ImGui::DragFloat(label, static_cast<float*>(ptr), speed, prop.min, prop.max);
                } else {
                    ImGui::DragFloat(label, static_cast<float*>(ptr), speed);
                }
                undoCapture();
                break;
            }
            case FieldType::Int: {
                if (prop.min != 0.0f || prop.max != 0.0f) {
                    ImGui::DragInt(label, static_cast<int*>(ptr), 1.0f,
                                   static_cast<int>(prop.min), static_cast<int>(prop.max));
                } else {
                    ImGui::DragInt(label, static_cast<int*>(ptr));
                }
                undoCapture();
                break;
            }
            case FieldType::UInt: {
                ImGui::DragScalar(label, ImGuiDataType_U32, ptr);
                undoCapture();
                break;
            }
            case FieldType::Bool: {
                ImGui::Checkbox(label, static_cast<bool*>(ptr));
                undoCapture();
                break;
            }
            case FieldType::Vec2: {
                auto* v = static_cast<Vec2*>(ptr);
                float vals[2] = {v->x, v->y};
                float speed = prop.step > 0.0f ? prop.step : 0.5f;
                if (ImGui::DragFloat2(label, vals, speed)) {
                    v->x = vals[0]; v->y = vals[1];
                }
                undoCapture();
                break;
            }
            case FieldType::Vec3: {
                auto* v = static_cast<Vec3*>(ptr);
                float vals[3] = {v->x, v->y, v->z};
                if (ImGui::DragFloat3(label, vals, 0.5f)) {
                    v->x = vals[0]; v->y = vals[1]; v->z = vals[2];
                }
                undoCapture();
                break;
            }
            case FieldType::Vec4: {
                auto* v = static_cast<Vec4*>(ptr);
                float vals[4] = {v->x, v->y, v->z, v->w};
                if (ImGui::DragFloat4(label, vals, 0.5f)) {
                    v->x = vals[0]; v->y = vals[1]; v->z = vals[2]; v->w = vals[3];
                }
                undoCapture();
                break;
            }
            case FieldType::Color: {
                ImGui::ColorEdit4(label, &static_cast<Color*>(ptr)->r);
                undoCapture();
                break;
            }
            case FieldType::Rect: {
                auto* r = static_cast<Rect*>(ptr);
                float vals[4] = {r->x, r->y, r->w, r->h};
                if (ImGui::DragFloat4(label, vals, 0.5f)) {
                    r->x = vals[0]; r->y = vals[1]; r->w = vals[2]; r->h = vals[3];
                }
                undoCapture();
                break;
            }
            case FieldType::String: {
                auto* s = static_cast<std::string*>(ptr);
                char buf[512] = {};
                strncpy(buf, s->c_str(), sizeof(buf) - 1);
                if (ctrl == EditorControl::TextMultiline) {
                    if (ImGui::InputTextMultiline(label, buf, sizeof(buf))) {
                        *s = buf;
                    }
                } else {
                    if (ImGui::InputText(label, buf, sizeof(buf))) {
                        *s = buf;
                    }
                }
                undoCapture();
                break;
            }
            case FieldType::Enum: {
                if (ctrl == EditorControl::Dropdown && prop.enumNames && prop.enumCount > 0) {
                    int32_t val = 0;
                    std::memcpy(&val, ptr, prop.size <= sizeof(val) ? prop.size : sizeof(val));
                    if (val >= 0 && val < prop.enumCount) {
                        if (ImGui::Combo(label, &val, prop.enumNames, prop.enumCount)) {
                            std::memcpy(ptr, &val, prop.size <= sizeof(val) ? prop.size : sizeof(val));
                        }
                    }
                } else {
                    int32_t val = 0;
                    std::memcpy(&val, ptr, prop.size <= sizeof(val) ? prop.size : sizeof(val));
                    ImGui::TextDisabled("%s: %d", label, val);
                }
                undoCapture();
                break;
            }
            case FieldType::EntityHandle:
            case FieldType::Direction:
            case FieldType::Custom:
            default: {
                if (ctrl == EditorControl::ReadOnly) {
                    ImGui::TextDisabled("%s: [read-only]", label);
                } else {
                    ImGui::TextDisabled("%s: [unsupported type]", label);
                }
                break;
            }
        }

        // Tooltip
        if (prop.tooltip && ImGui::IsItemHovered()) {
            ImGui::SetItemTooltip("%s", prop.tooltip);
        }

        ImGui::PopID();
    }

    // Close last category tree node
    if (currentCategory != nullptr && currentCategory[0] != '\0' && categoryOpen) {
        ImGui::TreePop();
    }
}

} // namespace fate
```

- [ ] **Step 3: Add to CMake**

Check that `engine/editor/property_inspector.cpp` is picked up by the existing source glob. If sources are globbed via `file(GLOB ...)`, it should be automatic. If sources are listed explicitly, add it to the editor sources list.

- [ ] **Step 4: Build**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

Expected: compiles with no errors.

- [ ] **Step 5: Commit**

```bash
git add engine/editor/property_inspector.h engine/editor/property_inspector.cpp
git commit -m "feat: add drawPropertyInspector — auto-generates ImGui from PropertyInfo metadata"
```

---

### Task 5: Wire reflection dispatch in inspector

**Files:**
- Modify: `engine/editor/ui_editor_panel.cpp`

- [ ] **Step 1: Add include**

Add at the top of `engine/editor/ui_editor_panel.cpp` (after line 54):

```cpp
#include "engine/editor/property_inspector.h"
```

- [ ] **Step 2: Add reflection dispatch before the legacy chain**

In `drawInspector()`, find the line where the widget-specific `dynamic_cast` chain begins (line 429, the first `if (auto* panel = dynamic_cast<Panel*>(selectedNode_))`). Insert before it:

```cpp
    // --- Reflected properties (new system) ---
    auto reflectedFields = selectedNode_->reflectedProperties();
    if (!reflectedFields.empty()) {
        drawPropertyInspector(selectedNode_, reflectedFields,
                              [&]() { checkUndoCapture(uiMgr); });
    } else {
    // --- Legacy widget-specific properties ---
```

Then indent the entire existing `dynamic_cast` chain one level and add the closing brace after the final `else { ImGui::TextDisabled(...); }` block (line 2251):

```cpp
    } // end legacy fallback
```

This ensures: if a widget returns reflected properties, the auto-inspector renders them. Otherwise, the existing hand-written inspector code runs unchanged.

- [ ] **Step 3: Build and test**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

Expected: compiles. No behavioral change yet because no widget overrides `reflectedProperties()`.

- [ ] **Step 4: Commit**

```bash
git add engine/editor/ui_editor_panel.cpp
git commit -m "feat: wire reflection dispatch in UI inspector (falls back to legacy chain)"
```

---

### Task 6: Wire reflection dispatch in serializer and deserializer

**Files:**
- Modify: `engine/ui/ui_serializer.cpp`
- Modify: `engine/ui/ui_manager.cpp`

- [ ] **Step 1: Add reflection dispatch in UISerializer::serializeNode**

In `engine/ui/ui_serializer.cpp`, find the comment `// --- Widget-specific properties ---` (line 152). Insert before the existing `if (type == "panel")` chain:

```cpp
    // --- Reflected properties (new system) ---
    auto reflectedFields = node->reflectedProperties();
    if (!reflectedFields.empty()) {
        node->serializeProperties(j);
    } else {
    // --- Legacy widget-specific serialization ---
```

Add the closing brace after the last widget's `else if` block (before the event bindings / data bindings section, around line 1303):

```cpp
    } // end legacy serialization fallback
```

- [ ] **Step 2: Add reflection dispatch in UIManager::parseNode**

In `engine/ui/ui_manager.cpp`, in the `parseNode` function, find the section after a widget is created and its type-specific fields are parsed. The pattern is that each `if (type == "xxx")` block creates the widget and sets fields, ending with `node = std::move(widget);`.

After the entire type dispatch chain (after the last `else if (type == ...)` block, around line 1880), add before the common properties section:

```cpp
    // --- Reflected properties (new system) ---
    if (node) {
        auto reflectedFields = node->reflectedProperties();
        if (!reflectedFields.empty()) {
            node->deserializeProperties(j);
        }
    }
```

This runs AFTER the legacy creation (which creates the node and may set some fields), so reflected properties override the legacy parsing. For unmigrated widgets, `reflectedProperties()` returns empty and nothing happens.

- [ ] **Step 3: Build and test**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
./out/build/x64-Debug/fate_tests.exe
```

Expected: compiles and all existing tests pass. No behavioral change — no widget returns reflected properties yet.

- [ ] **Step 4: Commit**

```bash
git add engine/ui/ui_serializer.cpp engine/ui/ui_manager.cpp
git commit -m "feat: wire reflection dispatch in UI serializer and deserializer"
```

---

### Task 7: Add responsive layout fields to UIAnchor

**Files:**
- Modify: `engine/ui/ui_anchor.h`

- [ ] **Step 1: Add new fields**

In `engine/ui/ui_anchor.h`, add after the `padding` field (line 23):

```cpp
    // Responsive layout
    Vec2  minSize;                // min width/height in reference pixels (0 = no min)
    Vec2  maxSize;                // max width/height in reference pixels (0 = no max)
    bool  useSafeArea = false;    // shrink root rect by platform insets
    float maxAspectRatio = 0.0f;  // cap content width on ultrawide (0 = off)
```

- [ ] **Step 2: Build**

Touch `engine/ui/ui_anchor.h` and rebuild:

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

Expected: compiles. `Vec2` default-initializes to `{0,0}`, `bool` to `false`, `float` to `0.0f`, so all existing layouts are unaffected.

- [ ] **Step 3: Commit**

```bash
git add engine/ui/ui_anchor.h
git commit -m "feat: add minSize, maxSize, useSafeArea, maxAspectRatio to UIAnchor"
```

---

### Task 8: Write responsive layout tests

**Files:**
- Create: `tests/test_responsive_layout.cpp`

- [ ] **Step 1: Write layout clamping and safe area tests**

Create `tests/test_responsive_layout.cpp`:

```cpp
#include <doctest/doctest.h>
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_anchor.h"

using namespace fate;

TEST_CASE("UINode: computeLayout respects minSize") {
    UINode node("test", "panel");
    UIAnchor a;
    a.preset = AnchorPreset::TopLeft;
    a.size = {50.0f, 30.0f};     // 50x30 at reference scale
    a.minSize = {100.0f, 80.0f}; // min 100x80
    node.setAnchor(a);

    // scale=1: size 50x30 clamped up to 100x80
    node.computeLayout({0, 0, 800, 600}, 1.0f);
    CHECK(node.computedRect().w == doctest::Approx(100.0f));
    CHECK(node.computedRect().h == doctest::Approx(80.0f));
}

TEST_CASE("UINode: computeLayout respects maxSize") {
    UINode node("test", "panel");
    UIAnchor a;
    a.preset = AnchorPreset::TopLeft;
    a.size = {500.0f, 400.0f};
    a.maxSize = {300.0f, 200.0f};
    node.setAnchor(a);

    node.computeLayout({0, 0, 800, 600}, 1.0f);
    CHECK(node.computedRect().w == doctest::Approx(300.0f));
    CHECK(node.computedRect().h == doctest::Approx(200.0f));
}

TEST_CASE("UINode: computeLayout no clamping when minSize/maxSize are zero") {
    UINode node("test", "panel");
    UIAnchor a;
    a.preset = AnchorPreset::TopLeft;
    a.size = {200.0f, 150.0f};
    // minSize and maxSize default to {0,0} — no clamping
    node.setAnchor(a);

    node.computeLayout({0, 0, 800, 600}, 1.0f);
    CHECK(node.computedRect().w == doctest::Approx(200.0f));
    CHECK(node.computedRect().h == doctest::Approx(150.0f));
}

TEST_CASE("UINode: computeLayout minSize scales with layout scale") {
    UINode node("test", "panel");
    UIAnchor a;
    a.preset = AnchorPreset::TopLeft;
    a.size = {50.0f, 30.0f};
    a.minSize = {100.0f, 80.0f};
    node.setAnchor(a);

    // scale=2: size 100x60 (50*2, 30*2), min 200x160 (100*2, 80*2) → clamped to 200x160
    node.computeLayout({0, 0, 1600, 1200}, 2.0f);
    CHECK(node.computedRect().w == doctest::Approx(200.0f));
    CHECK(node.computedRect().h == doctest::Approx(160.0f));
}
```

- [ ] **Step 2: Build and run — tests should FAIL**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
./out/build/x64-Debug/fate_tests.exe -tc="UINode: computeLayout respects*"
```

Expected: FAIL — `computeLayout` doesn't clamp yet.

- [ ] **Step 3: Commit test file**

```bash
git add tests/test_responsive_layout.cpp
git commit -m "test: add responsive layout tests for min/max size clamping"
```

---

### Task 9: Implement min/max clamping in computeLayout

**Files:**
- Modify: `engine/ui/ui_node.cpp`

- [ ] **Step 1: Add clamping after size resolution**

In `engine/ui/ui_node.cpp`, in the `computeLayout` method, find the line `computedRect_ = {cx, cy, w, h};` (line 138). Insert BEFORE that line, after the `switch` block ends:

```cpp
    // Responsive: clamp to min/max size (in reference pixels, scaled)
    if (anchor_.minSize.x > 0) w = (std::max)(w, anchor_.minSize.x * scale);
    if (anchor_.minSize.y > 0) h = (std::max)(h, anchor_.minSize.y * scale);
    if (anchor_.maxSize.x > 0) w = (std::min)(w, anchor_.maxSize.x * scale);
    if (anchor_.maxSize.y > 0) h = (std::min)(h, anchor_.maxSize.y * scale);
```

Note: `(std::max)` with parens to avoid Windows `max` macro conflicts.

- [ ] **Step 2: Build and run tests**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
./out/build/x64-Debug/fate_tests.exe -tc="UINode: computeLayout*"
```

Expected: all 4 responsive layout tests PASS, plus all existing layout tests still pass.

- [ ] **Step 3: Commit**

```bash
git add engine/ui/ui_node.cpp
git commit -m "feat: implement min/max size clamping in UINode::computeLayout"
```

---

### Task 10: Add safe area system

**Files:**
- Create: `engine/ui/ui_safe_area.h`
- Create: `engine/ui/ui_safe_area.cpp`
- Modify: `engine/ui/ui_manager.cpp`

- [ ] **Step 1: Create SafeAreaInsets header**

Create `engine/ui/ui_safe_area.h`:

```cpp
#pragma once

namespace fate {

struct SafeAreaInsets {
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;
};

// Returns safe area insets for the current platform/device.
// In editor: returns simulated values from the selected DeviceProfile.
// On real devices: queries the OS (SDL/iOS/Android).
// On desktop: returns zeros.
SafeAreaInsets getPlatformSafeArea();

// Editor sets simulated insets when a DeviceProfile is selected
void setSimulatedSafeArea(const SafeAreaInsets& insets);

} // namespace fate
```

- [ ] **Step 2: Create implementation**

Create `engine/ui/ui_safe_area.cpp`:

```cpp
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
    // For now return zeros on non-editor builds
    return {};
#endif
}

} // namespace fate
```

- [ ] **Step 3: Apply safe area and aspect ratio in UIManager::computeLayout**

In `engine/ui/ui_manager.cpp`, add include at top:

```cpp
#include "engine/ui/ui_safe_area.h"
```

Replace the `computeLayout` method (lines 263-274) with:

```cpp
void UIManager::computeLayout(float screenWidth, float screenHeight) {
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
    float scale = screenHeight / UI_REFERENCE_HEIGHT;
    Rect screenRect{0.0f, 0.0f, screenWidth, screenHeight};
    for (const auto& id : screenOrder_) {
        auto it = screens_.find(id);
        if (it != screens_.end() && it->second->visible()) {
            Rect rootRect = screenRect;

            // Safe area: shrink root rect by platform insets
            if (it->second->anchor().useSafeArea) {
                auto insets = getPlatformSafeArea();
                rootRect.x += insets.left;
                rootRect.y += insets.top;
                rootRect.w -= (insets.left + insets.right);
                rootRect.h -= (insets.top + insets.bottom);
            }

            // Aspect ratio cap: letterbox ultrawide displays
            float maxAR = it->second->anchor().maxAspectRatio;
            if (maxAR > 0.0f && rootRect.h > 0.0f) {
                float currentAR = rootRect.w / rootRect.h;
                if (currentAR > maxAR) {
                    float targetW = rootRect.h * maxAR;
                    float excess = rootRect.w - targetW;
                    rootRect.x += excess * 0.5f;
                    rootRect.w = targetW;
                }
            }

            it->second->computeLayout(rootRect, scale);
        }
    }
}
```

- [ ] **Step 4: Build**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
./out/build/x64-Debug/fate_tests.exe
```

Expected: compiles, all tests pass.

- [ ] **Step 5: Commit**

```bash
git add engine/ui/ui_safe_area.h engine/ui/ui_safe_area.cpp engine/ui/ui_manager.cpp
git commit -m "feat: add safe area system and aspect ratio capping in computeLayout"
```

---

### Task 11: Serialize/deserialize new UIAnchor fields

**Files:**
- Modify: `engine/ui/ui_serializer.cpp`
- Modify: `engine/ui/ui_manager.cpp`

- [ ] **Step 1: Serialize new anchor fields**

In `engine/ui/ui_serializer.cpp`, find where the anchor is serialized (look for `j["anchor"]` — the section that writes preset, offset, size, etc.). After the existing anchor fields (margin, padding), add:

```cpp
        if (a.minSize.x > 0 || a.minSize.y > 0)
            anchorJson["minSize"] = {a.minSize.x, a.minSize.y};
        if (a.maxSize.x > 0 || a.maxSize.y > 0)
            anchorJson["maxSize"] = {a.maxSize.x, a.maxSize.y};
        if (a.useSafeArea)
            anchorJson["useSafeArea"] = true;
        if (a.maxAspectRatio > 0.0f)
            anchorJson["maxAspectRatio"] = a.maxAspectRatio;
```

- [ ] **Step 2: Deserialize new anchor fields**

In `engine/ui/ui_manager.cpp`, in `parseNode`, find where the anchor is parsed (look for `j["anchor"]` or `anchorJson` — the section that reads preset, offset, size, margin, padding). After the existing fields, add:

```cpp
        if (anchorJson.contains("minSize")) {
            a.minSize.x = anchorJson["minSize"][0].get<float>();
            a.minSize.y = anchorJson["minSize"][1].get<float>();
        }
        if (anchorJson.contains("maxSize")) {
            a.maxSize.x = anchorJson["maxSize"][0].get<float>();
            a.maxSize.y = anchorJson["maxSize"][1].get<float>();
        }
        a.useSafeArea = anchorJson.value("useSafeArea", false);
        a.maxAspectRatio = anchorJson.value("maxAspectRatio", 0.0f);
```

- [ ] **Step 3: Build and test**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
./out/build/x64-Debug/fate_tests.exe
```

Expected: compiles, all tests pass. Existing JSON files load fine (new fields are optional with defaults).

- [ ] **Step 4: Commit**

```bash
git add engine/ui/ui_serializer.cpp engine/ui/ui_manager.cpp
git commit -m "feat: serialize/deserialize minSize, maxSize, useSafeArea, maxAspectRatio in anchor"
```

---

### Task 12: Replace DisplayPreset with DeviceProfile

**Files:**
- Modify: `engine/editor/editor.h`

- [ ] **Step 1: Replace the struct and device array**

In `engine/editor/editor.h`, replace the `DisplayPreset` struct and `kDisplayPresets` array (lines 43-64) with:

```cpp
// Device profile for play-testing
struct DeviceProfile {
    const char* name;
    const char* category;     // "Apple iPhone", "Apple iPad", "Android", "Desktop"
    int    width;             // native pixels
    int    height;
    float  scaleFactor;       // Retina/DPI scale
    float  safeTop;
    float  safeBottom;
    float  safeLeft;
    float  safeRight;
    bool   hasNotch;
    bool   hasDynamicIsland;
};

static constexpr DeviceProfile kDeviceProfiles[] = {
    // Apple iPhone
    {"iPhone SE (3rd gen)",  "Apple iPhone", 1334,  750, 2.0f, 20,  0, 0, 0, false, false},
    {"iPhone 14",            "Apple iPhone", 2532, 1170, 3.0f, 47, 34, 0, 0, true,  false},
    {"iPhone 14 Plus",       "Apple iPhone", 2778, 1284, 3.0f, 47, 34, 0, 0, true,  false},
    {"iPhone 14 Pro",        "Apple iPhone", 2556, 1179, 3.0f, 59, 34, 0, 0, false, true},
    {"iPhone 14 Pro Max",    "Apple iPhone", 2796, 1290, 3.0f, 59, 34, 0, 0, false, true},
    {"iPhone 15",            "Apple iPhone", 2556, 1179, 3.0f, 59, 34, 0, 0, false, true},
    {"iPhone 15 Plus",       "Apple iPhone", 2796, 1290, 3.0f, 59, 34, 0, 0, false, true},
    {"iPhone 15 Pro",        "Apple iPhone", 2556, 1179, 3.0f, 59, 34, 0, 0, false, true},
    {"iPhone 15 Pro Max",    "Apple iPhone", 2796, 1290, 3.0f, 59, 34, 0, 0, false, true},
    {"iPhone 16",            "Apple iPhone", 2556, 1179, 3.0f, 59, 34, 0, 0, false, true},
    {"iPhone 16 Plus",       "Apple iPhone", 2796, 1290, 3.0f, 59, 34, 0, 0, false, true},
    {"iPhone 16 Pro",        "Apple iPhone", 2622, 1206, 3.0f, 59, 34, 0, 0, false, true},
    {"iPhone 16 Pro Max",    "Apple iPhone", 2868, 1320, 3.0f, 59, 34, 0, 0, false, true},
    {"iPhone 17 Pro",        "Apple iPhone", 2622, 1206, 3.0f, 59, 34, 0, 0, false, true},
    // Apple iPad
    {"iPad (10th gen)",      "Apple iPad",   2360, 1640, 2.0f, 20,  0, 0, 0, false, false},
    {"iPad Air (M3)",        "Apple iPad",   2360, 1640, 2.0f, 20,  0, 0, 0, false, false},
    {"iPad Pro 11\" (M4)",   "Apple iPad",   2420, 1668, 2.0f, 20,  0, 0, 0, false, false},
    {"iPad Pro 13\" (M4)",   "Apple iPad",   2752, 2064, 2.0f, 20,  0, 0, 0, false, false},
    // Android
    {"Pixel 9",              "Android",      2424, 1080, 2.6f, 24, 24, 0, 0, false, false},
    {"Pixel 9 Pro",          "Android",      2856, 1280, 2.8f, 24, 24, 0, 0, false, false},
    {"Samsung S24",          "Android",      2340, 1080, 3.0f, 24, 24, 0, 0, false, false},
    {"Samsung S24 Ultra",    "Android",      3120, 1440, 3.0f, 24, 24, 0, 0, false, false},
    {"Samsung S25",          "Android",      2340, 1080, 3.0f, 24, 24, 0, 0, false, false},
    // Desktop
    {"720p",                 "Desktop",      1280,  720, 1.0f,  0,  0, 0, 0, false, false},
    {"1080p",                "Desktop",      1920, 1080, 1.0f,  0,  0, 0, 0, false, false},
    {"1440p",                "Desktop",      2560, 1440, 1.0f,  0,  0, 0, 0, false, false},
    {"4K",                   "Desktop",      3840, 2160, 1.0f,  0,  0, 0, 0, false, false},
    {"Ultrawide 1080p",      "Desktop",      2560, 1080, 1.0f,  0,  0, 0, 0, false, false},
    // Free Aspect
    {"Free Aspect",          "Free",            0,    0, 1.0f,  0,  0, 0, 0, false, false},
};
static constexpr int kDeviceProfileCount = sizeof(kDeviceProfiles) / sizeof(kDeviceProfiles[0]);
// Default: iPhone 17 Pro (index 13)
static constexpr int kDefaultDeviceIdx = 13;
```

- [ ] **Step 2: Update displayPresetIdx_ default**

In the Editor class member declaration (line 247), change:

```cpp
int displayPresetIdx_ = kDefaultDeviceIdx;
```

- [ ] **Step 3: Fix all references to the old names**

Search for `kDisplayPresets`, `kDisplayPresetCount`, `DisplayPreset`, and `preset.width`/`preset.height`/`preset.name` in `engine/editor/editor.cpp` and `engine/editor/editor.h`. Replace:
- `kDisplayPresets` → `kDeviceProfiles`
- `kDisplayPresetCount` → `kDeviceProfileCount`
- `DisplayPreset` → `DeviceProfile` (if used as a type reference)

The struct field names `width`/`height`/`name` are the same in both, so no changes needed for field access.

- [ ] **Step 4: Build**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

Expected: compiles.

- [ ] **Step 5: Commit**

```bash
git add engine/editor/editor.h engine/editor/editor.cpp
git commit -m "feat: replace DisplayPreset with DeviceProfile, add 25+ devices, default to iPhone 17 Pro"
```

---

### Task 13: Categorized device dropdown and safe area overlay

**Files:**
- Modify: `engine/editor/editor.cpp`

- [ ] **Step 1: Replace flat combo with categorized dropdown**

In `engine/editor/editor.cpp`, replace the device dropdown combo code (the block starting at line 811 `// Display resolution dropdown`) with:

```cpp
            // Device resolution dropdown
            {
                const auto& dev = kDeviceProfiles[displayPresetIdx_];
                char label[96];
                if (dev.width == 0)
                    snprintf(label, sizeof(label), "%s", dev.name);
                else
                    snprintf(label, sizeof(label), "%s (%dx%d)", dev.name, dev.width, dev.height);

                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::BeginCombo("##Device", label, ImGuiComboFlags_HeightLarge)) {
                    const char* lastCategory = nullptr;
                    for (int i = 0; i < kDeviceProfileCount; i++) {
                        const auto& d = kDeviceProfiles[i];
                        // Category separator
                        if (lastCategory == nullptr || std::strcmp(lastCategory, d.category) != 0) {
                            if (lastCategory != nullptr) ImGui::Separator();
                            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", d.category);
                            lastCategory = d.category;
                        }
                        char itemLabel[96];
                        if (d.width == 0)
                            snprintf(itemLabel, sizeof(itemLabel), "  %s", d.name);
                        else
                            snprintf(itemLabel, sizeof(itemLabel), "  %s  %dx%d", d.name, d.width, d.height);

                        if (ImGui::Selectable(itemLabel, i == displayPresetIdx_))
                            displayPresetIdx_ = i;
                    }
                    ImGui::EndCombo();
                }

                // Update simulated safe area when device changes
                {
                    const auto& selected = kDeviceProfiles[displayPresetIdx_];
                    fate::SafeAreaInsets insets;
                    insets.top    = selected.safeTop;
                    insets.bottom = selected.safeBottom;
                    insets.left   = selected.safeLeft;
                    insets.right  = selected.safeRight;
                    fate::setSimulatedSafeArea(insets);
                }
            }
```

Add include at top of `engine/editor/editor.cpp`:

```cpp
#include "engine/ui/ui_safe_area.h"
```

- [ ] **Step 2: Add safe area overlay in play mode**

In the scene viewport rendering code, after the FBO image is drawn to the ImGui viewport (around line 922 where the ImGui::Image call is), add the safe area visualization:

```cpp
                    // Safe area overlay (red translucent bars)
                    if (!paused_ && showSafeAreaOverlay_) {
                        const auto& selDev = kDeviceProfiles[displayPresetIdx_];
                        if (selDev.safeTop > 0 || selDev.safeBottom > 0 ||
                            selDev.safeLeft > 0 || selDev.safeRight > 0) {
                            float sf = selDev.scaleFactor;
                            float imgX = viewportPos_.x;
                            float imgY = viewportPos_.y;
                            float imgW = viewportSize_.x;
                            float imgH = viewportSize_.y;
                            // Scale insets from logical points to display pixels
                            float scaleToDisp = imgW / (float)selDev.width * sf;
                            float topH    = selDev.safeTop * scaleToDisp;
                            float bottomH = selDev.safeBottom * scaleToDisp;
                            float leftW   = selDev.safeLeft * scaleToDisp;
                            float rightW  = selDev.safeRight * scaleToDisp;
                            auto* dl = ImGui::GetWindowDrawList();
                            ImU32 col = IM_COL32(255, 40, 40, 60);
                            if (topH > 0)    dl->AddRectFilled({imgX, imgY}, {imgX + imgW, imgY + topH}, col);
                            if (bottomH > 0) dl->AddRectFilled({imgX, imgY + imgH - bottomH}, {imgX + imgW, imgY + imgH}, col);
                            if (leftW > 0)   dl->AddRectFilled({imgX, imgY}, {imgX + leftW, imgY + imgH}, col);
                            if (rightW > 0)  dl->AddRectFilled({imgX + imgW - rightW, imgY}, {imgX + imgW, imgY + imgH}, col);
                        }
                    }
```

- [ ] **Step 3: Add showSafeAreaOverlay_ member**

In `engine/editor/editor.h`, add near the `displayPresetIdx_` member:

```cpp
    bool showSafeAreaOverlay_ = true;
```

And add a toggle in the toolbar (after the device combo in editor.cpp):

```cpp
                ImGui::SameLine();
                ImGui::Checkbox("Safe Area", &showSafeAreaOverlay_);
```

- [ ] **Step 4: Build and test visually**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

Expected: compiles. Launch the editor, verify:
- Dropdown shows categorized device list with headers
- Default selection is iPhone 17 Pro
- Red overlay bars appear at viewport edges when a phone with safe area is selected

- [ ] **Step 5: Commit**

```bash
git add engine/editor/editor.h engine/editor/editor.cpp
git commit -m "feat: categorized device dropdown, safe area overlay, default iPhone 17 Pro"
```

---

### Task 14: Add new anchor fields to inspector

**Files:**
- Modify: `engine/editor/ui_editor_panel.cpp`

- [ ] **Step 1: Add min/max/safeArea/maxAspectRatio to the anchor editor**

In `engine/editor/ui_editor_panel.cpp`, find `drawAnchorEditor` (around line 2274). After the existing padding editor, add:

```cpp
    ImGui::Separator();
    ImGui::Text("Responsive");

    float minVals[2] = {a.minSize.x, a.minSize.y};
    if (ImGui::DragFloat2("Min Size", minVals, 1.0f, 0.0f, 4000.0f)) {
        a.minSize.x = minVals[0]; a.minSize.y = minVals[1];
    }
    checkUndoCapture(uiMgr);

    float maxVals[2] = {a.maxSize.x, a.maxSize.y};
    if (ImGui::DragFloat2("Max Size", maxVals, 1.0f, 0.0f, 4000.0f)) {
        a.maxSize.x = maxVals[0]; a.maxSize.y = maxVals[1];
    }
    checkUndoCapture(uiMgr);

    ImGui::Checkbox("Use Safe Area", &a.useSafeArea);
    checkUndoCapture(uiMgr);

    ImGui::DragFloat("Max Aspect Ratio", &a.maxAspectRatio, 0.01f, 0.0f, 4.0f);
    checkUndoCapture(uiMgr);
```

- [ ] **Step 2: Build**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

Expected: compiles. The new fields appear in the anchor editor when a UI node is selected.

- [ ] **Step 3: Commit**

```bash
git add engine/editor/ui_editor_panel.cpp
git commit -m "feat: add responsive layout fields to anchor editor inspector"
```
