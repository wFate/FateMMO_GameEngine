/**************************************************************************/
/*  metal_shader_lib.h                                                    */
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
#include <string>
#include <unordered_map>

#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

namespace gfx {
namespace metal {

class MetalShaderLib {
public:
    void init(void* mtlDevice);  // id<MTLDevice>
    void shutdown();

    // Compile .metal source string into the library
    bool compileSource(const std::string& source, const std::string& label = "");

    // Load pre-compiled .metallib file (FATE_SHIPPING)
    bool loadMetallib(const std::string& path);

    // VFS-aware variant: load pre-compiled .metallib bytes already read into
    // memory by IAssetSource. Equivalent to loadMetallib but without a disk read.
    bool loadMetallibFromBytes(const void* data, size_t size, const std::string& label = "");

    // Get a named function from the library
    void* getFunction(const std::string& name);  // returns id<MTLFunction>

private:
#ifdef __OBJC__
    id<MTLDevice> device_ = nil;
    id<MTLLibrary> library_ = nil;
#else
    void* device_ = nullptr;
    void* library_ = nullptr;
#endif
    std::unordered_map<std::string, void*> cache_;
};

} // namespace metal
} // namespace gfx
