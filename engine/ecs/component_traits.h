#pragma once
#include <cstdint>
#include <type_traits>

namespace fate {

enum class ComponentFlags : uint32_t {
    None         = 0,
    Serializable = 1 << 0,
    Networked    = 1 << 1,
    EditorOnly   = 1 << 2,
    Persistent   = 1 << 3,
};

constexpr ComponentFlags operator|(ComponentFlags a, ComponentFlags b) {
    return static_cast<ComponentFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr ComponentFlags operator&(ComponentFlags a, ComponentFlags b) {
    return static_cast<ComponentFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
constexpr bool hasFlag(ComponentFlags flags, ComponentFlags test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

// Default: all components are Serializable
template<typename T>
struct component_traits {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

} // namespace fate
