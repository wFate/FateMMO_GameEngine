#include "engine/job/fiber.h"

#ifdef _WIN32
#include <Windows.h>

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

void switchTo(FiberHandle f) {
    ::SwitchToFiber(f);
}

FiberHandle current() {
    return ::GetCurrentFiber();
}

} // namespace fiber
} // namespace fate

#endif // _WIN32
