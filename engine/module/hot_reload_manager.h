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
#include "engine/module/behavior_migration.h"
#include "engine/asset/file_watcher.h"
#include "engine/ecs/entity_handle.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fate {

class World;
struct BehaviorComponent;
// Defined in hot_reload_manager.cpp (TU-internal P4 fault info POD).
// Forward-declared here so the private setModuleDegraded() member can take
// it by const-ref without exposing the type publicly.
struct HrFaultInfo;

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

    // Status-emission event categories. One event-type per writeStatusFile()
    // call site. The script reads `last_event_type` from logs/hot_reload_status.json
    // to classify outcomes by event boundary (not by snapshot of reload_count
    // alone, which is ambiguous at build-end).
    enum class EventType {
        BuildStarted,                   // runBuildAsync entered
        BuildSucceededArtifactChanged,  // build OK, DLL on disk differs from pre-build snapshot
        BuildSucceededNoArtifactChange, // build OK, DLL identical (cmake no-op or comment-only edit)
        BuildFailed,                    // build returned non-zero exit code
        BuildRequested,                 // requestBuildNow accepted (queued or kicked)
        BuildSkippedLocked,             // build dispatcher skipped runBuildAsync because logs/.script_build.lock present
        ReloadDeferredPlayMode,         // processPendingReload saw play-mode gate; reload still pending
        ReloadSucceeded,                // performSwap returned true; reload_count advanced
        ReloadFailedTransient,          // processPendingReload gave up after kMaxTransientRetries
        ReloadFailedHard,               // performSwap returned false, non-transient
        ModuleDegraded                  // module_degraded_ flipped to true
    };

    BuildStatus buildStatus()  const { return buildStatus_.load(std::memory_order_acquire); }
    int         buildExitCode() const { return buildExitCode_.load(std::memory_order_acquire); }
    const std::string& buildCommand() const { return buildCmd_; }
    // Snapshot of the worker's last-flushed log tail. Returns by value so the
    // caller is never holding a reference into a string the worker thread
    // could be reallocating mid-frame. Cheap (small buffer, ~4 KB max).
    std::string buildLogTailSnapshot() const;

    // Source-watch diagnostics for the Hot Reload panel. sourceWatchEnabled
    // means enableSourceWatch() received a non-empty path + command; the
    // watcher thread is up and edits will trigger builds. sourceWatchPath
    // is the watched directory (typically <repo>/game/runtime).
    bool sourceWatchEnabled() const { return !sourceDir_.empty() && !buildCmd_.empty(); }
    const std::string& sourceWatchPath() const { return sourceDir_; }

    // Reload-queue diagnostics. isReloadPending: a reload is queued (either
    // from a successful build, an artifact-watcher event, or a manual
    // request) but processPendingReload has not committed it yet — usually
    // because debounce hasn't elapsed or play-mode is gating it.
    // isReloadDeferredByPlayMode: same flag is set AND the editor is in
    // play mode AND playModeReloadAllowed() is false. Surfaced so the
    // panel can say "queued until play mode exits" instead of looking
    // silently broken.
    bool isReloadPending() const { return reloadPending_.load(std::memory_order_acquire); }
    bool isReloadDeferredByPlayMode() const;

    // Manual build trigger. Editor "Build Runtime Now" button calls this
    // when the developer wants to rebuild without modifying a source file
    // (e.g., to verify a clean rebuild after a CMake change). No-op when
    // source-watch is not configured or a build is already running.
    // Reason is logged for diagnostics.
    void requestBuildNow(const char* reason);

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

    // Lifecycle hook: call AFTER a BehaviorComponent is constructed on an
    // entity. Adds a deferred-bind row to the roster (vtable=nullptr).
    // The next tick lazy-resolves the vtable from the registry once
    // bc->behavior is set. Idempotent — duplicate calls are no-ops.
    // P2 (S153): the dense-roster dispatch loop relies on this hook firing
    // for every BehaviorComponent. Missed hooks are caught by the safety-
    // net sweep (loud warn, recovery), but the contract is "always fire".
    void onBehaviorComponentAdded(World& world, EntityHandle handle);

    // Optional rebind hint: call when bc->behavior or bc->enabled was
    // edited at runtime (inspector, gameplay swap). The next tick already
    // auto-detects via behaviorName/bc->behavior comparison, so this is
    // strictly a no-latency path for callers that want immediate rebind
    // semantics. Equivalent to "next tick will rebind" today.
    void notifyBehaviorRebind(World& world, EntityHandle handle);

    // Type-erased component-added forwarder used by World::addComponentById
    // (the deserialization / prefab / scene-load path). World does not
    // include behavior_component.h, so it dispatches through here; the
    // manager checks the CompId and routes BehaviorComponent additions to
    // onBehaviorComponentAdded. Other CompIds are no-ops.
    void onComponentAddedNotification(World& world, EntityHandle handle, uint32_t compId);

    // Reload-cycle teardown: fires onDestroy on every active entry's CACHED
    // vtable (so module-owned scratch is freed BEFORE the old DLL is
    // unloaded), clears bc->state + bc->runtimeFields + boundGeneration,
    // nulls a.vtable, but KEEPS the roster entry. The next tickBehaviors
    // call's lazy-resolve picks the new vtable up from the post-commit
    // registry. Used by performSwap and exposed publicly so tests can
    // simulate module reload without going through the full DLL pipeline.
    //
    // Distinct from onWorldUnload (which destroys roster entries because
    // the World itself is going away) and from shutdown's
    // teardownActiveBehaviors (which drains the roster entirely).
    void teardownActiveBindings();

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

    // P4 (S153) fault diagnostics.
    bool        moduleDegraded() const { return moduleDegraded_; }
    const std::string& moduleDegradedReason() const { return moduleDegradedReason_; }

    // P3 (S153) safe-point contract. True only during the narrow window
    // where processPendingReload is actively running on the main thread.
    // performSwap refuses to run when this is false — catches any future
    // code path that tries to drive a structural swap from inside a system
    // tick, network handler, render callback, or worker thread.
    bool        inSafePoint() const { return inSafePoint_; }

    // SEH-+ C++-guarded copy-out of a behavior's optional describeFields()
    // schema. Walks every descriptor under SEH, validates the type enum,
    // probes name/tooltip as bounded NUL-terminated strings, and copies
    // all primitive + string fields into host-owned storage. Inspector
    // iterates SafeBehaviorField entries exclusively — never touches
    // module-owned memory after this call.
    //
    // Returns nullptr on fault, malformed schema, or legitimate "no
    // schema". Faults flag the module degraded with a deduped log.
    // Inspector callers should fall back to the freeform JSON drawer
    // when this returns nullptr.
    //
    // Lifetime: the returned pointer is owned by the manager and stable
    // until the next safeDescribeFields call OR the next module reload,
    // whichever comes first.
    struct SafeBehaviorField {
        std::string   name;         // copied + bounded; never empty for valid entries
        FateFieldType type;         // validated enum
        float         defaultF;
        int32_t       defaultI;
        int           defaultB;     // 0 or 1
        float         minF;
        float         maxF;
        int32_t       minI;
        int32_t       maxI;
        std::string   tooltip;      // copied + bounded; empty when absent
    };
    struct SafeBehaviorSchema {
        std::vector<SafeBehaviorField> fields;
    };
    const SafeBehaviorSchema* safeDescribeFields(const FateBehaviorVTable* vt);

    // Cap to reject obviously-uninitialized field counts. Real schemas
    // carry a handful of fields (ExampleIdle has 2). Anything past this
    // is rejected as a malformed header.
    static constexpr uint32_t kMaxSchemaFields = 256;
    // Cap on probed C-string length (name, tooltip). Anything longer is
    // treated as malformed (likely missing NUL or a wild pointer that
    // happens to point at readable memory). Keeps the bounded-strlen
    // probe O(1) under SEH.
    static constexpr size_t   kMaxSchemaStringLen = 256;
    // Snapshot of currently-quarantined behavior instances. Each pair is
    // (entity-id-as-uint, behavior-name + fault detail string). Used by
    // the editor Hot Reload panel to render a "Faulted" subsection. The
    // returned vector is a copy — cheap (faults are rare, and the caller
    // would otherwise need a mutex against in-flight fault flagging).
    struct FaultedRow {
        uint32_t    entityId;
        std::string behaviorName;
        std::string detail;
    };
    std::vector<FaultedRow> faultedBehaviors() const;
    // Editor "Re-arm" action: clears every Active::faulted flag so the
    // next tick re-attempts dispatch on previously-quarantined instances.
    // Module-degraded flag is cleared by a successful subsequent reload.
    void clearAllFaults();

    // Dirty-seam callback. Fired from applyMigrations when a schema
    // diff or module migrate() callback CHANGED a behavior's authored
    // payload (rules 2/3 or a non-empty before/after diff in Tier 2).
    // The host's own pure save state stays in HotReloadManager; the
    // editor wires this to Editor::markSceneDirty so reload-induced
    // payload changes round-trip through the existing Ctrl+S flow.
    // Reason is a short human-readable tag used in editor logging.
    //
    // Default callback is null — no editor coupling unless explicitly
    // wired. This is the one decoupling concession the user demanded
    // when 7.3a was scoped: HotReloadManager does NOT include
    // engine/editor/editor.h.
    using BehaviorAuthoredDataChangedCallback =
        std::function<void(World*, EntityHandle, const std::string& reason)>;
    void setBehaviorAuthoredDataChangedCallback(BehaviorAuthoredDataChangedCallback cb) {
        authoredDataChangedCb_ = std::move(cb);
    }
    // Module-degraded clear (manual, for cases where the developer has
    // verified the underlying issue is fixed). A successful reload also
    // clears it automatically.
    void clearModuleDegraded();

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

    // Single source of truth for "what currently has a vtable bound and may
    // own module-side scratch via state". One entry per (World, EntityHandle)
    // pair. Public so the cpp's translation unit (and tests) can inspect /
    // mutate via friends-equivalent free functions in P4 fault containment;
    // direct mutation outside the manager is not supported.
    struct Active {
        World*                    world         = nullptr;
        EntityHandle              handle        = {};
        BehaviorComponent*        component     = nullptr;
        const FateBehaviorVTable* vtable        = nullptr;
        std::string               behaviorName;
        void*                     cachedState   = nullptr;
        bool                      seenThisTick  = false;
        bool                      faulted       = false;
        std::string               faultMessage;

        // Migration capture (S163). Populated by
        // captureOldSchemasForMigration() right before a reload's
        // teardownActiveBindings runs, consumed by applyMigrations()
        // after the new module's EndReload returns. Cleared at the
        // end of applyMigrations.
        //
        // `migrationOldFields` is the snapshot of the OUTGOING
        // behavior's schema at swap time (copied out of the SEH-
        // probed cache so the new safeDescribeFields call doesn't
        // clobber it). `migrationOldProtocolVersion` is the
        // bc->payloadProtocolVersion observed at capture time —
        // routed through the module's migrate() callback as the
        // `fromVersion` argument when it differs from the running
        // module's FATE_MODULE_PROTOCOL_VERSION.
        std::vector<MigrationField> migrationOldFields;
        uint32_t                    migrationOldProtocolVersion = 1;
    };

private:
    HotReloadManager() = default;
    ~HotReloadManager() { shutdown(); }

    // Performs the full swap pipeline. Returns true on success.
    bool performSwap(float currentTime);

    // Capture every Active entry's outgoing schema + payload protocol
    // version BEFORE teardownActiveBindings nulls the cached vtable.
    // Stores into Active::migrationOldFields /
    // ::migrationOldProtocolVersion for applyMigrations to consume.
    // Idempotent — safe to call from anywhere in performSwap before
    // teardown runs.
    void captureOldSchemasForMigration();

    // Walks every Active entry AFTER fateGameModuleEndReload returns
    // (the module is now both live and post-init), runs the four
    // schema-evolution rules from behavior_migration.h, fires the
    // optional vtable->migrate callback when the payload protocol
    // version is older than the module's current version, stamps the
    // new protocol on the component, and notifies
    // authoredDataChangedCb_ when authored fields actually changed.
    // Clears Active::migrationOldFields after each entry is handled.
    void applyMigrations();

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
    // Maintained by tickBehaviors / lifecycle hooks. One entry per (World,
    // EntityHandle) pair carrying a BehaviorComponent. The struct is public
    // (above) so P4 fault helpers in hot_reload_manager.cpp's anonymous
    // namespace can mutate Active::faulted + Active::faultMessage. Outside
    // the manager: read-only via faultedBehaviors() snapshots.
    std::vector<Active> active_;

    // Linear scan; roster is small (active behaviors only). If this becomes
    // hot at MMO scale, replace with a hashmap keyed on EntityHandle.
    int findActive(World* world, EntityHandle handle) const;

    // Fires onDestroy + clears state for one entry, then erases at idx.
    // Caller owns the vtable validity check window.
    void destroyOne(int idx);

    // P2 (S153) safety-net sweep. Runs from inside tickBehaviors at a
    // throttled cadence. Walks `world` once and asserts every entity
    // carrying an enabled, non-replicated BehaviorComponent is in active_.
    // Recovers + warn-once on any stragglers (catches missed bind hooks).
    void runSafetyNetSweep(World& world);

    // Throttle bookkeeping for "BehaviorComponent on a replicated entity"
    // log lines. Cleared on shutdown so a fresh session re-warns.
    std::vector<EntityHandle> replicatedWarnedHandles_;

    // Throttle bookkeeping for "missed bind hook" warnings emitted by the
    // dev-only safety-net sweep when it finds an entity carrying a
    // BehaviorComponent that's not in active_. We warn once per offending
    // handle to keep dev logs informative without spamming.
    std::vector<EntityHandle> missedBindWarnedHandles_;

    // Safety-net sweep cadence. Pure-event-driven dispatch trusts that
    // every component-mutation path fires the bind hook. The sweep is
    // invariant validation: every kSafetyNetTickInterval frames, do a
    // single world.forEachEntity scan and flag any BehaviorComponent
    // that's not in active_. Cheap (one walk per second-ish at 60 FPS),
    // editor/non-shipping only via FATE_ENABLE_HOT_RELOAD already-set.
    int frameCounterForSafetyNet_ = 0;
    static constexpr int kSafetyNetTickInterval = 60;

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

    // Host-owned schema cache. safeDescribeFields populates this on each
    // successful copy-out and returns its address. Reset on shutdown / on
    // any subsequent safeDescribeFields call. Holding the result by value
    // means inspector iteration never re-enters module code.
    SafeBehaviorSchema safeSchemaCache_;

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
    // P4 (S153) module-level fault flag. Set when any module lifecycle
    // callback (Init, BeginReload, EndReload, Shutdown) faults under SEH
    // or throws. The editor surfaces this so the developer knows the
    // module DLL itself misbehaved during reload-handshake plumbing,
    // distinct from "a single behavior instance crashed in onUpdate".
    bool     moduleDegraded_      = false;
    std::string moduleDegradedReason_;
    // P3 (S153) safe-point bookkeeping. Set true at the top of
    // processPendingReload, cleared on return (via RAII guard so early
    // returns don't leak the flag). performSwap reads this to refuse
    // execution outside the safe-point window. mainThreadId_ is captured
    // on the first processPendingReload call so subsequent calls from
    // worker threads are rejected with a loud LOG_ERROR.
    bool                inSafePoint_  = false;
    std::thread::id     mainThreadId_;
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

    // Drain pendingBuildEndEvent_: if a build worker thread published a
    // build-end event type, call writeStatusFile from the main thread so
    // every status-visible field read is sequenced with its main-thread
    // writer. Idempotent — multiple calls in the same frame collapse via
    // the atomic exchange. See pendingBuildEndEvent_ above for rationale.
    void drainPendingBuildEndEvent();

    // Phase 0: emits logs/hot_reload_status.json atomically. ALL callers must
    // go through this helper so statusEventId_ stays monotonic and
    // statusMutex_ serializes against the buildThread_/main-thread race.
    // See spec docs/superpowers/specs/2026-05-06-ai-hot-reload-behavior-loop-design.md.
    void writeStatusFile(EventType eventType);

    // Phase 0 (Task 7) module-degraded set + emit wrapper. ALL hot-reload
    // call sites that previously called the file-scope hrSetModuleDegraded
    // free helper must route through this member instead so a clean
    // false -> true transition fires exactly one ModuleDegraded status
    // event. Repeated faults on an already-degraded module update the
    // reason string (and refresh the LOG_ERROR dedup) but do NOT emit a
    // duplicate status event.
    //
    // CONTRACT: main thread only. moduleDegraded_ is a plain bool (not
    // atomic); transition detection here reads-then-writes without a
    // mutex. All current callers reach this from inside the
    // processPendingReload safe-point window, tickBehaviors (main thread),
    // or safeDescribeFields (editor inspector main thread). Do NOT call
    // from buildThread_ or any other worker — would torn-read the bool
    // and could double-emit. If a future task needs an off-thread path,
    // promote moduleDegraded_ to std::atomic<bool> first.
    void setModuleDegraded(const char* phase, const HrFaultInfo& fi);

    // Phase 1: returns true when logs/.script_build.lock is present and
    // fresh (< 10 min). Caller (runBuildAsync) checks this BEFORE invoking
    // cmake and bails early on true, emitting BuildSkippedLocked. Stale
    // locks (>= 10 min) are deleted and false is returned (proceed normally).
    // See spec for ownership rule rationale.
    bool isScriptBuildLockActive();

    // Phase 0 status writer state. statusMutex_ protects the trio:
    // (statusEventId_ increment, JSON snapshot, writeFileAtomic call).
    // writeFileAtomic uses a fixed <path>.tmp filename so concurrent writers
    // would race even though each individual rename is atomic to readers.
    // engineSessionId_ + processStartTimestamp_ are session-scoped: captured
    // in initialize(), stable for the process lifetime, reset on shutdown +
    // re-init. The script uses (engineSessionId_, statusEventId_) as the
    // event-boundary tuple so it can detect engine restart mid-run.
    mutable std::mutex statusMutex_;
    uint64_t    statusEventId_         = 0;
    uint64_t    engineSessionId_       = 0;   // generated in initialize(); 0 = uninitialized
    std::string processStartTimestamp_;        // ISO-8601 UTC, captured in initialize()

    // Phase 0 DLL-change detection. runBuildAsync snapshots the DLL's
    // last_write_time + size BEFORE invoking cmake; on success it compares
    // AFTER, then emits BuildSucceededArtifactChanged (sets pendingDllSwap_)
    // or BuildSucceededNoArtifactChange (clears pendingDllSwap_).
    // Stored on the manager so the helper can be called from runBuildAsync
    // without re-statting in the emit path.
    //
    // pendingDllSwap_ is std::atomic<bool> because it crosses the build
    // worker thread and the main thread. The script consumer reads
    // reload_pending out of writeStatusFile's JSON and classifies on it,
    // so we cannot tolerate a torn read. Codex audit P2 follow-up: prior
    // to this change the field was a plain bool and only the JSON write
    // was guarded by statusMutex_, leaving every other write/read pair
    // racy in formal C++23 terms.
    std::filesystem::file_time_type dllPreBuildMtime_{};
    uintmax_t                       dllPreBuildSize_ = 0;
    std::atomic<bool>               pendingDllSwap_{false};

    // Codex audit P1 follow-up #3: writeStatusFile reads main-thread-owned
    // fields (reloadCount_, failureCount_, moduleDegraded_, lastError_,
    // moduleBuildIdStr_) without any synchronization that pairs with the
    // writes on those fields. The build worker used to call writeStatusFile
    // directly to emit BuildSucceeded*/BuildFailed, which made every such
    // read formally racy and could produce incoherent JSON if the main
    // thread happened to be in the middle of a degrade or error update.
    //
    // Fix: the build worker now publishes the EVENT TYPE it wants emitted
    // into pendingBuildEndEvent_ (atomic release), and the main thread
    // drains it at the top of processPendingReload + writeStatusFile,
    // re-issuing writeStatusFile from the main thread where every field
    // read is sequenced with its main-thread writer. Sentinel -1 means
    // "no pending event"; other values are static_cast<int>(EventType).
    // EventType has no None enumerator, so we can't store the enum
    // directly without a separate "valid" flag.
    std::atomic<int>                pendingBuildEndEvent_{-1};

    // Editor-wired dirty-seam callback. See setBehaviorAuthoredData-
    // ChangedCallback above for contract. Null by default.
    BehaviorAuthoredDataChangedCallback authoredDataChangedCb_;
};

} // namespace fate
