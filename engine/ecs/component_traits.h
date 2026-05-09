/**************************************************************************/
/*  component_traits.h                                                    */
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
