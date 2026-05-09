/**************************************************************************/
/*  component.h                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                          FateMMO Game Engine                           */
/*                       https://www.FateMMO.com                          */
/**************************************************************************/
/* Copyright (c) 2026-present FateMMO Game Engine contributors.           */
/* Copyright (c) 2026-present Caleb Kious.                                */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/
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
