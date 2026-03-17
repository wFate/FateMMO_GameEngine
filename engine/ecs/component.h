#pragma once
#include "engine/ecs/component_registry.h"
#include <cstdint>
#include <string>
#include <typeinfo>
#include <typeindex>

namespace fate {

// Legacy component type ID — still used by entity.h during migration
using ComponentTypeId = std::type_index;

template<typename T>
ComponentTypeId getComponentTypeId() {
    return std::type_index(typeid(T));
}

// Legacy base component — kept during migration
struct Component {
    virtual ~Component() = default;
    virtual const char* typeName() const = 0;
    virtual ComponentTypeId typeId() const = 0;
    bool enabled = true;
};

// Legacy macro — game code uses this during migration period
#define FATE_LEGACY_COMPONENT(ClassName) \
    const char* typeName() const override { return #ClassName; } \
    ComponentTypeId typeId() const override { return fate::getComponentTypeId<ClassName>(); }

} // namespace fate
