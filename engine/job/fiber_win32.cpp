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
