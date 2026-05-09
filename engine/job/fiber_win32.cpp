/**************************************************************************/
/*  fiber_win32.cpp                                                       */
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
#include "engine/job/fiber.h"

#ifdef _WIN32
#include <Windows.h>

// Fiber switch functions must never be optimized — the compiler cannot know
// that SwitchToFiber changes the entire register set and stack pointer.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif

namespace fate {
namespace fiber {

FiberHandle convertThreadToFiber() {
    return ::ConvertThreadToFiber(nullptr);
}

void convertFiberToThread() {
    ::ConvertFiberToThread();
}

FiberHandle create(size_t stackSize, FiberProc proc, void* param) {
    return ::CreateFiber(stackSize, reinterpret_cast<LPFIBER_START_ROUTINE>(proc), param);
}

void destroy(FiberHandle f) {
    if (f) ::DeleteFiber(f);
}

__declspec(noinline) void switchTo(FiberHandle f) {
    ::SwitchToFiber(f);
}

FiberHandle current() {
    return ::GetCurrentFiber();
}

} // namespace fiber
} // namespace fate

#ifdef _MSC_VER
#pragma optimize("", on)
#endif

#endif // _WIN32
