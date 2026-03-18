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
