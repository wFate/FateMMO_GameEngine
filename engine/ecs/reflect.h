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

struct FieldInfo {
    const char* name;
    size_t offset;
    size_t size;
    FieldType type;
};

// Default: no reflection (empty field list)
template<typename T>
struct Reflection {
    static std::span<const FieldInfo> fields() { return {}; }
};

} // namespace fate

// Suppress MSVC warning for offsetof on non-standard-layout types
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4200)
#endif

#define FATE_FIELD(fieldName, fieldType) \
    fate::FieldInfo{ #fieldName, offsetof(_ReflType, fieldName), sizeof(decltype(std::declval<_ReflType>().fieldName)), fate::FieldType::fieldType }

#define FATE_REFLECT(Type, ...) \
    template<> struct fate::Reflection<Type> { \
        using _ReflType = Type; \
        static std::span<const fate::FieldInfo> fields() { \
            static const fate::FieldInfo _fields[] = { \
                __VA_ARGS__ \
            }; \
            return std::span<const fate::FieldInfo>(_fields); \
        } \
    };

// For components with NO reflectable fields (markers, etc.)
#define FATE_REFLECT_EMPTY(Type) \
    template<> struct fate::Reflection<Type> { \
        static std::span<const fate::FieldInfo> fields() { return {}; } \
    };

#ifdef _MSC_VER
#pragma warning(pop)
#endif
