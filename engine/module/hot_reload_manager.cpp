#include "engine/module/hot_reload_manager.h"
#include "engine/module/hot_reload_hook_decl.h"
#include "engine/module/behavior_component.h"
#include "engine/module/behavior_ctx_internal.h"
#include "engine/components/transform.h"
#include "engine/ecs/world.h"
#include "engine/core/logger.h"
#ifndef FATE_SHIPPING
#include "engine/editor/editor.h"
#endif

#include <filesystem>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace fate {

// P4 (S153) fault info. Defined in namespace fate (NOT anonymous) so the
// forward declarations below + the helper hrCall* / SEH wrappers / fault
// helpers can all reference the type. Plain POD: no destructors, char[256]
// for cppWhat keeps it copyable and SEH-/__try-safe.
struct HrFaultInfo {
    int      caught        = 0;       // 0=ok, 1=SEH, -1=C++ exception
    uint32_t exceptionCode = 0;       // GetExceptionCode() when SEH
    void*    exceptionAddr = nullptr; // SEH record's ExceptionAddress
    char     cppWhat[256]  = {};      // truncated copy of std::exception::what()
};

// ---------------------------------------------------------------------------
// Host API implementation. Every function casts the opaque ctx back to
// FateBehaviorCtx* and dispatches against host structures. All checks are
// defensive — a misbehaving module that passes garbage must not crash the
// host process.
// ---------------------------------------------------------------------------
namespace {

void hostLog(FateLogLevel level, const char* category, const char* message) {
    const char* cat = category ? category : "GameModule";
    const char* msg = message ? message : "";
    switch (level) {
        case FATE_LOG_DEBUG: LOG_DEBUG(cat, "%s", msg); break;
        case FATE_LOG_INFO:  LOG_INFO(cat,  "%s", msg); break;
        case FATE_LOG_WARN:  LOG_WARN(cat,  "%s", msg); break;
        case FATE_LOG_ERROR: LOG_ERROR(cat, "%s", msg); break;
        default:             LOG_INFO(cat,  "%s", msg); break;
    }
}

FateModuleResult hostRegisterBehavior(const char* name, const FateBehaviorVTable* vtable) {
    return BehaviorRegistry::instance().registerBehavior(name, vtable);
}

uint64_t hostCtxEntityId(FateBehaviorCtx* ctx) {
    if (!ctx || !ctx->entity) return 0;
    return static_cast<uint64_t>(ctx->entity->id());
}

int hostCtxIsEnabled(FateBehaviorCtx* ctx) {
    if (!ctx || !ctx->component) return 0;
    return ctx->component->enabled ? 1 : 0;
}

float hostGetFloat(FateBehaviorCtx* ctx, const char* name, float def) {
    if (!ctx || !ctx->component || !name) return def;
    const auto& f = ctx->component->fields;
    auto it = f.find(name);
    if (it == f.end() || !it->is_number()) return def;
    return it->get<float>();
}

int32_t hostGetInt(FateBehaviorCtx* ctx, const char* name, int32_t def) {
    if (!ctx || !ctx->component || !name) return def;
    const auto& f = ctx->component->fields;
    auto it = f.find(name);
    if (it == f.end() || !it->is_number_integer()) return def;
    return it->get<int32_t>();
}

int hostGetBool(FateBehaviorCtx* ctx, const char* name, int def) {
    if (!ctx || !ctx->component || !name) return def;
    const auto& f = ctx->component->fields;
    auto it = f.find(name);
    if (it == f.end() || !it->is_boolean()) return def;
    return it->get<bool>() ? 1 : 0;
}

void hostSetFloat(FateBehaviorCtx* ctx, const char* name, float value) {
    if (!ctx || !ctx->component || !name) return;
    ctx->component->fields[name] = value;
}

void hostSetInt(FateBehaviorCtx* ctx, const char* name, int32_t value) {
    if (!ctx || !ctx->component || !name) return;
    ctx->component->fields[name] = value;
}

void hostSetBool(FateBehaviorCtx* ctx, const char* name, int value) {
    if (!ctx || !ctx->component || !name) return;
    ctx->component->fields[name] = (value != 0);
}

// Runtime fields — same shape, but writes/reads BehaviorComponent::runtimeFields,
// which is NOT serialized and is cleared by the host on every onDestroy.
float hostGetRuntimeFloat(FateBehaviorCtx* ctx, const char* name, float def) {
    if (!ctx || !ctx->component || !name) return def;
    const auto& f = ctx->component->runtimeFields;
    auto it = f.find(name);
    if (it == f.end() || !it->is_number()) return def;
    return it->get<float>();
}

int32_t hostGetRuntimeInt(FateBehaviorCtx* ctx, const char* name, int32_t def) {
    if (!ctx || !ctx->component || !name) return def;
    const auto& f = ctx->component->runtimeFields;
    auto it = f.find(name);
    if (it == f.end() || !it->is_number_integer()) return def;
    return it->get<int32_t>();
}

int hostGetRuntimeBool(FateBehaviorCtx* ctx, const char* name, int def) {
    if (!ctx || !ctx->component || !name) return def;
    const auto& f = ctx->component->runtimeFields;
    auto it = f.find(name);
    if (it == f.end() || !it->is_boolean()) return def;
    return it->get<bool>() ? 1 : 0;
}

void hostSetRuntimeFloat(FateBehaviorCtx* ctx, const char* name, float value) {
    if (!ctx || !ctx->component || !name) return;
    ctx->component->runtimeFields[name] = value;
}

void hostSetRuntimeInt(FateBehaviorCtx* ctx, const char* name, int32_t value) {
    if (!ctx || !ctx->component || !name) return;
    ctx->component->runtimeFields[name] = value;
}

void hostSetRuntimeBool(FateBehaviorCtx* ctx, const char* name, int value) {
    if (!ctx || !ctx->component || !name) return;
    ctx->component->runtimeFields[name] = (value != 0);
}

int hostGetEntityPos(FateBehaviorCtx* ctx, float* outX, float* outY) {
    if (!ctx || !ctx->entity) return 0;
    auto* tr = ctx->entity->getComponent<Transform>();
    if (!tr) return 0;
    if (outX) *outX = tr->position.x;
    if (outY) *outY = tr->position.y;
    return 1;
}

void hostSetEntityPos(FateBehaviorCtx* ctx, float x, float y) {
    if (!ctx || !ctx->entity) return;
    auto* tr = ctx->entity->getComponent<Transform>();
    if (!tr) return;
    tr->position = Vec2(x, y);
}

void* hostGetState(FateBehaviorCtx* ctx) {
    if (!ctx) return nullptr;
    return ctx->state;
}

void hostSetState(FateBehaviorCtx* ctx, void* state) {
    if (!ctx) return;
    ctx->state = state;
    // Mirror to the live component when it still exists, so the next
    // dispatch round (if any) sees the same pointer. After onDestroy fires
    // and the host clears component state, this assignment is a no-op
    // because component is null.
    if (ctx->component) ctx->component->state = state;
}

// ---------------------------------------------------------------------------
// P4 (S153) leaf SEH wrappers for module callback invocation.
//
// Constraint: under /EHsc, __try cannot appear in a function body that
// contains C++ objects requiring destruction. These leaf functions only
// touch primitive types, function pointers, and POD ctx structs — no
// std::string, no nlohmann::json, no smart pointers — so the constraint
// holds. Each takes a raw vtable function pointer + args, returns 0 on
// clean execution and 1 on SEH (Windows) or -1 on caught C++ exception.
//
// On non-Windows the SEH branch falls through to a plain call; C++
// exceptions are still caught by the C++ try/catch in the wrapper.
//
// Compile-gated to FATE_ENABLE_HOT_RELOAD by virtue of being in
// hot_reload_manager.cpp, which only links into non-shipping builds.
// (HrFaultInfo struct is defined above, in namespace fate, so the forward
// declarations of fault helpers below the anonymous namespace can see it.)
// ---------------------------------------------------------------------------

#ifdef _WIN32
// Forward decls so the C++-only wrapper can call the SEH-only leaf.
static int hr_seh_call_void(void(*fn)(FateBehaviorCtx*), FateBehaviorCtx* ctx,
                            uint32_t* outCode, void** outAddr);
static int hr_seh_call_update(void(*fn)(FateBehaviorCtx*, float), FateBehaviorCtx* ctx,
                              float dt, uint32_t* outCode, void** outAddr);
static int hr_seh_call_shutdown(void(*fn)(),
                                uint32_t* outCode, void** outAddr);
static int hr_seh_call_reload(int(*fn)(FateReloadContext*), FateReloadContext* ctx,
                              int* outResult, uint32_t* outCode, void** outAddr);
static int hr_seh_call_init(int(*fn)(const FateHostApi*, FateGameModuleApi*),
                            const FateHostApi* host, FateGameModuleApi* out,
                            int* outResult, uint32_t* outCode, void** outAddr);
#endif

// Public-to-the-cpp helper: invoke the function pointer with both SEH and
// C++ exception protection. Caller passes a function callback and HrFaultInfo.
//
// Implementation: each variant first runs C++ try/catch around the SEH-leaf
// call. If the leaf reports SEH, fault info is filled with code+address.
// If C++ exception thrown across the boundary (legal even though ABI says
// not to — modules using C++ standard library can leak), what() is copied.
static void hrCallVoid(void(*fn)(FateBehaviorCtx*), FateBehaviorCtx* ctx, HrFaultInfo* fi) {
    if (!fn) { return; }
    try {
#ifdef _WIN32
        if (hr_seh_call_void(fn, ctx, &fi->exceptionCode, &fi->exceptionAddr)) {
            fi->caught = 1;
        }
#else
        fn(ctx);
#endif
    } catch (const std::exception& ex) {
        fi->caught = -1;
        std::strncpy(fi->cppWhat, ex.what(), sizeof(fi->cppWhat) - 1);
    } catch (...) {
        fi->caught = -1;
        std::strncpy(fi->cppWhat, "unknown C++ exception", sizeof(fi->cppWhat) - 1);
    }
}

static void hrCallUpdate(void(*fn)(FateBehaviorCtx*, float), FateBehaviorCtx* ctx,
                         float dt, HrFaultInfo* fi) {
    if (!fn) { return; }
    try {
#ifdef _WIN32
        if (hr_seh_call_update(fn, ctx, dt, &fi->exceptionCode, &fi->exceptionAddr)) {
            fi->caught = 1;
        }
#else
        fn(ctx, dt);
#endif
    } catch (const std::exception& ex) {
        fi->caught = -1;
        std::strncpy(fi->cppWhat, ex.what(), sizeof(fi->cppWhat) - 1);
    } catch (...) {
        fi->caught = -1;
        std::strncpy(fi->cppWhat, "unknown C++ exception", sizeof(fi->cppWhat) - 1);
    }
}

static int hrCallShutdown(void(*fn)(), HrFaultInfo* fi) {
    if (!fn) { return 0; }
    try {
#ifdef _WIN32
        if (hr_seh_call_shutdown(fn, &fi->exceptionCode, &fi->exceptionAddr)) {
            fi->caught = 1;
            return 1;
        }
        return 0;
#else
        fn();
        return 0;
#endif
    } catch (const std::exception& ex) {
        fi->caught = -1;
        std::strncpy(fi->cppWhat, ex.what(), sizeof(fi->cppWhat) - 1);
        return 1;
    } catch (...) {
        fi->caught = -1;
        std::strncpy(fi->cppWhat, "unknown C++ exception", sizeof(fi->cppWhat) - 1);
        return 1;
    }
}

// Returns 0 on clean execution (and writes module's int result to *outResult);
// returns 1 on fault (outResult unchanged, fi populated).
static int hrCallReload(int(*fn)(FateReloadContext*), FateReloadContext* ctx,
                        int* outResult, HrFaultInfo* fi) {
    if (!fn) { *outResult = 0; return 0; }
    try {
#ifdef _WIN32
        if (hr_seh_call_reload(fn, ctx, outResult, &fi->exceptionCode, &fi->exceptionAddr)) {
            fi->caught = 1;
            return 1;
        }
        return 0;
#else
        *outResult = fn(ctx);
        return 0;
#endif
    } catch (const std::exception& ex) {
        fi->caught = -1;
        std::strncpy(fi->cppWhat, ex.what(), sizeof(fi->cppWhat) - 1);
        return 1;
    } catch (...) {
        fi->caught = -1;
        std::strncpy(fi->cppWhat, "unknown C++ exception", sizeof(fi->cppWhat) - 1);
        return 1;
    }
}

static int hrCallInit(int(*fn)(const FateHostApi*, FateGameModuleApi*),
                      const FateHostApi* host, FateGameModuleApi* out,
                      int* outResult, HrFaultInfo* fi) {
    if (!fn) { *outResult = 0; return 0; }
    try {
#ifdef _WIN32
        if (hr_seh_call_init(fn, host, out, outResult, &fi->exceptionCode, &fi->exceptionAddr)) {
            fi->caught = 1;
            return 1;
        }
        return 0;
#else
        *outResult = fn(host, out);
        return 0;
#endif
    } catch (const std::exception& ex) {
        fi->caught = -1;
        std::strncpy(fi->cppWhat, ex.what(), sizeof(fi->cppWhat) - 1);
        return 1;
    } catch (...) {
        fi->caught = -1;
        std::strncpy(fi->cppWhat, "unknown C++ exception", sizeof(fi->cppWhat) - 1);
        return 1;
    }
}

#ifdef _WIN32
// SEH-only leaf functions. /EHsc requires that __try not coexist with C++
// destructors — these only touch primitive types + function pointers.
// EXCEPTION_POINTERS is captured via GetExceptionInformation() in the
// filter expression, then we extract the address before the handler
// returns. Anti-pattern: NEVER touch a C++ object inside __try.

static int hr_seh_call_void(void(*fn)(FateBehaviorCtx*), FateBehaviorCtx* ctx,
                            uint32_t* outCode, void** outAddr) {
    EXCEPTION_POINTERS* xp = nullptr;
    __try {
        fn(ctx);
        return 0;
    } __except (xp = GetExceptionInformation(), EXCEPTION_EXECUTE_HANDLER) {
        if (xp && xp->ExceptionRecord) {
            *outCode = xp->ExceptionRecord->ExceptionCode;
            *outAddr = xp->ExceptionRecord->ExceptionAddress;
        }
        return 1;
    }
}

static int hr_seh_call_update(void(*fn)(FateBehaviorCtx*, float), FateBehaviorCtx* ctx,
                              float dt, uint32_t* outCode, void** outAddr) {
    EXCEPTION_POINTERS* xp = nullptr;
    __try {
        fn(ctx, dt);
        return 0;
    } __except (xp = GetExceptionInformation(), EXCEPTION_EXECUTE_HANDLER) {
        if (xp && xp->ExceptionRecord) {
            *outCode = xp->ExceptionRecord->ExceptionCode;
            *outAddr = xp->ExceptionRecord->ExceptionAddress;
        }
        return 1;
    }
}

static int hr_seh_call_shutdown(void(*fn)(), uint32_t* outCode, void** outAddr) {
    EXCEPTION_POINTERS* xp = nullptr;
    __try {
        fn();
        return 0;
    } __except (xp = GetExceptionInformation(), EXCEPTION_EXECUTE_HANDLER) {
        if (xp && xp->ExceptionRecord) {
            *outCode = xp->ExceptionRecord->ExceptionCode;
            *outAddr = xp->ExceptionRecord->ExceptionAddress;
        }
        return 1;
    }
}

static int hr_seh_call_reload(int(*fn)(FateReloadContext*), FateReloadContext* ctx,
                              int* outResult, uint32_t* outCode, void** outAddr) {
    EXCEPTION_POINTERS* xp = nullptr;
    __try {
        *outResult = fn(ctx);
        return 0;
    } __except (xp = GetExceptionInformation(), EXCEPTION_EXECUTE_HANDLER) {
        if (xp && xp->ExceptionRecord) {
            *outCode = xp->ExceptionRecord->ExceptionCode;
            *outAddr = xp->ExceptionRecord->ExceptionAddress;
        }
        return 1;
    }
}

static int hr_seh_call_init(int(*fn)(const FateHostApi*, FateGameModuleApi*),
                            const FateHostApi* host, FateGameModuleApi* out,
                            int* outResult, uint32_t* outCode, void** outAddr) {
    EXCEPTION_POINTERS* xp = nullptr;
    __try {
        *outResult = fn(host, out);
        return 0;
    } __except (xp = GetExceptionInformation(), EXCEPTION_EXECUTE_HANDLER) {
        if (xp && xp->ExceptionRecord) {
            *outCode = xp->ExceptionRecord->ExceptionCode;
            *outAddr = xp->ExceptionRecord->ExceptionAddress;
        }
        return 1;
    }
}
#endif // _WIN32

} // anonymous namespace

// Forward declarations for P4 (S153) fault helpers. Definitions live below,
// near the public lifecycle hooks. Declared here so performSwap, shutdown,
// and tickBehaviors (all earlier in this TU than the definitions) can call
// them without reordering the file.
static void quarantineActive(HotReloadManager::Active& a, Entity* e,
                              const char* phase, const HrFaultInfo& fi);
static void hrSetModuleDegraded(std::string& reasonOut, bool& flagOut,
                                 const char* phase, const HrFaultInfo& fi);

// P3 (S153) RAII safe-point guard. Sets a bool flag on construction, clears
// on destruction so early returns from processPendingReload (or from the
// initial-load path in initialize) leave the flag in a clean state.
namespace {
struct SafePointGuard {
    bool& flag;
    explicit SafePointGuard(bool& f) : flag(f) { flag = true; }
    ~SafePointGuard() { flag = false; }
    SafePointGuard(const SafePointGuard&) = delete;
    SafePointGuard& operator=(const SafePointGuard&) = delete;
};
} // namespace

// ---------------------------------------------------------------------------
HotReloadManager& HotReloadManager::instance() {
    static HotReloadManager s_instance;
    return s_instance;
}

#if FATE_ENABLE_HOT_RELOAD
// Forwarding shims used by engine/ecs/entity_inline.h's add/removeComponent
// specializations. Live here so the entity inline doesn't need to pull the
// full HotReloadManager header into world.h's include graph.
void hotReloadNotifyBehaviorComponentRemoved(World& world, EntityHandle handle) {
    HotReloadManager::instance().onBehaviorComponentRemoved(world, handle);
}
void hotReloadNotifyBehaviorComponentAdded(World& world, EntityHandle handle) {
    HotReloadManager::instance().onBehaviorComponentAdded(world, handle);
}
void hotReloadNotifyBehaviorRebind(World& world, EntityHandle handle) {
    HotReloadManager::instance().notifyBehaviorRebind(world, handle);
}
#endif

void HotReloadManager::ensureHostApi() {
    if (hostApiInitialized_) return;
    hostApi_.hostAbiVersion      = FATE_MODULE_ABI_VERSION;
    hostApi_.hostProtocolVersion = FATE_MODULE_PROTOCOL_VERSION;
    hostApi_.log                 = &hostLog;
    hostApi_.registerBehavior    = &hostRegisterBehavior;
    hostApi_.ctxEntityId         = &hostCtxEntityId;
    hostApi_.ctxIsEnabled        = &hostCtxIsEnabled;
    hostApi_.getFloat            = &hostGetFloat;
    hostApi_.getInt              = &hostGetInt;
    hostApi_.getBool             = &hostGetBool;
    hostApi_.setFloat            = &hostSetFloat;
    hostApi_.setInt              = &hostSetInt;
    hostApi_.setBool             = &hostSetBool;
    hostApi_.getRuntimeFloat     = &hostGetRuntimeFloat;
    hostApi_.getRuntimeInt       = &hostGetRuntimeInt;
    hostApi_.getRuntimeBool      = &hostGetRuntimeBool;
    hostApi_.setRuntimeFloat     = &hostSetRuntimeFloat;
    hostApi_.setRuntimeInt       = &hostSetRuntimeInt;
    hostApi_.setRuntimeBool      = &hostSetRuntimeBool;
    hostApi_.getEntityPos        = &hostGetEntityPos;
    hostApi_.setEntityPos        = &hostSetEntityPos;
    hostApi_.getState            = &hostGetState;
    hostApi_.setState            = &hostSetState;
    hostApiInitialized_ = true;
}

void HotReloadManager::onWatchEvent(const std::string& relPath) {
    // Only fire on the actual module artifact. The watcher runs on a worker
    // thread; we just set the atomic and let the main thread drive the swap.
    std::string lower = relPath;
    for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
    std::string target = moduleNameBase_ + ".dll";
    for (auto& c : target) c = (char)std::tolower((unsigned char)c);
    if (lower.find(target) == std::string::npos) return;

    reloadPending_.store(true, std::memory_order_release);
}

void HotReloadManager::onSourceWatchEvent(const std::string& relPath) {
    // Filter to compilable inputs — ignore editor swap/temp files and
    // non-source notifications. The artifact watcher is the one that
    // actually triggers reload; this watcher only kicks the build.
    auto endsWith = [](const std::string& s, const char* ext) {
        size_t n = std::strlen(ext);
        return s.size() >= n && std::equal(s.end() - n, s.end(), ext);
    };
    if (!endsWith(relPath, ".cpp") && !endsWith(relPath, ".h") &&
        !endsWith(relPath, ".hpp") && !endsWith(relPath, ".inl")) {
        return;
    }
    buildPending_.store(true, std::memory_order_release);
}

void HotReloadManager::enableSourceWatch(const std::string& sourceDir, const std::string& buildCmd) {
    if (sourceDir.empty() || buildCmd.empty()) return;
    sourceDir_ = sourceDir;
    buildCmd_  = buildCmd;
    sourceWatcher_.start(sourceDir_, [this](const std::string& rel) {
        onSourceWatchEvent(rel);
    });
    LOG_INFO("HotReload", "Source-watch enabled: %s (build: %s)",
             sourceDir_.c_str(), buildCmd_.c_str());
}

void HotReloadManager::joinBuildThread() {
    if (buildThread_.joinable()) buildThread_.join();
}

std::string HotReloadManager::buildLogTailSnapshot() const {
    std::lock_guard<std::mutex> lk(buildLogMutex_);
    return buildLogTail_;
}

void HotReloadManager::runBuildAsync() {
    // Don't queue a second build on top of a running one; the artifact
    // watcher will fire reload as soon as the in-flight build finishes.
    if (buildStatus_.load(std::memory_order_acquire) == BuildStatus::Running) return;

    joinBuildThread();  // reap a finished prior thread before launching the next.
    buildStatus_.store(BuildStatus::Running, std::memory_order_release);
    buildExitCode_.store(0, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(buildLogMutex_);
        buildLogTail_ = "(building...)";
    }

    const std::string cmd = buildCmd_;
    buildThread_ = std::thread([this, cmd]() {
        // Redirect stdout+stderr to a tail file so the editor panel can
        // surface the last few lines on failure.
        namespace fs = std::filesystem;
        fs::path tailPath = fs::path(moduleDir_) / "fate_module_shadow" / "build_tail.log";
        std::error_code ec;
        fs::create_directories(tailPath.parent_path(), ec);
        std::string redir = cmd + " > \"" + tailPath.string() + "\" 2>&1";

        int rc = std::system(redir.c_str());

        // Read the tail OUTSIDE the lock; we own the whole `all` string here
        // and only publish it into the shared buildLogTail_ at the end.
        std::string fresh;
        {
            std::ifstream f(tailPath);
            if (f) {
                std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                const size_t maxBytes = 4096;
                fresh = all.size() > maxBytes ? all.substr(all.size() - maxBytes) : all;
            }
        }
        {
            std::lock_guard<std::mutex> lk(buildLogMutex_);
            buildLogTail_ = std::move(fresh);
        }
        buildExitCode_.store(rc, std::memory_order_release);
        // Status flip MUST happen after the log + exit code are published, so
        // a panel reader that observes Succeeded/Failed via acquire-load sees
        // a coherent snapshot.
        buildStatus_.store(rc == 0 ? BuildStatus::Succeeded : BuildStatus::Failed,
                           std::memory_order_release);
        LOG_INFO("HotReload", "Build finished rc=%d", rc);
    });
}

bool HotReloadManager::initialize(const std::string& moduleDir, const std::string& moduleName) {
    moduleDir_       = moduleDir;
    moduleNameBase_  = moduleName;
    sourcePath_      = moduleDir + "/" + moduleName + ".dll";
    shadowDir_       = moduleDir + "/fate_module_shadow";

    ensureHostApi();

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(shadowDir_, ec);
    if (ec) {
        lastError_ = "create_directories(" + shadowDir_ + ") failed: " + ec.message();
        LOG_WARN("HotReload", "%s", lastError_.c_str());
    }

    // Start watching the module dir before loading. Edits made during the
    // first load (race with the build) will queue a reload that runs on the
    // next frame.
    watcher_.start(moduleDir_, [this](const std::string& rel) { onWatchEvent(rel); });

    // First load. Absent module is not a failure — just leave currentHandle_
    // null; subsequent edits will populate it.
    if (!fs::exists(sourcePath_)) {
        LOG_INFO("HotReload", "No game module at %s — running headless", sourcePath_.c_str());
        return false;
    }

    // Treat the initial load as a reload-from-nothing; the swap pipeline
    // already handles "no current module" cleanly.
    // P3 (S153): initialize is structurally a safe point (App::initialize
    // runs before any system tick / network / render). Set the flag for
    // the duration of the first-load swap so performSwap's gate accepts.
    if (mainThreadId_ == std::thread::id{}) {
        mainThreadId_ = std::this_thread::get_id();
    }
    SafePointGuard sg(inSafePoint_);
    return performSwap(0.0f);
}

void HotReloadManager::shutdown() {
    watcher_.stop();
    sourceWatcher_.stop();
    joinBuildThread();

    // Tear down every active behavior BEFORE we drop the module handles, so
    // the cached vtable pointers that destroyOne dispatches against are
    // still valid. teardownActiveBehaviors drains the entire roster.
    teardownActiveBehaviors();

#ifdef _WIN32
    if (currentHandle_) {
        auto shutdownFn = (void(*)())GetProcAddress((HMODULE)currentHandle_, FATE_SYM_SHUTDOWN);
        if (shutdownFn) {
            HrFaultInfo fi;
            if (hrCallShutdown(shutdownFn, &fi)) {
                hrSetModuleDegraded(moduleDegradedReason_, moduleDegraded_,
                                    "fateGameModuleShutdown (manager shutdown)", fi);
            }
        }
        BehaviorRegistry::instance().clear();
        FreeLibrary((HMODULE)currentHandle_);
        currentHandle_ = nullptr;
    }
    if (previousHandle_) {
        FreeLibrary((HMODULE)previousHandle_);
        previousHandle_ = nullptr;
    }
#endif

    moduleApi_ = FateGameModuleApi{};
    moduleNameStr_.clear();
    moduleBuildIdStr_.clear();
    activeShadowPath_.clear();
    replicatedWarnedHandles_.clear();
    missedBindWarnedHandles_.clear();
    frameCounterForSafetyNet_ = 0;
    // P3 (S153) safe-point bookkeeping reset. Next session captures a
    // fresh main-thread id (matters for the unit-test runner where each
    // TEST_CASE call hardReset's the manager).
    mainThreadId_ = std::thread::id{};
    inSafePoint_  = false;
    moduleDegraded_ = false;
    moduleDegradedReason_.clear();
}

void HotReloadManager::requestManualReload(const char* reason) {
    LOG_INFO("HotReload", "Manual reload requested: %s", reason ? reason : "(no reason)");
    reloadPending_.store(true, std::memory_order_release);
    reloadRequestedAt_ = -1.0f;  // re-stamp on next process call so debounce starts now
}

void HotReloadManager::processPendingReload(float currentTime) {
    // P3 (S153) main-thread + safe-point contract. The caller (App::update)
    // must invoke this once per frame at the top, before any system tick,
    // network dispatch, render callback, or input handler. We capture the
    // thread on first call; subsequent calls from any other thread are
    // refused with a loud error (no swap can race the main loop).
    const auto tid = std::this_thread::get_id();
    if (mainThreadId_ == std::thread::id{}) {
        mainThreadId_ = tid;
    } else if (mainThreadId_ != tid) {
        LOG_ERROR("HotReload",
            "processPendingReload called from non-main thread — refusing swap. "
            "Reload pipeline is single-threaded; only call from App::update.");
        return;
    }
    SafePointGuard sg(inSafePoint_);

    // Source-side build trigger — kicks BEFORE the artifact-reload check so
    // a save-and-rebuild round trip happens entirely under one frame's
    // process call. The artifact watcher will fire reloadPending_ when the
    // build's DLL drop completes.
    //
    // Drop-prevention: if a build is already running, we LEAVE buildPending_
    // set and reset the debounce timestamp. When the worker finishes (status
    // flips to Succeeded/Failed), the next process call observes pending +
    // status!=Running and kicks a fresh build that picks up the newer source
    // change. Without this latch, an edit that lands during a 30 s build
    // would be lost.
    if (buildPending_.load(std::memory_order_acquire) && !buildCmd_.empty()) {
        if (buildStatus_.load(std::memory_order_acquire) == BuildStatus::Running) {
            // Hold the flag; reset debounce stamp so we re-debounce after the
            // current build finishes (not 0.4 s after the original save).
            buildRequestedAt_ = -1.0f;
        } else if (buildRequestedAt_ < 0.0f) {
            buildRequestedAt_ = currentTime;
        } else if (currentTime - buildRequestedAt_ >= kBuildDebounce) {
            buildPending_.store(false, std::memory_order_release);
            buildRequestedAt_ = -1.0f;
            LOG_INFO("HotReload", "Source change debounced — kicking build");
            runBuildAsync();
        }
    }

    if (!reloadPending_.load(std::memory_order_acquire)) return;

    if (reloadRequestedAt_ < 0.0f) {
        reloadRequestedAt_ = currentTime;
        return;  // start the debounce window; revisit next frame
    }
    if (currentTime - reloadRequestedAt_ < kReloadDebounce) return;

#ifndef FATE_SHIPPING
    // Play-mode guard. The reload point is structurally safe for editor mode
    // (no systems running yet), but in play mode network packet dispatch +
    // combat resolution + AOI iteration are live and we don't yet guarantee
    // they're quiesced at this exact frame slot. Hold the pending flag and
    // revisit when the editor returns to edit mode.
    if (!allowPlayModeReload_ && Editor::instance().inPlayMode()) {
        if (!playModeWarned_) {
            LOG_WARN("HotReload", "Module changed but play-mode reload is disabled — will retry when play mode exits.");
            playModeWarned_ = true;
        }
        return;  // keep flag set; don't clear timestamp so we don't re-debounce
    }
    playModeWarned_ = false;
#endif

    reloadPending_.store(false, std::memory_order_release);
    reloadRequestedAt_ = -1.0f;

    if (!performSwap(currentTime)) {
        // performSwap re-arms reloadPending_ when the failure is a transient
        // build-state condition (DLL temporarily missing, LoadLibrary races
        // a still-finalizing link). In that case we keep retrying without
        // bumping failureCount_ or screaming a warn, until kMaxTransientRetries
        // is hit and we escalate to a hard failure.
        const bool transient = reloadPending_.load(std::memory_order_acquire);
        if (transient) {
            ++transientRetries_;
            if (transientRetries_ > kMaxTransientRetries) {
                reloadPending_.store(false, std::memory_order_release);
                reloadRequestedAt_ = -1.0f;
                ++failureCount_;
                transientRetries_ = 0;
                transientWarned_ = false;
                LOG_WARN("HotReload",
                    "Reload gave up after %d transient retries: %s (current module preserved)",
                    kMaxTransientRetries, lastError_.c_str());
            } else if (!transientWarned_) {
                LOG_INFO("HotReload", "Swap deferred (build still in progress): %s",
                         lastError_.c_str());
                transientWarned_ = true;
            }
        } else {
            ++failureCount_;
            transientRetries_ = 0;
            transientWarned_ = false;
            LOG_WARN("HotReload", "Reload failed: %s (current module preserved)",
                     lastError_.c_str());
        }
    } else {
        // Successful swap — clear transient throttle state.
        transientRetries_ = 0;
        transientWarned_ = false;
    }
}

bool HotReloadManager::performSwap(float /*currentTime*/) {
    // P3 (S153) safe-point gate. performSwap mutates structural state
    // (BehaviorRegistry generation, currentHandle_, active_ bindings) —
    // running it outside the App::update top-of-frame window is unsafe.
    // processPendingReload sets inSafePoint_ via SafePointGuard for its
    // entire body; any other call site (a future code path that forgot
    // the contract) gets caught here.
    if (!inSafePoint_) {
        lastError_ = "performSwap called outside processPendingReload safe-point window — refusing structural mutation";
        LOG_ERROR("HotReload", "%s", lastError_.c_str());
        return false;
    }
#ifndef _WIN32
    lastError_ = "Hot reload only implemented on Windows in this slice";
    return false;
#else
    namespace fs = std::filesystem;
    std::error_code ec;

    // 1. Build a unique shadow path. The counter is monotonic so an old
    //    shadow file never collides with a new load even if FreeLibrary on
    //    the previous instance is still in flight.
    ++shadowCounter_;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "_%04u", shadowCounter_);
    std::string shadow = shadowDir_ + "/" + moduleNameBase_ + buf + ".dll";

    if (!fs::exists(sourcePath_)) {
        // Build artifact temporarily missing during a link transition (linker
        // deletes/rewrites the .dll, watcher fires mid-write). Re-arm pending
        // so we retry once the linker finishes; caller observes the re-armed
        // flag and treats this as a transient retry, NOT a hard failure.
        lastError_ = "Source DLL missing during build transition (will retry)";
        reloadPending_.store(true, std::memory_order_release);
        reloadRequestedAt_ = -1.0f;
        return false;
    }

    fs::copy_file(sourcePath_, shadow, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        // Common cause: linker still writing the file. Re-arm pending so we
        // retry next frame with a fresh debounce.
        lastError_ = "copy_file(" + sourcePath_ + " -> " + shadow + ") failed: " + ec.message();
        reloadPending_.store(true, std::memory_order_release);
        reloadRequestedAt_ = -1.0f;
        return false;
    }

    // 2. LoadLibrary on the shadow copy. The original sourcePath_ stays
    //    writeable so the next build can overwrite it.
    HMODULE newHandle = LoadLibraryA(shadow.c_str());
    if (!newHandle) {
        DWORD err = GetLastError();
        lastError_ = "LoadLibrary(" + shadow + ") failed: error " + std::to_string(err);
        fs::remove(shadow, ec);
        // Treat as transient — DLL may still be mid-write or AV-locked. Re-arm
        // pending so we retry; caller observes re-armed flag and skips
        // bumping failureCount_ until the retry cap escalates.
        reloadPending_.store(true, std::memory_order_release);
        reloadRequestedAt_ = -1.0f;
        return false;
    }

    // 3. Resolve required symbols. Missing any of them = fatal for this
    //    module; old module remains active.
    auto queryFn = (void(*)(uint32_t*, uint32_t*, uint32_t*, uint32_t*))GetProcAddress(newHandle, FATE_SYM_QUERY_VERSION);
    auto initFn = (int(*)(const FateHostApi*, FateGameModuleApi*))GetProcAddress(newHandle, FATE_SYM_INIT);
    auto shutdownFn = (void(*)())GetProcAddress(newHandle, FATE_SYM_SHUTDOWN);
    auto beginFn = (int(*)(FateReloadContext*))GetProcAddress(newHandle, FATE_SYM_BEGIN_RELOAD);
    auto endFn = (int(*)(FateReloadContext*))GetProcAddress(newHandle, FATE_SYM_END_RELOAD);
    if (!queryFn || !initFn || !shutdownFn || !beginFn || !endFn) {
        lastError_ = "Module missing required exports (init/shutdown/begin/end/queryVersion)";
        FreeLibrary(newHandle);
        fs::remove(shadow, ec);
        return false;
    }

    // 4. Version + struct-size check BEFORE any state mutation. Reject
    //    without touching BehaviorRegistry or any live handle. Size checks
    //    catch packing / ODR / stale-header drift even when version matches.
    uint32_t modAbi = 0, modProto = 0, modHostSize = 0, modModuleSize = 0;
    queryFn(&modAbi, &modProto, &modHostSize, &modModuleSize);
    if (modAbi != FATE_MODULE_ABI_VERSION || modProto != FATE_MODULE_PROTOCOL_VERSION) {
        char emsg[256];
        std::snprintf(emsg, sizeof(emsg),
            "ABI/protocol mismatch: host abi=%u proto=%u, module abi=%u proto=%u",
            FATE_MODULE_ABI_VERSION, FATE_MODULE_PROTOCOL_VERSION, modAbi, modProto);
        lastError_ = emsg;
        FreeLibrary(newHandle);
        fs::remove(shadow, ec);
        return false;
    }
    const uint32_t hostHostSize   = (uint32_t)sizeof(FateHostApi);
    const uint32_t hostModuleSize = (uint32_t)sizeof(FateGameModuleApi);
    if (modHostSize != hostHostSize || modModuleSize != hostModuleSize) {
        char emsg[256];
        std::snprintf(emsg, sizeof(emsg),
            "ABI struct-size mismatch (stale header on one side?): "
            "host FateHostApi=%u/%u, FateGameModuleApi=%u/%u",
            modHostSize, hostHostSize, modModuleSize, hostModuleSize);
        lastError_ = emsg;
        FreeLibrary(newHandle);
        fs::remove(shadow, ec);
        return false;
    }

    // 5. Stage new behaviors. registerBehavior writes to staging during init
    //    so the live registry is untouched if init returns failure.
    BehaviorRegistry::instance().beginStaging();

    FateGameModuleApi newApi{};
    int initOk = 0;
    {
        HrFaultInfo fi;
        if (hrCallInit(initFn, &hostApi_, &newApi, &initOk, &fi)) {
            // Init faulted under SEH or threw. Module is unusable; do NOT
            // commit. Mark module degraded for post-mortem visibility.
            // Note: we have NOT called shutdownFn yet because init never
            // returned cleanly — calling shutdown on a half-initialized
            // module is itself unsafe, so we just FreeLibrary.
            hrSetModuleDegraded(moduleDegradedReason_, moduleDegraded_,
                                "fateGameModuleInit", fi);
            lastError_ = "fateGameModuleInit faulted (see degraded reason)";
            BehaviorRegistry::instance().abortStaging();
            FreeLibrary(newHandle);
            fs::remove(shadow, ec);
            return false;
        }
    }
    if (!initOk) {
        lastError_ = "fateGameModuleInit returned 0";
        BehaviorRegistry::instance().abortStaging();
        FreeLibrary(newHandle);
        fs::remove(shadow, ec);
        return false;
    }

    if (newApi.moduleAbiVersion != FATE_MODULE_ABI_VERSION ||
        newApi.moduleProtocolVersion != FATE_MODULE_PROTOCOL_VERSION) {
        // Belt-and-suspenders: should already be caught by queryFn, but the
        // module owner of these fields might mismatch the static query.
        // POST-INIT path: shutdownFn MUST run so the new module can release
        // anything it allocated during init() (scratch buffers, registered
        // resources, host pointers it cached).
        lastError_ = "module API struct version disagrees with QueryVersion";
        BehaviorRegistry::instance().abortStaging();
        if (shutdownFn) shutdownFn();
        FreeLibrary(newHandle);
        fs::remove(shadow, ec);
        return false;
    }

    // 6. Ask the outgoing module FIRST whether it accepts the swap. This
    //    runs BEFORE any state mutation (no teardown, no registry clear) so
    //    a BeginReload veto leaves both the live registry and the roster
    //    fully intact and dispatchable against the OLD vtables.
    if (currentHandle_) {
        auto oldBegin = (int(*)(FateReloadContext*))GetProcAddress((HMODULE)currentHandle_, FATE_SYM_BEGIN_RELOAD);
        if (oldBegin) {
            FateReloadContext rctx{};
            rctx.fromAbiVersion      = FATE_MODULE_ABI_VERSION;
            rctx.toAbiVersion        = FATE_MODULE_ABI_VERSION;
            rctx.fromProtocolVersion = FATE_MODULE_PROTOCOL_VERSION;
            rctx.toProtocolVersion   = FATE_MODULE_PROTOCOL_VERSION;
            rctx.generation          = reloadCount_;
            int beginOk = 0;
            HrFaultInfo fi;
            if (hrCallReload(oldBegin, &rctx, &beginOk, &fi)) {
                // P4: outgoing BeginReload faulted. Per the user's tier
                // policy, mark the OUTGOING module degraded (we stop
                // trusting it). The swap PROCEEDS — the new module just
                // didn't get a clean handshake from the old one. We still
                // need to call shutdownFn on the new module if we abort,
                // but here we let the swap continue.
                hrSetModuleDegraded(moduleDegradedReason_, moduleDegraded_,
                                    "outgoing fateGameModuleBeginReload", fi);
                // Treat as "veto" semantically — the outgoing module is
                // no longer reliable, so honor the abort path.
                lastError_ = "outgoing module's fateGameModuleBeginReload faulted (swap aborted; old module marked degraded)";
                BehaviorRegistry::instance().abortStaging();
                if (shutdownFn) shutdownFn();
                FreeLibrary(newHandle);
                fs::remove(shadow, ec);
                return false;
            }
            if (!beginOk) {
                // POST-INIT abort path: same shutdown invariant as the
                // version-mismatch case above. The new module's init() ran;
                // it owns scratch we have to give it a chance to free.
                lastError_ = "outgoing module's fateGameModuleBeginReload returned 0 (swap aborted; old module + roster preserved)";
                BehaviorRegistry::instance().abortStaging();
                if (shutdownFn) shutdownFn();
                FreeLibrary(newHandle);
                fs::remove(shadow, ec);
                return false;
            }
        }

        // Outgoing module accepted the reload. NOW it is safe to tear down
        // per-instance state — walk the live roster, call onDestroy on every
        // bound behavior via its CACHED old vtable (still points into the
        // soon-to-be-superseded module). Frees module-owned scratch and
        // nulls bc->state. MUST run before commitStaging or the cached
        // vtable pointers become unreachable.
        //
        // P2 (S153): teardownActiveBindings (NOT teardownActiveBehaviors)
        // because the dense roster needs to KEEP entries across reloads —
        // the next tickBehaviors lazy-resolves new vtables from the post-
        // commit registry. Draining active_ here would orphan the bound
        // entities (they still carry BehaviorComponent), which would only
        // be recovered by the safety-net sweep ~60 ticks later.
        teardownActiveBindings();

        // Old module gets a chance to fully shut down its statics. Note that
        // FreeLibrary on currentHandle_ is deferred by one swap (parked in
        // previousHandle_) so any in-flight stack frames still in old code
        // when this returns can finish unwinding.
        auto oldShutdown = (void(*)())GetProcAddress((HMODULE)currentHandle_, FATE_SYM_SHUTDOWN);
        if (oldShutdown) {
            HrFaultInfo fi;
            if (hrCallShutdown(oldShutdown, &fi)) {
                // Old module's shutdown faulted. It's about to be
                // FreeLibrary'd anyway, so we just log + flag degraded
                // and continue.
                hrSetModuleDegraded(moduleDegradedReason_, moduleDegraded_,
                                    "outgoing fateGameModuleShutdown", fi);
            }
        }
    }

    // 7. Commit staging — registry generation bumps, BehaviorComponent
    //    dispatchers will see (boundGeneration != gen) and re-run onStart.
    BehaviorRegistry::instance().commitStaging();

    // 8. Promote handles. previousHandle_ from the LAST swap is now safe to
    //    free (one full reload-cycle of grace), and currentHandle_ becomes
    //    the "previous" until the NEXT swap.
    if (previousHandle_) {
        FreeLibrary((HMODULE)previousHandle_);
        previousHandle_ = nullptr;
    }
    previousHandle_  = currentHandle_;
    currentHandle_   = newHandle;

    // 9. Track new module identity + give it a chance to settle in.
    moduleApi_        = newApi;
    moduleNameStr_    = newApi.moduleName    ? newApi.moduleName    : "(unnamed)";
    moduleBuildIdStr_ = newApi.moduleBuildId ? newApi.moduleBuildId : "";
    activeShadowPath_ = shadow;

    FateReloadContext endCtx{};
    endCtx.fromAbiVersion      = FATE_MODULE_ABI_VERSION;
    endCtx.toAbiVersion        = FATE_MODULE_ABI_VERSION;
    endCtx.fromProtocolVersion = FATE_MODULE_PROTOCOL_VERSION;
    endCtx.toProtocolVersion   = FATE_MODULE_PROTOCOL_VERSION;
    endCtx.generation          = reloadCount_ + 1;
    int endOk = 0;
    {
        HrFaultInfo fi;
        if (hrCallReload(endFn, &endCtx, &endOk, &fi)) {
            // EndReload faulted. New module is live (we already promoted
            // handles), but its post-swap setup is broken. Mark degraded.
            hrSetModuleDegraded(moduleDegradedReason_, moduleDegraded_,
                                "fateGameModuleEndReload", fi);
            lastError_ = "fateGameModuleEndReload faulted (module live but degraded)";
            ++failureCount_;
            endOk = 0;
        }
    }
    if (!endOk) {
        // EndReload failure means the new module is live but its post-swap
        // setup didn't complete. We don't roll back — the old module is
        // already shut down — but we mark the manager as degraded so the
        // editor can surface it.
        if (lastError_.empty()) {
            lastError_  = "fateGameModuleEndReload returned 0 (module live but degraded)";
            ++failureCount_;
        }
        LOG_WARN("HotReload", "%s", lastError_.c_str());
    } else {
        lastError_.clear();
        // Clean reload — clear module-degraded flag (the new code is fresh).
        moduleDegraded_ = false;
        moduleDegradedReason_.clear();
    }

    ++reloadCount_;
    LOG_INFO("HotReload", "Loaded module '%s' build=[%s] gen=%u (shadow=%s)",
             moduleNameStr_.c_str(), moduleBuildIdStr_.c_str(),
             reloadCount_, shadow.c_str());
    return true;
#endif
}

// ---------------------------------------------------------------------------
// Roster lookup + per-entry teardown helpers.
// ---------------------------------------------------------------------------
int HotReloadManager::findActive(World* world, EntityHandle handle) const {
    for (size_t i = 0; i < active_.size(); ++i) {
        if (active_[i].world == world && active_[i].handle == handle) {
            return (int)i;
        }
    }
    return -1;
}

void HotReloadManager::destroyOne(int idx) {
    if (idx < 0 || idx >= (int)active_.size()) return;
    Active& a = active_[idx];

    // Re-resolve the component from the world rather than trusting the cached
    // pointer — the entity may have been destroyed or had its archetype
    // reorganized since the last tick. If the component is gone we still
    // call onDestroy with the cached state so the module can free its
    // scratch (it never sees a stale or null state pointer in onDestroy).
    Entity*            ent = (a.world && a.world->isAlive(a.handle)) ? a.world->getEntity(a.handle) : nullptr;
    BehaviorComponent* bc  = ent ? ent->getComponent<BehaviorComponent>() : nullptr;

    // Refresh cached state from the live component if we still have one;
    // otherwise the cached state from the last tick is still valid.
    void* stateForDestroy = bc ? bc->state : a.cachedState;

    FateBehaviorCtx ctx{a.world, ent, bc, stateForDestroy};
    if (a.vtable && a.vtable->onDestroy) {
        // P4 (S153): wrap onDestroy in fault guard. We're about to erase
        // the entry anyway, so on fault we just log + continue cleanup —
        // no quarantine needed (there's nothing left to skip).
        HrFaultInfo fi;
        hrCallVoid(a.vtable->onDestroy, &ctx, &fi);
        if (fi.caught != 0) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "entity=%u behavior='%s' onDestroy %s (continuing cleanup)",
                ent ? (unsigned)ent->id() : 0u, a.behaviorName.c_str(),
                fi.caught == 1 ? "SEH" : "C++ exception");
            LOG_ERROR("HotReload", "%s", buf);
        }
    }
    // Module had its chance to free. Clear scratch + runtime fields on the
    // live component (if any). cachedState on the roster is about to go
    // away with the entry erase.
    if (bc) {
        bc->state = nullptr;
        bc->runtimeFields = nlohmann::json::object();
        bc->boundGeneration = 0;
    }

    active_.erase(active_.begin() + idx);
}

void HotReloadManager::teardownActiveBehaviors() {
    // Drains active_ entirely. Used by shutdown when the manager itself is
    // going away. Walk in reverse so erase indices stay valid.
    while (!active_.empty()) {
        destroyOne((int)active_.size() - 1);
    }
}

void HotReloadManager::teardownActiveBindings() {
    // Reload-cycle teardown. Fires onDestroy on each entry's CACHED vtable
    // (still valid, points into the soon-to-be-superseded module), clears
    // bc->state + bc->runtimeFields + boundGeneration, nulls a.vtable +
    // behaviorName + cachedState — but KEEPS the entry. The next
    // tickBehaviors call's lazy-resolve picks up the new vtable from the
    // post-commit registry and re-fires onStart.
    for (Active& a : active_) {
        Entity* e = (a.world && a.world->isAlive(a.handle))
                    ? a.world->getEntity(a.handle) : nullptr;
        BehaviorComponent* bc = e ? e->getComponent<BehaviorComponent>() : nullptr;
        void* stateForDestroy = bc ? bc->state : a.cachedState;

        if (a.vtable && a.vtable->onDestroy) {
            FateBehaviorCtx ctx{a.world, e, bc, stateForDestroy};
            // P4: onDestroy fault on reload teardown — log + continue, we
            // null the binding either way. Don't quarantine (we're about
            // to null vtable; nothing to skip).
            HrFaultInfo fi;
            hrCallVoid(a.vtable->onDestroy, &ctx, &fi);
            if (fi.caught != 0) {
                LOG_ERROR("HotReload",
                    "entity=%u behavior='%s' onDestroy faulted during reload teardown — continuing",
                    e ? (unsigned)e->id() : 0u, a.behaviorName.c_str());
            }
        }
        if (bc) {
            bc->state = nullptr;
            bc->runtimeFields = nlohmann::json::object();
            bc->boundGeneration = 0;
        }
        a.vtable       = nullptr;
        a.behaviorName.clear();
        a.cachedState  = nullptr;
        // Clearing fault state too — the new module's binding gets a fresh
        // chance. Persistent module bugs will re-fault on the new vtable.
        a.faulted      = false;
        a.faultMessage.clear();
    }
}

// ---------------------------------------------------------------------------
// Public lifecycle hooks.
// ---------------------------------------------------------------------------
void HotReloadManager::onWorldUnload(World& world) {
    // Caller is about to destroy `world`. Tear down every roster entry
    // whose world matches, even if the entity is already gone. After this
    // returns it is safe for the caller to destroy the World.
    for (int i = (int)active_.size() - 1; i >= 0; --i) {
        if (active_[i].world == &world) destroyOne(i);
    }
}

void HotReloadManager::onEntityDestroyed(World& world, EntityHandle handle) {
    int idx = findActive(&world, handle);
    if (idx >= 0) destroyOne(idx);
}

void HotReloadManager::onBehaviorComponentRemoved(World& world, EntityHandle handle) {
    // Same shape as onEntityDestroyed but the entity is still alive — just
    // its component is going. destroyOne is correct either way.
    int idx = findActive(&world, handle);
    if (idx >= 0) destroyOne(idx);
}

void HotReloadManager::onBehaviorComponentAdded(World& world, EntityHandle handle) {
    // Idempotent: addComponent + addComponentById can both fire for the same
    // sequence (the inline addComponent calls addComponentToEntity, not
    // addComponentById, so usually just one fires — but defensive checks
    // are cheap and we'd rather have one-roster-entry-per-handle than
    // duplicates).
    if (findActive(&world, handle) >= 0) return;

    Active a;
    a.world         = &world;
    a.handle        = handle;
    a.component     = nullptr;   // re-resolved each tick from world+handle
    a.vtable        = nullptr;   // lazy: tick resolves once bc->behavior is set
    a.behaviorName.clear();
    a.cachedState   = nullptr;
    a.seenThisTick  = false;
    active_.push_back(std::move(a));
}

void HotReloadManager::notifyBehaviorRebind(World& world, EntityHandle handle) {
    // The tick auto-detects name changes (a.behaviorName != bc->behavior).
    // Explicit rebind exists for callers that want immediate-rebind
    // semantics — currently it's a no-op because the next tick handles
    // it. Defensive add: if the entity isn't tracked yet (some weird
    // edit path bypassed the bind hook), treat as a fresh add.
    if (findActive(&world, handle) < 0) onBehaviorComponentAdded(world, handle);
}

// P4 (S153) helper: format + apply a per-instance fault. Sets a.faulted=true
// + a.faultMessage with entity id + behavior name + fault detail; logs once.
// Subsequent ticks see a.faulted and skip dispatch.
static void quarantineActive(HotReloadManager::Active& a, Entity* e,
                              const char* phase, const HrFaultInfo& fi) {
    char buf[512];
    const unsigned eid = e ? (unsigned)e->id() : 0u;
    if (fi.caught == 1) {
        std::snprintf(buf, sizeof(buf),
            "entity=%u behavior='%s' phase=%s SEH code=0x%08X addr=%p",
            eid, a.behaviorName.c_str(), phase,
            fi.exceptionCode, fi.exceptionAddr);
    } else {
        std::snprintf(buf, sizeof(buf),
            "entity=%u behavior='%s' phase=%s C++ exception: %s",
            eid, a.behaviorName.c_str(), phase, fi.cppWhat);
    }
    a.faulted     = true;
    a.faultMessage = buf;
    LOG_ERROR("HotReload",
        "Behavior quarantined after fault: %s (no further dispatch on this "
        "instance until editor 'Re-arm' clears the fault flag)", buf);
}

// P4 (S153) module-degraded set + log. Called when a module lifecycle
// callback faults. Only one degraded log per (phase, reason) so repeated
// faults from the same path don't spam.
static void hrSetModuleDegraded(std::string& reasonOut, bool& flagOut,
                                 const char* phase, const HrFaultInfo& fi) {
    char buf[512];
    if (fi.caught == 1) {
        std::snprintf(buf, sizeof(buf),
            "module callback %s faulted (SEH code=0x%08X addr=%p) — module marked degraded",
            phase, fi.exceptionCode, fi.exceptionAddr);
    } else {
        std::snprintf(buf, sizeof(buf),
            "module callback %s faulted (C++ exception: %s) — module marked degraded",
            phase, fi.cppWhat);
    }
    if (!flagOut || reasonOut != buf) {
        LOG_ERROR("HotReload", "%s", buf);
    }
    flagOut    = true;
    reasonOut  = buf;
}

std::vector<HotReloadManager::FaultedRow> HotReloadManager::faultedBehaviors() const {
    std::vector<FaultedRow> out;
    out.reserve(active_.size());
    for (const Active& a : active_) {
        if (!a.faulted) continue;
        FaultedRow row;
        Entity* e = (a.world && a.world->isAlive(a.handle))
                    ? a.world->getEntity(a.handle) : nullptr;
        row.entityId     = e ? (uint32_t)e->id() : 0u;
        row.behaviorName = a.behaviorName;
        row.detail       = a.faultMessage;
        out.push_back(std::move(row));
    }
    return out;
}

void HotReloadManager::clearAllFaults() {
    int cleared = 0;
    for (Active& a : active_) {
        if (a.faulted) {
            a.faulted = false;
            a.faultMessage.clear();
            ++cleared;
        }
    }
    if (cleared > 0) {
        LOG_INFO("HotReload",
            "Re-armed %d quarantined behavior instance(s); next tick will retry dispatch",
            cleared);
    }
}

void HotReloadManager::clearModuleDegraded() {
    if (moduleDegraded_) {
        LOG_INFO("HotReload",
            "Cleared module-degraded flag (was: %s)",
            moduleDegradedReason_.c_str());
    }
    moduleDegraded_ = false;
    moduleDegradedReason_.clear();
}

void HotReloadManager::onComponentAddedNotification(
        World& world, EntityHandle handle, uint32_t compId) {
    // Cheap CompId compare. Cached after first call so the BehaviorRegistry
    // is not pinged by every World::addComponentById in the codebase
    // (Transform, Sprite, Velocity, … all flow through here).
    static const uint32_t kBehaviorCompId = componentId<BehaviorComponent>();
    if (compId == kBehaviorCompId) {
        onBehaviorComponentAdded(world, handle);
    }
}

// ---------------------------------------------------------------------------
// Per-frame dispatch (P2, S153).
//
// Iterates the dense active_ roster directly. Roster membership is
// maintained by event-style hooks: onBehaviorComponentAdded fires on
// component construction; onBehaviorComponentRemoved fires on component
// removal; onEntityDestroyed fires on entity destroy queue drain;
// onWorldUnload fires on scene swap. Behavior-name changes are detected
// by comparing a.behaviorName against bc->behavior every tick (cheap
// because we're only iterating bound entries).
//
// Algorithm:
//   For each Active entry in this world:
//     a. Re-resolve Entity* + BehaviorComponent* from the world (handles
//        archetype migration). If either is gone, destroyOne + erase.
//     b. If entity is now replicated: warn-once, destroyOne + erase
//        (server-authoritative — never tick a behavior on a replicated
//        ghost; client writes would race the next replication frame).
//     c. If !enabled or behavior name empty: skip dispatch but keep entry.
//     d. Lazy-resolve / re-resolve vtable when null OR name changed.
//        On rebind: fire OLD onDestroy first, clear state/runtimeFields,
//        then bind new + onStart.
//     e. Dispatch onUpdate.
//
// Then runs the dev-only safety-net sweep at a throttled cadence
// (kSafetyNetTickInterval frames) to catch any code path that adds a
// BehaviorComponent without firing the bind hook. Caught misses are
// recovered + warn-once-per-handle.
// ---------------------------------------------------------------------------
void HotReloadManager::tickBehaviors(World& world, float dt) {
    // Module-global tick (optional).
    if (moduleApi_.tick) {
        moduleApi_.tick(dt);
    }

    auto& reg = BehaviorRegistry::instance();
    const uint32_t gen = reg.generation();

    // Iterate active_ directly. We use index-based iteration so destroyOne
    // (which erases) doesn't invalidate the loop. Re-fetch the size each
    // step because new entries can be appended by the rebind path on a
    // freshly-registered behavior (rare but legal).
    for (int i = 0; i < (int)active_.size(); ) {
        Active& a = active_[i];
        if (a.world != &world) { ++i; continue; }

        // Re-resolve entity from handle. Archetype migrations can move it,
        // and the entity may have died without onEntityDestroyed firing.
        Entity* e = world.isAlive(a.handle) ? world.getEntity(a.handle) : nullptr;
        if (!e) {
            destroyOne(i);
            continue;  // index unchanged — destroyOne erased the row
        }

        BehaviorComponent* bc = e->getComponent<BehaviorComponent>();
        if (!bc) {
            // Component removed via a path that didn't fire the remove hook.
            destroyOne(i);
            continue;
        }

        // Replicated ghosts are server-authoritative. Tear down + warn-once
        // if a previously non-replicated entity flipped to replicated.
        if (e->isReplicated()) {
            bool warned = false;
            for (const auto& h : replicatedWarnedHandles_) {
                if (h == a.handle) { warned = true; break; }
            }
            if (!warned) {
                LOG_WARN("HotReload",
                    "Entity %u has BehaviorComponent '%s' but isReplicated()==true; skipping (server-authoritative).",
                    (unsigned)e->id(), bc->behavior.c_str());
                replicatedWarnedHandles_.push_back(a.handle);
            }
            destroyOne(i);
            continue;
        }

        // Disabled / unset — keep the roster entry alive (re-enabling
        // shouldn't require re-add hook) but skip dispatch.
        if (!bc->enabled || bc->behavior.empty()) {
            // If we had a bound vtable and the entity transitioned into
            // disabled/empty state, fire onDestroy so module scratch is
            // released. The entry stays so re-enabling is a clean path.
            if (a.vtable) {
                FateBehaviorCtx ctx{&world, e, bc, bc->state};
                if (a.vtable->onDestroy) {
                    HrFaultInfo fi;
                    hrCallVoid(a.vtable->onDestroy, &ctx, &fi);
                    if (fi.caught != 0) {
                        LOG_ERROR("HotReload",
                            "entity=%u behavior='%s' onDestroy faulted on disable — continuing",
                            (unsigned)e->id(), a.behaviorName.c_str());
                    }
                }
                bc->state = nullptr;
                bc->runtimeFields = nlohmann::json::object();
                a.vtable = nullptr;
                a.behaviorName.clear();
                a.cachedState = nullptr;
                bc->boundGeneration = 0;
            }
            ++i;
            continue;
        }

        // P4 (S153): quarantined instance from a prior fault — skip until
        // editor "Re-arm" clears the flag.
        if (a.faulted) {
            ++i;
            continue;
        }

        a.component = bc;  // refresh — archetype migration may have moved it

        // Lazy resolve / rebind. Two triggers:
        //   - vtable is null (first-sight or registry didn't have the name
        //     at last attempt; the registration may have arrived since).
        //   - behavior name changed since last bind (inspector edit, gameplay
        //     swap).
        if (a.vtable == nullptr || a.behaviorName != bc->behavior) {
            // Tear down old binding first if there was one.
            if (a.vtable && a.vtable->onDestroy) {
                FateBehaviorCtx oldCtx{&world, e, bc, bc->state};
                HrFaultInfo fi;
                hrCallVoid(a.vtable->onDestroy, &oldCtx, &fi);
                if (fi.caught != 0) {
                    LOG_ERROR("HotReload",
                        "entity=%u behavior='%s' onDestroy faulted during rebind — continuing",
                        (unsigned)e->id(), a.behaviorName.c_str());
                }
                bc->state = nullptr;
                bc->runtimeFields = nlohmann::json::object();
            }

            const FateBehaviorVTable* vt = reg.find(bc->behavior);
            a.vtable       = vt;
            a.behaviorName = bc->behavior;
            if (!vt) {
                // No registration for this name yet. Stay tracked; future
                // registration or rename will pick us up.
                bc->boundGeneration = 0;
                ++i;
                continue;
            }
            // Bind: fire onStart for the new vtable.
            FateBehaviorCtx newCtx{&world, e, bc, bc->state};
            if (vt->onStart) {
                HrFaultInfo fi;
                hrCallVoid(vt->onStart, &newCtx, &fi);
                if (fi.caught != 0) {
                    quarantineActive(a, e, "onStart", fi);
                    ++i;
                    continue;  // skip onUpdate this tick; quarantined
                }
            }
            a.cachedState = bc->state;
            bc->boundGeneration = gen;
        }

        // Dispatch onUpdate.
        if (a.vtable && a.vtable->onUpdate) {
            FateBehaviorCtx ctx{&world, e, bc, bc->state};
            HrFaultInfo fi;
            hrCallUpdate(a.vtable->onUpdate, &ctx, dt, &fi);
            if (fi.caught != 0) {
                quarantineActive(a, e, "onUpdate", fi);
                ++i;
                continue;
            }
            a.cachedState = bc->state;  // may have been mutated via setState
        }

        ++i;
    }

    // Safety-net invariant validation. Cheap (one pass per second-ish at
    // 60 FPS), editor/non-shipping only by virtue of FATE_ENABLE_HOT_RELOAD
    // being off in shipping. Catches any code path that bypassed the bind
    // hook and would otherwise produce silent "behavior not running"
    // mysteries.
    if (++frameCounterForSafetyNet_ >= kSafetyNetTickInterval) {
        frameCounterForSafetyNet_ = 0;
        runSafetyNetSweep(world);
    }
}

void HotReloadManager::runSafetyNetSweep(World& world) {
    // Walk every entity once; for any with a non-replicated, enabled
    // BehaviorComponent that's NOT in active_, warn-once and recover via
    // onBehaviorComponentAdded so the next tick lazy-binds it.
    world.forEachEntity([&](Entity* e) {
        if (e->isReplicated()) return;
        BehaviorComponent* bc = e->getComponent<BehaviorComponent>();
        if (!bc) return;
        // We track even disabled/empty BehaviorComponents in active_ so
        // that toggling enabled or filling in a name later picks up
        // dispatch without re-firing the bind hook. The invariant is
        // simply "every BehaviorComponent has a roster entry".
        EntityHandle h = e->handle();
        if (findActive(&world, h) >= 0) return;

        bool warned = false;
        for (const auto& wh : missedBindWarnedHandles_) {
            if (wh == h) { warned = true; break; }
        }
        if (!warned) {
            LOG_WARN("HotReload",
                "Safety-net sweep recovered missed bind hook: entity %u has BehaviorComponent '%s' "
                "but no roster entry. Some component-mutation path bypassed "
                "hotReloadNotifyBehaviorComponentAdded — investigate.",
                (unsigned)e->id(), bc->behavior.c_str());
            missedBindWarnedHandles_.push_back(h);
        }
        // Recover so the entity keeps working. Next tick lazy-binds.
        onBehaviorComponentAdded(world, h);
    });
}

} // namespace fate
