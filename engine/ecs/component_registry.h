#pragma once
#include <cstdint>
#include <cstddef>
#include <atomic>

namespace fate {

enum class ComponentTier : uint8_t {
    Hot,   // Future SoA field-split
    Warm,  // Contiguous typed array (default)
    Cold   // Rarely accessed
};

// New compile-time type ID — named CompId to avoid collision with legacy ComponentTypeId
using CompId = uint32_t;

inline CompId nextCompId() {
    static std::atomic<CompId> counter{0};
    return counter.fetch_add(1);
}

template<typename T>
CompId componentId() {
    static const CompId id = nextCompId();
    return id;
}

// New FATE_COMPONENT macros — compile-time, no virtual dispatch
#define FATE_COMPONENT(ClassName) \
    static constexpr const char* COMPONENT_NAME = #ClassName; \
    static inline const fate::CompId COMPONENT_TYPE_ID = fate::componentId<ClassName>(); \
    static constexpr fate::ComponentTier COMPONENT_TIER = fate::ComponentTier::Warm; \
    bool enabled = true;

#define FATE_COMPONENT_HOT(ClassName) \
    static constexpr const char* COMPONENT_NAME = #ClassName; \
    static inline const fate::CompId COMPONENT_TYPE_ID = fate::componentId<ClassName>(); \
    static constexpr fate::ComponentTier COMPONENT_TIER = fate::ComponentTier::Hot; \
    bool enabled = true;

#define FATE_COMPONENT_COLD(ClassName) \
    static constexpr const char* COMPONENT_NAME = #ClassName; \
    static inline const fate::CompId COMPONENT_TYPE_ID = fate::componentId<ClassName>(); \
    static constexpr fate::ComponentTier COMPONENT_TIER = fate::ComponentTier::Cold; \
    bool enabled = true;

} // namespace fate
