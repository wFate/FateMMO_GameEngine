/**************************************************************************/
/*  asset_handle.h                                                        */
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
#include <functional>

namespace fate {

struct AssetHandle {
    uint32_t bits = 0;

    uint32_t index() const { return bits & 0xFFFFF; }
    uint32_t generation() const { return bits >> 20; }
    bool valid() const { return bits != 0; }

    static AssetHandle make(uint32_t index, uint32_t gen) {
        return { (gen << 20) | (index & 0xFFFFF) };
    }

    bool operator==(AssetHandle o) const { return bits == o.bits; }
    bool operator!=(AssetHandle o) const { return bits != o.bits; }
};

} // namespace fate

template<>
struct std::hash<fate::AssetHandle> {
    size_t operator()(fate::AssetHandle h) const noexcept {
        return std::hash<uint32_t>{}(h.bits);
    }
};
