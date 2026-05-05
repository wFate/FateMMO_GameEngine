#pragma once
// engine/module/behavior_migration.h
//
// Host-side schema-evolution policy for BehaviorComponent payloads.
//
// HotReloadManager calls applyHostSchemaDiff() on every live
// BehaviorComponent when the module reloads. It also calls the module's
// optional FateBehaviorVTable::migrate callback when the payload's
// protocol version is older than the running module's. This header
// declares the host-side half (the four pure schema-evolution rules);
// the module-callback half lives in HotReloadManager because it needs
// SEH wrapping + roster integration.
//
// Rules (per S163 design decisions):
//   1. Added field   — inject the schema default into bc->fields under
//                      the new field name. Does NOT mark the scene
//                      dirty; the on-disk file already implies the
//                      pre-default value, and the designer hasn't
//                      authored anything yet.
//   2. Removed field — if bc->fields contains the old name, MOVE its
//                      value to bc->fields["__fate_migrated"][oldName]
//                      and erase the original. Marks scene dirty.
//   3. Type mismatch — same name, schema says different type now.
//                      Move the existing value to quarantine AND inject
//                      the new schema default at the real key. Logs
//                      a warning. Marks scene dirty.
//   4. No change     — schema hash matches; no mutation.
//
// `__fate_migrated` is a host-reserved key (see
// behavior_component.h::kBehaviorReservedKey_FateMigrated). Behaviors
// MUST NOT declare schema fields beginning with `__`.
//
// Intentionally pure: no logging from the reduce loop (callers log
// summaries based on the returned counts), no module re-entry, no
// editor / dirty-bit coupling. Tests can drive this directly without
// instantiating HotReloadManager or a real DLL.

#include "engine/module/fate_module_abi.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <vector>

namespace fate {

// Mirror of HotReloadManager::SafeBehaviorField — duplicated here so
// behavior_migration.h doesn't pull in the full hot_reload_manager.h
// transitive dependency tree (file_watcher, std::thread, EntityHandle).
// Conversion is trivial (same field-by-field shape).
struct MigrationField {
    std::string   name;
    FateFieldType type     = FATE_FIELD_FLOAT;
    float         defaultF = 0.0f;
    int32_t       defaultI = 0;
    int           defaultB = 0;       // 0 or 1
};

struct MigrationSchema {
    std::vector<MigrationField> fields;
};

// Return value from applyHostSchemaDiff. Counts let callers log a
// concise summary line and decide whether to fire the dirty seam.
struct BehaviorMigrationResult {
    int  addedDefaults  = 0;  // rule 1
    int  quarantined    = 0;  // rule 2
    int  typeMismatches = 0;  // rule 3 (counted separately for clarity)
    bool authoredChanged = false;  // true when rule 2 OR 3 fired

    // Per-field diagnostic strings the caller can fan out to LOG_INFO/
    // LOG_WARN. Empty if nothing happened. Caller owns formatting.
    std::vector<std::string> infoLines;
    std::vector<std::string> warnLines;
};

// Applies the four schema-evolution rules to `fields` in place.
// `oldSchema` may be empty (e.g. behavior added schema for the first
// time — every authored field is treated as "removed" and moved to
// quarantine, matching rule 2 semantics; new fields trigger rule 1).
// `newSchema` may be empty (behavior dropped its schema — every
// authored field becomes "removed" and quarantines).
// `behaviorNameForLog` is folded into the diagnostic strings only.
//
// Returns a result struct: counts for summary logging and an
// `authoredChanged` flag that callers route to the dirty seam.
BehaviorMigrationResult applyHostSchemaDiff(
    nlohmann::json& fields,
    const MigrationSchema& oldSchema,
    const MigrationSchema& newSchema,
    const std::string& behaviorNameForLog);

// ---------------------------------------------------------------------------
// runOneEntityMigration — per-entity Tier 2 + Tier 1 orchestration.
//
// Pure-engine, Editor-free. Sits between HotReloadManager::applyMigrations
// (which knows about Active rows + SEH wrappers) and applyHostSchemaDiff
// (which is the pure schema-diff policy). Tests drive every outcome path
// without standing up a real DLL or HotReloadManager.
//
// Inputs:
//   fields                          — bc->fields, mutated in place
//   migrationOldProtocolVersion     — captured pre-swap protocol stamp
//   currentProtocol                 — running module's protocol
//   hasMigrateSlot                  — newVt->migrate != nullptr
//   moduleDegraded                  — module flagged degraded
//   oldSchema, newSchema            — pre/post swap schemas
//   behaviorName                    — for log + fault-message context
//   runTier2                        — caller-supplied SEH-wrapped migrate
//                                     runner. May be null when no Tier 2
//                                     dispatch is needed/possible.
//
// Decision tree:
//   1. If protocols match OR no migrate slot     → Tier 1 only, no fault.
//   2. If protocols differ AND has migrate slot:
//      a. Module degraded                       → markFaulted, no Tier 1,
//                                                  no protocol stamp.
//      b. runTier2 returns Faulted              → rollback, markFaulted,
//                                                  no Tier 1, no stamp.
//      c. runTier2 returns NonOk                → rollback, markFaulted,
//                                                  no Tier 1, no stamp.
//      d. runTier2 returns Ok                   → Tier 1 cleanup, stamp.
//
// The caller applies the outcome to its Active row (faulted bit, fault
// message), to bc->payloadProtocolVersion (stampNewProtocol), and to
// the dirty-seam callback (fireDirtySeam). Diagnostic strings are routed
// to LOG_INFO / LOG_WARN / LOG_ERROR by category.
// ---------------------------------------------------------------------------

enum class Tier2RunOutcome {
    Ok,             // module migrate returned FATE_MODULE_OK without faulting
    NonOk,          // module migrate returned a non-OK FateModuleResult
    FaultedSEH,     // SEH wrapper caught a structured exception
    FaultedCpp      // SEH wrapper caught a C++ exception
};

struct Tier2Result {
    Tier2RunOutcome outcome    = Tier2RunOutcome::Ok;
    int             resultCode = 0;     // populated when outcome == NonOk
    std::string     faultDetail;        // populated when outcome == FaultedSEH/Cpp
};

// Runner abstracts the SEH-guarded module callback so tests can simulate
// every outcome without LoadLibrary. Production: HotReloadManager wraps
// hrCallMigrate in a lambda. Tests: lambda returns canned Tier2Result.
//
// The runner is allowed to mutate `fields` in place (host-API setters do
// that in production). runOneEntityMigration snapshots `fields` before
// invoking and restores from the snapshot when the outcome is anything
// but Ok — so partial writes can never leak into Tier 1 or back into a
// later reload's view of "the original v1 payload".
using Tier2Runner =
    std::function<Tier2Result(nlohmann::json& fields, uint32_t fromVersion)>;

struct OneEntityMigrationInput {
    uint32_t migrationOldProtocolVersion = 1;
    uint32_t currentProtocol             = 1;
    bool     hasMigrateSlot              = false;
    bool     moduleDegraded              = false;
    const MigrationSchema* oldSchema     = nullptr;
    const MigrationSchema* newSchema     = nullptr;
    std::string behaviorName;
    Tier2Runner runTier2;
};

struct OneEntityMigrationOutcome {
    bool stampNewProtocol = false;   // caller sets bc->payloadProtocolVersion
    bool markFaulted      = false;   // caller sets Active::faulted = true
    std::string faultMessage;        // populated when markFaulted
    bool fireDirtySeam    = false;   // caller invokes authoredDataChangedCb_

    bool tier1Ran  = false;          // applyHostSchemaDiff ran
    bool tier2Ran  = false;          // runTier2 was invoked
    Tier2RunOutcome tier2Outcome = Tier2RunOutcome::Ok;

    // Diagnostic strings the caller fans out to LOG_*. Empty on the no-op
    // success path (matches existing applyMigrations log volume).
    std::vector<std::string> infoLines;
    std::vector<std::string> warnLines;
    std::vector<std::string> errorLines;
};

OneEntityMigrationOutcome runOneEntityMigration(
    nlohmann::json& fields,
    const OneEntityMigrationInput& in);

} // namespace fate
