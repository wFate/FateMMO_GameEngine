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
        .name = #fieldName, \
        .offset = offsetof(_ReflType, fieldName), \
        .size = sizeof(decltype(std::declval<_ReflType>().fieldName)), \
        .type = fate::FieldType::fieldType, \
        __VA_ARGS__ \
    }

#define FATE_REFLECT(Type, ...) \
    template<> struct fate::Reflection<Type> { \
        using _ReflType = Type; \
        static std::span<const fate::PropertyInfo> fields() { \
            static const fate::PropertyInfo _fields[] = { \
                __VA_ARGS__ \
            }; \
            return {_fields, sizeof(_fields) / sizeof(_fields[0])}; \
        } \
    };

#define FATE_REFLECT_EMPTY(Type) \
    template<> struct fate::Reflection<Type> { \
        static std::span<const fate::PropertyInfo> fields() { return {}; } \
    };

#ifdef _MSC_VER
#pragma warning(pop)
#endif
