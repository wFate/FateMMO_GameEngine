/**************************************************************************/
/*  fiber.h                                                               */
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
#include <cstddef>

namespace fate {

using FiberHandle = void*;

// Platform-specific fiber entry point calling convention
#ifdef _WIN32
using FiberProc = void (__stdcall *)(void*);
#else
using FiberProc = void (*)(void*);
#endif

namespace fiber {
    FiberHandle convertThreadToFiber();
    void convertFiberToThread();
    FiberHandle create(size_t stackSize, FiberProc proc, void* param);
    void destroy(FiberHandle fiber);
    void switchTo(FiberHandle fiber);
    FiberHandle current();
}

} // namespace fate
