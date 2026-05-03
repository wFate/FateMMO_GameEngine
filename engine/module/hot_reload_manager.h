#pragma once
// engine/module/hot_reload_manager.h
//
// Owns the live FateGameRuntime.dll handle, drives shadow-copy + LoadLibrary,
// validates ABI/protocol versions, and dispatches per-frame behavior callbacks.
//
// Failure principle: every reload is non-destructive. A failed compile, a
// missing export, an ABI mismatch, or a copy_file collision must leave the
// previously-loaded module fully active. All failure paths log the cause via
// `lastError()` and bump `failureCount()` so the editor can surface it.

#include "engine/module/fate_module_abi.h"
#include "engine/module/behavior_registry.h"
#include "engine/asset/file_watcher.h"
#include "engine/ecs/entity_handle.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fate {

class World;
struct BehaviorComponent;

class HotReloadManager {
public:
    static HotReloadManager& instance();

    HotReloadManager(const HotReloadManager&) = delete;
    HotReloadManager& operator=(const HotReloadManager&) = delete;

    // Boot. Looks for `<moduleDir>/<moduleName>.dll` and shadow-loads it.
    // Returns false if the artifact isn't present — that is not an error;
    // the engine simply runs without a game module (e.g. demo build).
    // Starts the file watcher for subsequent edit-rebuild cycles.
    bool initialize(const std::string& moduleDir, const std::string& moduleName = "FateGameRuntime");

    // Optional: also watch a source directory (e.g. <repo>/game/runtime) and
    // run a build command on debounced source change. The build's resulting
    // DLL drop is what the artifact watcher then picks up to trigger the
    // actual reload. `buildCmd` is invoked with system() on a worker thread;
    // typical value: `cmake --build <binary_dir> --target FateGameRuntime`.
    // If `sourceDir` or `buildCmd` is empty, the source-side watcher is not
    // started — the developer keeps the manual rebuild flow.
    void enableSourceWatch(const std::string& sourceDir, const std::string& buildCmd);

    // Build status surfacer for the editor panel.
    enum class BuildStatus { Idle, Running, Succeeded, Failed };
    BuildStatus buildStatus()  const { return buildStatus_.load(std::memory_order_acquire); }
    int         buildExitCode() const { return buildExitCode_.load(std::memory_order_acquire); }
    const std::string& buildCommand() const { return buildCmd_; }
    // Snapshot of the worker's last-flushed log tail. Returns by value so the
    // caller is never holding a reference into a string the worker thread
    // could be reallocating mid-frame. Cheap (small buffer, ~4 KB max).
    std::string buildLogTailSnapshot() const;

    // Stops watcher, calls fateGameModuleShutdown, FreeLibrary's both the
    // current and any deferred-previous handle. Idempotent.
    void shutdown();

    // Drains a pending reload at a safe frame boundary. Call once near the top
    // of App::update — outside ECS iteration, before any system tick or
    // network dispatch. No-op when no reload is pending or the debounce
    // window has not elapsed.
    void processPendingReload(float currentTime);

    // Per-frame fan-out: walks the world, finds BehaviorComponent on every
    // entity, calls onUpdate (and onStart on first dispatch after a reload).
    // Game-side update paths call this once per frame against their World.
    //
    // Skips entities whose Entity::isReplicated() is true. Replicated ghosts
    // are server-authoritative; client-side behaviors must not mutate them.
    // Per-frame log throttled to once per offending entity per session.
    void tickBehaviors(World& world, float dt);

    // Lifecycle hook: call BEFORE destroying a World (scene unload, editor
    // exitPlayMode wave, manager shutdown). Walks the roster and fires
    // onDestroy for every entry whose world matches; clears their state.
    // Idempotent.
    void onWorldUnload(World& world);

    // Lifecycle hook: call when a single entity is about to be destroyed
    // outside of a full world unload (e.g. entity-by-entity destroy queue).
    // Optional — the per-frame sweep in tickBehaviors will catch it on the
    // next tick — but explicit notification means onDestroy fires BEFORE
    // the entity's archetype storage gets reorganized.
    void onEntityDestroyed(World& world, EntityHandle handle);

    // Lifecycle hook: call when BehaviorComponent is about to be removed
    // from an entity (or when behavior name was just edited and the old
    // binding needs flushing). Caller is responsible for actually removing
    // the component / mutating the name afterwards.
    void onBehaviorComponentRemoved(World& world, EntityHandle handle);

    // Manual reload trigger (editor menu / hotkey). Sets the pending flag with
    // the current time so processPendingReload's debounce treats it as a fresh
    // edit. Reason is for diagnostics.
    void requestManualReload(const char* reason);

    // ---- Diagnostics --------------------------------------------------------
    bool        isModuleLoaded()  const { return currentHandle_ != nullptr; }
    const std::string& moduleName()    const { return moduleNameStr_; }
    const std::string& moduleBuildId() const { return moduleBuildIdStr_; }
    const std::string& sourcePath()    const { return sourcePath_; }
    const std::string& lastError()     const { return lastError_; }
    uint32_t    reloadCount()  const { return reloadCount_; }
    uint32_t    failureCount() const { return failureCount_; }

    // Play-mode safety knob. Default OFF — play-mode reload is unsafe until
    // every play-mode subsystem (combat, network packet dispatch on game
    // thread, AOI iteration) is proven quiesced at the safe-frame point.
    // The mutator is compile-gated: only available when the engine is
    // built with FATE_HOTRELOAD_EXPERIMENTAL_PLAYMODE=1. The editor UI is
    // read-only and surfaces the current state for diagnostics.
    bool playModeReloadAllowed() const { return allowPlayModeReload_; }
#if defined(FATE_HOTRELOAD_EXPERIMENTAL_PLAYMODE) && FATE_HOTRELOAD_EXPERIMENTAL_PLAYMODE
    void setPlayModeReloadAllowed(bool v) { allowPlayModeReload_ = v; }
#endif

private:
    HotReloadManager() = default;
    ~HotReloadManager() { shutdown(); }

    // Performs the full swap pipeline. Returns true on success.
    bool performSwap(float currentTime);

    // Internal: builds the host-side FateHostApi vtable (idempotent).
    void ensureHostApi();

    // Internal: walks the roster, calls onDestroy on every active behavior
    // via its CACHED old vtable, clears BehaviorComponent::state. Frees
    // module-owned scratch before the old DLL is superseded. Roster is
    // emptied after.
    void teardownActiveBehaviors();

    // Watcher callback. Runs on the watcher thread; only sets atomics.
    void onWatchEvent(const std::string& relPath);

    // ---- Active-behavior roster --------------------------------------------
    // Single source of truth for "what currently has a vtable bound and may
    // own module-side scratch via state". One entry per (World, EntityHandle)
    // pair. Maintained by tickBehaviors — adds on first sight, removes on
    // sweep (entity dead / component gone / behavior name changed) or on
    // explicit lifecycle hooks (world unload, entity destroy).
    struct Active {
        World*                    world         = nullptr;
        EntityHandle              handle        = {};
        BehaviorComponent*        component     = nullptr;       // re-resolved each tick; do not trust across frames
        const FateBehaviorVTable* vtable        = nullptr;       // cached at bind time; nulled on reload
        std::string               behaviorName;                  // last-bound name (detect runtime rename)
        void*                     cachedState   = nullptr;       // mirror of bc->state, survives component destruction
        bool                      seenThisTick  = false;         // sweep flag
    };
    std::vector<Active> active_;

    // Linear scan; roster is small (active behaviors only). If this becomes
    // hot at MMO scale, replace with a hashmap keyed on EntityHandle.
    int findActive(World* world, EntityHandle handle) const;

    // Fires onDestroy + clears state for one entry, then erases at idx.
    // Caller owns the vtable validity check window.
    void destroyOne(int idx);

    // Throttle bookkeeping for "BehaviorComponent on a replicated entity"
    // log lines. Cleared on shutdown so a fresh session re-warns.
    std::vector<EntityHandle> replicatedWarnedHandles_;

#ifdef _WIN32
    void* currentHandle_  = nullptr;   // HMODULE; current live module
    void* previousHandle_ = nullptr;   // HMODULE; freed on next reload
#else
    // Non-Windows: not implemented in this slice. Members remain null.
    void* currentHandle_  = nullptr;
    void* previousHandle_ = nullptr;
#endif

    std::string moduleDir_;            // <exe_dir>
    std::string moduleNameBase_;       // "FateGameRuntime"
    std::string sourcePath_;           // <exe_dir>/FateGameRuntime.dll
    std::string shadowDir_;            // <exe_dir>/fate_module_shadow
    std::string activeShadowPath_;     // currently-loaded shadow dll
    uint32_t    shadowCounter_ = 0;

    FateGameModuleApi moduleApi_{};
    std::string       moduleNameStr_;
    std::string       moduleBuildIdStr_;

    // Host-side vtable handed to fateGameModuleInit. Stable address for the
    // lifetime of the manager; module copies pointers into its own statics.
    FateHostApi hostApi_{};
    bool        hostApiInitialized_ = false;

    // Reload bookkeeping. reloadPending_ is set by the watcher thread; the
    // timestamp is read+written on the main thread once the flag is observed.
    std::atomic<bool> reloadPending_{false};
    float reloadRequestedAt_ = -1.0f;
    static constexpr float kReloadDebounce = 1.0f;  // seconds — collapse build write bursts

    bool     allowPlayModeReload_ = false;
    bool     playModeWarned_      = false;  // throttle the "deferred" log
    bool     transientWarned_     = false;  // throttle "build still in progress" log
    int      transientRetries_    = 0;      // consecutive transient swap aborts (DLL missing / mid-link)
    static constexpr int kMaxTransientRetries = 30;  // ~30s at 1s debounce; then escalate
    uint32_t reloadCount_  = 0;
    uint32_t failureCount_ = 0;

    std::string lastError_;
    FileWatcher watcher_;          // watches the DLL artifact directory
    FileWatcher sourceWatcher_;    // watches game/runtime source dir (optional)

    // Source-side rebuild bookkeeping.
    std::string sourceDir_;
    std::string buildCmd_;
    std::atomic<bool>  buildPending_{false};
    float              buildRequestedAt_ = -1.0f;
    static constexpr float kBuildDebounce = 0.4f;  // collapse editor-save bursts
    std::atomic<BuildStatus> buildStatus_{BuildStatus::Idle};
    std::atomic<int>         buildExitCode_{0};
    // Build worker writes buildLogTail_ under buildLogMutex_; the editor
    // panel reads via buildLogTailSnapshot() which copies under the same
    // lock. Direct access from outside the manager is forbidden — pre-lock
    // mutex builds had a UB race when ImGui read a string the worker was
    // reallocating mid-frame.
    mutable std::mutex       buildLogMutex_;
    std::string              buildLogTail_;
    std::thread              buildThread_;

    void onSourceWatchEvent(const std::string& relPath);
    void runBuildAsync();
    void joinBuildThread();
};

} // namespace fate
