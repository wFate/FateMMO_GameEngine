/* engine/module/fate_module_abi.h
 *
 * Stable C ABI between the FateEngine.exe host and FateGameRuntime.dll.
 *
 * Lifetime + ownership rules:
 *   - The host owns ALL persistent state: World, entity IDs, components,
 *     allocators, networking, DB handles. The module never holds long-lived
 *     pointers to host memory across reload boundaries.
 *   - The module owns its CODE and transient code-owned scratch (state* pointers
 *     attached to BehaviorComponent::state). Scratch is freed in onDestroy or
 *     fateGameModuleBeginReload before the old DLL is unloaded.
 *   - Neither side passes C++ objects with vtables, STL containers, std::string,
 *     std::function, exceptions, or RTTI across the boundary. JSON travels as
 *     UTF-8 char buffers; everything else is plain integers/floats/pointers.
 *
 * Header is intentionally pure C: includable from the host (C++23) and from
 * the module DLL without dragging fate_engine headers across the seam. */

#ifndef FATE_MODULE_ABI_H
#define FATE_MODULE_ABI_H

#include <stdint.h>
#include <stddef.h>

/* ABI version: bump when this header changes shape (struct fields, callback
 * signatures, calling convention). Host refuses to load a module whose
 * reported ABI version differs from the host's.
 *
 * Version history:
 *   1 — initial slice
 *   2 — fateGameModuleQueryVersion now also reports sizeof(FateHostApi) +
 *       sizeof(FateGameModuleApi) so layout drift between host and module
 *       is rejected even when the version constant matches (catches packing
 *       or ODR mismatch from a stale checkout).
 *   3 — FateBehaviorVTable adds an OPTIONAL describeFields() entry
 *       returning a FateBehaviorSchema*. The inspector uses the schema to
 *       render descriptor-driven widgets (typed defaults + bounds +
 *       tooltips) instead of guessing from raw JSON values. May be null;
 *       behaviors without a schema fall back to the freeform JSON drawer.
 *       Schema this slice supports float/int/bool only (no string ABI).
 *       NOTE: ABI-2 modules are rejected after this bump — the vtable is
 *       larger by one function pointer and reading past the end of an
 *       ABI-2 vtable would be UB. Recompile FateGameRuntime against this
 *       header. */
#define FATE_MODULE_ABI_VERSION 3u

/* Protocol version: bump when the JSON wire format / field semantics for
 * BehaviorComponent change in a non-backwards-compatible way. Migrate
 * callbacks (FateBehaviorVTable::migrate) bridge older payloads up. */
#define FATE_MODULE_PROTOCOL_VERSION 1u

#if defined(_WIN32)
#  if defined(FATE_GAME_RUNTIME_BUILD)
#    define FATE_MODULE_EXPORT __declspec(dllexport)
#  else
#    define FATE_MODULE_EXPORT __declspec(dllimport)
#  endif
#else
#  define FATE_MODULE_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles. Host implements; module never dereferences. */
typedef struct FateBehaviorCtx       FateBehaviorCtx;       /* per-call: which entity, which component, dt */
typedef struct FateReloadContext     FateReloadContext;     /* per-reload bookkeeping */

/* Result codes. Stable; never renumber. */
typedef enum FateModuleResult {
    FATE_MODULE_OK                = 0,
    FATE_MODULE_ERR_VERSION       = 1,  /* ABI or protocol mismatch */
    FATE_MODULE_ERR_INIT_FAILED   = 2,
    FATE_MODULE_ERR_BAD_ARG       = 3,
    FATE_MODULE_ERR_INTERNAL      = 4
} FateModuleResult;

/* Log levels mirror engine/core/logger.h LogLevel ordering. */
typedef enum FateLogLevel {
    FATE_LOG_DEBUG = 0,
    FATE_LOG_INFO  = 1,
    FATE_LOG_WARN  = 2,
    FATE_LOG_ERROR = 3
} FateLogLevel;

/* ---------------------------------------------------------------------------
 * Behavior schema (ABI v3). A behavior may declare its expected authoring
 * fields so the inspector can render descriptor-driven widgets (typed
 * defaults + bounds + tooltips) instead of guessing from raw JSON.
 *
 * Lifetime: the FateFieldDescriptor and FateBehaviorSchema structs returned
 * from describeFields() must have STATIC storage duration in the module —
 * the host holds the pointers for as long as the module is loaded.
 *
 * String fields are NOT supported in ABI v3: there are no string getters or
 * setters in FateHostApi. Future bump can add (FateFieldType_String + a
 * proper string ABI with caller-owned buffers + truncation semantics).
 * ------------------------------------------------------------------------- */
typedef enum FateFieldType {
    FATE_FIELD_FLOAT = 0,
    FATE_FIELD_INT   = 1,
    FATE_FIELD_BOOL  = 2
} FateFieldType;

typedef struct FateFieldDescriptor {
    const char*    name;        /* stable JSON key */
    FateFieldType  type;
    /* Defaults — only the field matching `type` is read. The host uses
     * these to seed bc->fields[name] when the inspector creates a new
     * BehaviorComponent and to render the "reset to default" affordance. */
    float          defaultF;
    int32_t        defaultI;
    int            defaultB;    /* 0 or 1 */
    /* Optional bounds for slider/clamp widgets. For FLOAT/INT, set
     * minF == maxF (or minI == maxI) to mean "unbounded — drag widget".
     * For BOOL the bounds fields are ignored. */
    float          minF;
    float          maxF;
    int32_t        minI;
    int32_t        maxI;
    /* Optional UTF-8 tooltip. May be null. */
    const char*    tooltip;
} FateFieldDescriptor;

typedef struct FateBehaviorSchema {
    const FateFieldDescriptor* fields;
    uint32_t                   fieldCount;
    /* Reserved for forward-compatibility (e.g. category groupings). Must be
     * zero in ABI v3. Future bumps can repurpose. */
    uint32_t                   reserved;
} FateBehaviorSchema;

/* ---------------------------------------------------------------------------
 * Host -> Module: behavior callbacks.
 *
 * All callbacks are invoked on the host main thread between frames. They must
 * not retain pointers to FateBehaviorCtx beyond the call.
 * ------------------------------------------------------------------------- */
typedef struct FateBehaviorVTable {
    /* Called once when a BehaviorComponent first becomes active or when a
     * reload installs a different behavior on an existing component. */
    void (*onStart)(FateBehaviorCtx* ctx);

    /* Per-frame tick. dt is in seconds. */
    void (*onUpdate)(FateBehaviorCtx* ctx, float dt);

    /* Called when the host sees a destroy or the behavior is being swapped
     * out (component removed, entity destroyed, behavior name changed). */
    void (*onDestroy)(FateBehaviorCtx* ctx);

    /* Optional: bridge older serialized payloads to the current schema. May be
     * null, in which case fields pass through untouched. fromVersion is the
     * protocol version that originally wrote the payload. Returns OK on
     * success; on failure the host keeps the original fields and logs a
     * warning. */
    FateModuleResult (*migrate)(FateBehaviorCtx* ctx, uint32_t fromVersion);

    /* ABI v3: optional schema accessor. Returns a STATIC FateBehaviorSchema
     * describing the behavior's authoring fields, or null. The inspector
     * renders descriptor-driven widgets when non-null and falls back to the
     * freeform JSON drawer otherwise. Caller must not free the returned
     * pointer; the module owns the lifetime. */
    const FateBehaviorSchema* (*describeFields)(void);
} FateBehaviorVTable;

/* ---------------------------------------------------------------------------
 * Module -> Host: services exposed by the host.
 *
 * Every function pointer is non-null after fateGameModuleInit returns OK.
 * Strings are UTF-8, NUL-terminated.
 * ------------------------------------------------------------------------- */
typedef struct FateHostApi {
    uint32_t hostAbiVersion;       /* always FATE_MODULE_ABI_VERSION */
    uint32_t hostProtocolVersion;  /* always FATE_MODULE_PROTOCOL_VERSION */

    /* Logging routes through the host's spdlog so module output appears in
     * fate_engine.log and the editor LogViewer alongside engine logs. */
    void (*log)(FateLogLevel level, const char* category, const char* message);

    /* Register a behavior implementation under a stable name. The host stores
     * the vtable; subsequent BehaviorComponent::behavior strings matching this
     * name will dispatch to it. Re-registering an existing name overwrites
     * (this is how reload swaps in new code). */
    FateModuleResult (*registerBehavior)(const char* name, const FateBehaviorVTable* vtable);

    /* Per-call ctx accessors. All are safe no-ops when ctx is null. */
    uint64_t (*ctxEntityId)(FateBehaviorCtx* ctx);
    int      (*ctxIsEnabled)(FateBehaviorCtx* ctx);

    /* AUTHORING field getters/setters. Names are stable string keys into the
     * BehaviorComponent::fields JSON object — designer-edited, SERIALIZED to
     * scene .json. Use these for tunable knobs (radius, speed, target tag).
     * Default values are returned when the key is missing or has the wrong
     * type. */
    float    (*getFloat)(FateBehaviorCtx* ctx, const char* name, float defaultValue);
    int32_t  (*getInt)(FateBehaviorCtx* ctx, const char* name, int32_t defaultValue);
    int      (*getBool)(FateBehaviorCtx* ctx, const char* name, int defaultValue);

    void     (*setFloat)(FateBehaviorCtx* ctx, const char* name, float value);
    void     (*setInt)(FateBehaviorCtx* ctx, const char* name, int32_t value);
    void     (*setBool)(FateBehaviorCtx* ctx, const char* name, int value);

    /* RUNTIME field getters/setters. Names key into BehaviorComponent::
     * runtimeFields — NOT serialized, cleared by the host on every onDestroy
     * (entity destroy, component removal, scene unload, reload, behavior-
     * name change). Use these for theta accumulators, captured anchor
     * positions, cached IDs — anything that must never leak into a saved
     * scene. */
    float    (*getRuntimeFloat)(FateBehaviorCtx* ctx, const char* name, float defaultValue);
    int32_t  (*getRuntimeInt)(FateBehaviorCtx* ctx, const char* name, int32_t defaultValue);
    int      (*getRuntimeBool)(FateBehaviorCtx* ctx, const char* name, int defaultValue);

    void     (*setRuntimeFloat)(FateBehaviorCtx* ctx, const char* name, float value);
    void     (*setRuntimeInt)(FateBehaviorCtx* ctx, const char* name, int32_t value);
    void     (*setRuntimeBool)(FateBehaviorCtx* ctx, const char* name, int value);

    /* Transform helpers — read/write the entity's Transform. Hot reload's
     * primary use case is gameplay behaviors that nudge transforms; exposing
     * these directly keeps simple behaviors purely in C territory. */
    int  (*getEntityPos)(FateBehaviorCtx* ctx, float* outX, float* outY);
    void (*setEntityPos)(FateBehaviorCtx* ctx, float x, float y);

    /* Transient code-owned scratch. The host stores the pointer on the
     * BehaviorComponent and surfaces it back on the next call. Module is
     * responsible for releasing in onDestroy or fateGameModuleBeginReload. */
    void* (*getState)(FateBehaviorCtx* ctx);
    void  (*setState)(FateBehaviorCtx* ctx, void* state);
} FateHostApi;

/* ---------------------------------------------------------------------------
 * Module -> Host: function table the host invokes.
 * fateGameModuleInit fills this out so the host can call into the module
 * uniformly. Each entry is mandatory unless documented otherwise. */
typedef struct FateGameModuleApi {
    uint32_t moduleAbiVersion;       /* must equal FATE_MODULE_ABI_VERSION */
    uint32_t moduleProtocolVersion;  /* must equal FATE_MODULE_PROTOCOL_VERSION */

    /* Diagnostic strings owned by the module (static lifetime). */
    const char* moduleName;
    const char* moduleBuildId;       /* e.g. __DATE__ " " __TIME__ */

    /* Optional per-frame tick the module runs once globally (ahead of any
     * per-behavior onUpdate). May be null. */
    void (*tick)(float dt);
} FateGameModuleApi;

/* ---------------------------------------------------------------------------
 * Reload context. Travels between fateGameModuleBeginReload (called on the
 * OUTGOING module) and fateGameModuleEndReload (called on the INCOMING
 * module). Lets a module flush transient caches without touching host
 * persistent state. */
struct FateReloadContext {
    uint32_t fromAbiVersion;
    uint32_t toAbiVersion;
    uint32_t fromProtocolVersion;
    uint32_t toProtocolVersion;
    uint32_t generation;             /* monotonically increasing per swap */
    void*    reserved;               /* host-owned; module ignores */
};

/* ---------------------------------------------------------------------------
 * Required exported symbols.
 *
 * fateGameModuleQueryVersion — host calls FIRST, before init, to validate ABI
 *                             compatibility without committing any state.
 *                             Reports BOTH the ABI/protocol version constants
 *                             AND the module's view of sizeof(FateHostApi)
 *                             and sizeof(FateGameModuleApi), so packing /
 *                             ODR / stale-header drift is caught even when
 *                             both sides claim the same version.
 *
 * fateGameModuleInit       — host calls after version check passes. Module
 *                             fills out *out and registers built-in behaviors
 *                             via host->registerBehavior. Return non-zero
 *                             on success, zero on failure.
 *
 * fateGameModuleShutdown   — host calls before FreeLibrary. Module releases
 *                             scratch state, unregisters log sinks, etc.
 *
 * fateGameModuleBeginReload — host calls on the OUTGOING module right before
 *                             swapping. Module flushes transient state owned
 *                             by its TU. Return non-zero on success; zero
 *                             aborts the swap and keeps the old module live.
 *
 * fateGameModuleEndReload  — host calls on the INCOMING module after the swap
 *                             so it can re-bind to host services if needed.
 *                             Return non-zero on success; zero is logged and
 *                             counted as a failure (the new module is still
 *                             live but flagged as in a degraded state). */
FATE_MODULE_EXPORT void fateGameModuleQueryVersion(uint32_t* outAbi,
                                                   uint32_t* outProtocol,
                                                   uint32_t* outHostApiSize,
                                                   uint32_t* outModuleApiSize);
FATE_MODULE_EXPORT int  fateGameModuleInit(const FateHostApi* host, FateGameModuleApi* out);
FATE_MODULE_EXPORT void fateGameModuleShutdown(void);
FATE_MODULE_EXPORT int  fateGameModuleBeginReload(FateReloadContext* ctx);
FATE_MODULE_EXPORT int  fateGameModuleEndReload(FateReloadContext* ctx);

/* Symbol names the host resolves via GetProcAddress. Keep in sync with the
 * declarations above. */
#define FATE_SYM_QUERY_VERSION    "fateGameModuleQueryVersion"
#define FATE_SYM_INIT             "fateGameModuleInit"
#define FATE_SYM_SHUTDOWN         "fateGameModuleShutdown"
#define FATE_SYM_BEGIN_RELOAD     "fateGameModuleBeginReload"
#define FATE_SYM_END_RELOAD       "fateGameModuleEndReload"

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FATE_MODULE_ABI_H */
