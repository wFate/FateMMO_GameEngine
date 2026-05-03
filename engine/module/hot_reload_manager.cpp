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
#include <fstream>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace fate {

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

} // anonymous namespace

// ---------------------------------------------------------------------------
HotReloadManager& HotReloadManager::instance() {
    static HotReloadManager s_instance;
    return s_instance;
}

#if FATE_ENABLE_HOT_RELOAD
// Forwarding shim used by engine/ecs/entity_inline.h's removeComponent
// specialization. Lives here so the entity inline doesn't need to pull the
// full HotReloadManager header into world.h's include graph.
void hotReloadNotifyBehaviorComponentRemoved(World& world, EntityHandle handle) {
    HotReloadManager::instance().onBehaviorComponentRemoved(world, handle);
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
        if (shutdownFn) shutdownFn();
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
}

void HotReloadManager::requestManualReload(const char* reason) {
    LOG_INFO("HotReload", "Manual reload requested: %s", reason ? reason : "(no reason)");
    reloadPending_.store(true, std::memory_order_release);
    reloadRequestedAt_ = -1.0f;  // re-stamp on next process call so debounce starts now
}

void HotReloadManager::processPendingReload(float currentTime) {
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
        ++failureCount_;
        LOG_WARN("HotReload", "Reload failed: %s (current module preserved)", lastError_.c_str());
    }
}

bool HotReloadManager::performSwap(float /*currentTime*/) {
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
        lastError_ = "Source DLL missing: " + sourcePath_;
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
    int initOk = initFn(&hostApi_, &newApi);
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
            int beginOk = oldBegin(&rctx);
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
        // nulls BehaviorComponent::state. MUST run before commitStaging or
        // the cached vtable pointers become unreachable.
        teardownActiveBehaviors();

        // Old module gets a chance to fully shut down its statics. Note that
        // FreeLibrary on currentHandle_ is deferred by one swap (parked in
        // previousHandle_) so any in-flight stack frames still in old code
        // when this returns can finish unwinding.
        auto oldShutdown = (void(*)())GetProcAddress((HMODULE)currentHandle_, FATE_SYM_SHUTDOWN);
        if (oldShutdown) oldShutdown();
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
    int endOk = endFn(&endCtx);
    if (!endOk) {
        // EndReload failure means the new module is live but its post-swap
        // setup didn't complete. We don't roll back — the old module is
        // already shut down — but we mark the manager as degraded so the
        // editor can surface it.
        lastError_  = "fateGameModuleEndReload returned 0 (module live but degraded)";
        ++failureCount_;
        LOG_WARN("HotReload", "%s", lastError_.c_str());
    } else {
        lastError_.clear();
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
        a.vtable->onDestroy(&ctx);
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
    // Walk in reverse so erase indices stay valid.
    while (!active_.empty()) {
        destroyOne((int)active_.size() - 1);
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

// ---------------------------------------------------------------------------
// Per-frame dispatch.
//
// Algorithm:
//   1. Mark every roster entry seenThisTick=false.
//   2. Walk world entities. For each non-replicated entity with an enabled,
//      named BehaviorComponent:
//        a. Find or create roster entry. On create, run onStart.
//        b. If behavior name changed since last frame, fire onDestroy on
//           old vtable, clear runtime state, rebind to new vtable + onStart.
//        c. Mark seenThisTick=true.
//        d. Dispatch onUpdate via cached vtable.
//   3. Sweep any roster entries with seenThisTick=false — entity destroyed,
//      component removed, behavior disabled, or entity now replicated.
//      Fire onDestroy and erase.
// ---------------------------------------------------------------------------
void HotReloadManager::tickBehaviors(World& world, float dt) {
    // Module-global tick (optional).
    if (moduleApi_.tick) {
        moduleApi_.tick(dt);
    }

    auto& reg = BehaviorRegistry::instance();
    const uint32_t gen = reg.generation();

    // 1. Reset sweep flags for entries belonging to this world.
    for (Active& a : active_) {
        if (a.world == &world) a.seenThisTick = false;
    }

    // 2. Walk entities.
    world.forEachEntity([&](Entity* e) {
        // Replicated ghosts are server-authoritative. Behaviors must NEVER
        // mutate them — the server has the truth and would clobber any
        // client-side write on the next replication tick. If the entity
        // was previously bound (had a non-replicated phase, then replication
        // took over), tear it down. Either way, log once per offending
        // entity per process so a scene full of replicated mobs with stray
        // BehaviorComponents doesn't spam.
        if (e->isReplicated()) {
            BehaviorComponent* bcCheck = e->getComponent<BehaviorComponent>();
            if (bcCheck && bcCheck->enabled && !bcCheck->behavior.empty()) {
                int existing = findActive(&world, e->handle());
                if (existing >= 0) destroyOne(existing);

                bool warned = false;
                for (const auto& h : replicatedWarnedHandles_) {
                    if (h == e->handle()) { warned = true; break; }
                }
                if (!warned) {
                    LOG_WARN("HotReload",
                        "Entity %u has BehaviorComponent '%s' but isReplicated()==true; skipping (server-authoritative).",
                        (unsigned)e->id(), bcCheck->behavior.c_str());
                    replicatedWarnedHandles_.push_back(e->handle());
                }
            }
            return;
        }

        BehaviorComponent* bc = e->getComponent<BehaviorComponent>();
        if (!bc || !bc->enabled || bc->behavior.empty()) {
            // Entity used to dispatch but no longer should. Sweep handles it.
            return;
        }

        int idx = findActive(&world, e->handle());
        if (idx < 0) {
            // First sight — bind. If no registration matches the name, do
            // not add to roster (nothing to dispatch, nothing to tear down).
            const FateBehaviorVTable* vt = reg.find(bc->behavior);
            if (!vt) return;

            Active a;
            a.world         = &world;
            a.handle        = e->handle();
            a.component     = bc;
            a.vtable        = vt;
            a.behaviorName  = bc->behavior;
            a.cachedState   = bc->state;
            a.seenThisTick  = true;
            active_.push_back(std::move(a));

            FateBehaviorCtx ctx{&world, e, bc, bc->state};
            if (vt->onStart) vt->onStart(&ctx);
            // Module may have set state during onStart — pull it back so the
            // roster sees the latest value before the same-tick onUpdate.
            active_.back().cachedState = bc->state;
            bc->boundGeneration = gen;
            // Fall through to onUpdate so a freshly-bound behavior also runs
            // its first tick this frame (Unity-equivalent semantics).
            idx = (int)active_.size() - 1;
        }

        Active& a = active_[idx];
        a.component    = bc;          // refresh — archetype migration may have moved it
        a.seenThisTick = true;

        // Behavior name changed at runtime (designer edited inspector, or a
        // gameplay event swapped it). Tear down old, bind new.
        if (a.behaviorName != bc->behavior) {
            const FateBehaviorVTable* vt = reg.find(bc->behavior);
            if (!vt) {
                // No registration for the new name. Drop the active entry
                // entirely so a later reload that DOES register this name
                // takes the first-sight bind path. destroyOne fires old
                // onDestroy + clears bc->state + bc->runtimeFields, then
                // erases the row — single point of cleanup, no double-fire.
                bc->boundGeneration = 0;
                destroyOne(idx);
                return;  // skip dispatch this tick; next tick rebinds fresh
            }
            // Renaming to a registered name. Fire OLD vtable's onDestroy
            // directly (we still have a.vtable pointing at it), clear
            // scratch, swap, fire new onStart.
            FateBehaviorCtx oldCtx{&world, e, bc, bc->state};
            if (a.vtable && a.vtable->onDestroy) a.vtable->onDestroy(&oldCtx);
            bc->state = nullptr;
            bc->runtimeFields = nlohmann::json::object();

            a.vtable       = vt;
            a.behaviorName = bc->behavior;
            FateBehaviorCtx newCtx{&world, e, bc, bc->state};
            if (vt->onStart) vt->onStart(&newCtx);
            a.cachedState  = bc->state;
            bc->boundGeneration = gen;
        }

        // Dispatch.
        if (a.vtable && a.vtable->onUpdate) {
            FateBehaviorCtx ctx{&world, e, bc, bc->state};
            a.vtable->onUpdate(&ctx, dt);
            // Module may have called setState — refresh the cache.
            a.cachedState = bc->state;
        }
    });

    // 3. Sweep — anything not seen this tick (in this world) is dead.
    for (int i = (int)active_.size() - 1; i >= 0; --i) {
        if (active_[i].world == &world && !active_[i].seenThisTick) {
            destroyOne(i);
        }
    }
}

} // namespace fate
