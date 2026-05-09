/**************************************************************************/
/*  component_registry.h                                                  */
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
