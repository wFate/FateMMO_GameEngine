#pragma once
#include <cstdint>
#include <string>
#include <typeinfo>
#include <typeindex>

namespace fate {

// Component type ID - each component class gets a unique ID at compile time
using ComponentTypeId = std::type_index;

template<typename T>
ComponentTypeId getComponentTypeId() {
    return std::type_index(typeid(T));
}

// Base component - all game components inherit from this
// Components are pure data containers. Logic lives in Systems.
struct Component {
    virtual ~Component() = default;
    virtual const char* typeName() const = 0;
    virtual ComponentTypeId typeId() const = 0;

    bool enabled = true;
};

// Macro to reduce boilerplate when defining components
#define FATE_COMPONENT(ClassName) \
    const char* typeName() const override { return #ClassName; } \
    ComponentTypeId typeId() const override { return fate::getComponentTypeId<ClassName>(); }

} // namespace fate
