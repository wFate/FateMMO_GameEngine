#pragma once
#include <cstdint>
#include <functional>

namespace fate {

// ==========================================================================
// EntityHandle — packed generational handle [index:20 | generation:12]
//
// Provides O(1) entity lookup and stale-reference detection.
// Max 1,048,575 concurrent entities, 4,096 generations before wrap.
// ==========================================================================
struct EntityHandle {
    uint32_t value = 0;

    static constexpr uint32_t INDEX_BITS = 20;
    static constexpr uint32_t GEN_BITS = 12;
    static constexpr uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1;
    static constexpr uint32_t GEN_MASK = (1u << GEN_BITS) - 1;
    static constexpr uint32_t MAX_INDEX = INDEX_MASK;
    static constexpr uint32_t MAX_GEN = GEN_MASK;

    constexpr EntityHandle() = default;
    constexpr explicit EntityHandle(uint32_t raw) : value(raw) {}
    constexpr EntityHandle(uint32_t index, uint32_t generation)
        : value((index & INDEX_MASK) | ((generation & GEN_MASK) << INDEX_BITS)) {}

    constexpr uint32_t index() const { return value & INDEX_MASK; }
    constexpr uint32_t generation() const { return (value >> INDEX_BITS) & GEN_MASK; }

    constexpr bool isNull() const { return value == 0; }
    constexpr explicit operator bool() const { return value != 0; }

    constexpr bool operator==(const EntityHandle& other) const { return value == other.value; }
    constexpr bool operator!=(const EntityHandle& other) const { return value != other.value; }
    constexpr bool operator<(const EntityHandle& other) const { return value < other.value; }

    struct Hash {
        size_t operator()(const EntityHandle& h) const {
            return std::hash<uint32_t>{}(h.value);
        }
    };
};

static constexpr EntityHandle NULL_ENTITY_HANDLE{};

} // namespace fate
