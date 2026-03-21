#ifndef _WIN32

#define MINICORO_IMPL
#include "engine/job/minicoro.h"
#include "engine/job/fiber.h"

namespace fate {
namespace fiber {

static thread_local void* tls_mainFiber = nullptr;

FiberHandle convertThreadToFiber() {
    // Main thread doesn't need a real coroutine — use a sentinel value
    tls_mainFiber = reinterpret_cast<void*>(0x1);
    return tls_mainFiber;
}

void convertFiberToThread() {
    tls_mainFiber = nullptr;
}

struct FiberData {
    FiberProc proc;
    void* param;
};

static void fiberEntry(mco_coro* co) {
    auto* data = static_cast<FiberData*>(mco_get_user_data(co));
    if (data && data->proc) {
        data->proc(data->param);
    }
}

FiberHandle create(size_t stackSize, FiberProc proc, void* param) {
    auto* data = new FiberData{proc, param};
    mco_desc desc = mco_desc_init(fiberEntry, stackSize);
    desc.user_data = data;
    mco_coro* co = nullptr;
    mco_result res = mco_create(&co, &desc);
    if (res != MCO_SUCCESS || !co) {
        delete data;
        return nullptr;
    }
    return static_cast<FiberHandle>(co);
}

void destroy(FiberHandle f) {
    if (f && f != tls_mainFiber) {
        auto* co = static_cast<mco_coro*>(f);
        auto* data = static_cast<FiberData*>(mco_get_user_data(co));
        if (data) {
            delete data;
        }
        mco_destroy(co);
    }
}

void switchTo(FiberHandle f) {
    if (f == tls_mainFiber) {
        // Switching back to main thread = yield from current coroutine
        mco_yield(mco_running());
    } else {
        // Switching to a fiber = resume that coroutine
        mco_resume(static_cast<mco_coro*>(f));
    }
}

FiberHandle current() {
    mco_coro* running = mco_running();
    return running ? static_cast<FiberHandle>(running) : tls_mainFiber;
}

} // namespace fiber
} // namespace fate

#endif // !_WIN32
