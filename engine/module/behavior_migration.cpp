#include "engine/module/behavior_migration.h"
#include "engine/module/behavior_component.h"

#include <unordered_map>

namespace fate {

namespace {

// Inject `fld`'s schema default into `fields[fld.name]`, overwriting any
// existing value at that key. Caller decides whether overwrite is safe
// (rule 1 only fires when the key is absent; rule 3 has already moved
// the stale value into quarantine before this is called).
void injectDefault(nlohmann::json& fields, const MigrationField& fld) {
    switch (fld.type) {
        case FATE_FIELD_FLOAT: fields[fld.name] = fld.defaultF;          break;
        case FATE_FIELD_INT:   fields[fld.name] = fld.defaultI;          break;
        case FATE_FIELD_BOOL:  fields[fld.name] = (fld.defaultB != 0);   break;
    }
}

// Move `fields[name]` under `fields["__fate_migrated"][name]` and erase
// the original. No-op if `name` isn't present. Creates the quarantine
// object lazily so empty migrations don't litter the JSON.
void quarantine(nlohmann::json& fields, const std::string& name) {
    auto it = fields.find(name);
    if (it == fields.end()) return;

    auto qIt = fields.find(kBehaviorReservedKey_FateMigrated);
    if (qIt == fields.end() || !qIt->is_object()) {
        fields[kBehaviorReservedKey_FateMigrated] = nlohmann::json::object();
        qIt = fields.find(kBehaviorReservedKey_FateMigrated);
    }
    (*qIt)[name] = std::move(*it);
    fields.erase(it);
}

const char* fieldTypeName(FateFieldType t) {
    switch (t) {
        case FATE_FIELD_FLOAT: return "float";
        case FATE_FIELD_INT:   return "int";
        case FATE_FIELD_BOOL:  return "bool";
    }
    return "?";
}

} // namespace

BehaviorMigrationResult applyHostSchemaDiff(
    nlohmann::json& fields,
    const MigrationSchema& oldSchema,
    const MigrationSchema& newSchema,
    const std::string& behaviorNameForLog) {

    BehaviorMigrationResult result;

    // Defensive: bc->fields may not be an object on a malformed payload.
    // The deserializer normalizes to object{} on load, but a programmatic
    // caller (test or future authoring tool) could pass anything. Reset
    // to an empty object — losing the malformed contents is the only
    // safe move; the only diff outcome would be "everything authored is
    // a type mismatch" which is the same as wiping.
    if (!fields.is_object()) fields = nlohmann::json::object();

    // Build name → descriptor maps for O(1) cross-lookup.
    std::unordered_map<std::string, const MigrationField*> oldByName;
    oldByName.reserve(oldSchema.fields.size());
    for (const auto& f : oldSchema.fields) oldByName[f.name] = &f;

    std::unordered_map<std::string, const MigrationField*> newByName;
    newByName.reserve(newSchema.fields.size());
    for (const auto& f : newSchema.fields) newByName[f.name] = &f;

    // ---- Rule 3: type mismatch ----
    // Walk new schema first because rule 3 is "same name, different
    // type" — we need both maps to be queryable. Quarantine the stale
    // value, then inject the new default. Counted separately from
    // rule 1 even though both paths end with injectDefault.
    for (const auto& nf : newSchema.fields) {
        auto oldIt = oldByName.find(nf.name);
        if (oldIt == oldByName.end()) continue;          // not in old → rule 1 territory
        if (oldIt->second->type == nf.type) continue;    // unchanged → no-op

        // Authored value at this key — move to quarantine before the
        // default overwrites it. (If no authored value exists, the
        // designer never set it; just inject the new default.)
        if (fields.contains(nf.name)) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "behavior='%s' field '%s' type changed (%s -> %s); old value quarantined to %s",
                behaviorNameForLog.c_str(), nf.name.c_str(),
                fieldTypeName(oldIt->second->type), fieldTypeName(nf.type),
                kBehaviorReservedKey_FateMigrated);
            result.warnLines.emplace_back(buf);
            quarantine(fields, nf.name);
        }
        injectDefault(fields, nf);
        ++result.typeMismatches;
        result.authoredChanged = true;
    }

    // ---- Rule 1: added field ----
    // New schema has it, old didn't. If authored already has the key
    // (e.g. designer pre-edited a freeform JSON field that's now
    // formally schema'd), leave the authored value. Otherwise inject
    // the schema default. Does NOT mark dirty: the on-disk file
    // implicitly carried the pre-default value and the inspector will
    // surface the new default as authored-equivalent.
    for (const auto& nf : newSchema.fields) {
        if (oldByName.count(nf.name)) continue;          // rule 3 territory above
        if (fields.contains(nf.name)) continue;          // already authored, leave alone
        injectDefault(fields, nf);

        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "behavior='%s' added field '%s' (%s) default-injected",
            behaviorNameForLog.c_str(), nf.name.c_str(), fieldTypeName(nf.type));
        result.infoLines.emplace_back(buf);
        ++result.addedDefaults;
    }

    // ---- Rule 2: removed field ----
    // Old schema declared it, new doesn't. Quarantine any authored
    // value so the designer can recover or manually drop. Marks dirty.
    for (const auto& of : oldSchema.fields) {
        if (newByName.count(of.name)) continue;          // unchanged or rule 3
        if (!fields.contains(of.name)) continue;         // nothing authored to preserve

        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "behavior='%s' removed field '%s' quarantined to %s (designer can recover or delete)",
            behaviorNameForLog.c_str(), of.name.c_str(),
            kBehaviorReservedKey_FateMigrated);
        result.infoLines.emplace_back(buf);
        quarantine(fields, of.name);
        ++result.quarantined;
        result.authoredChanged = true;
    }

    return result;
}

// ---------------------------------------------------------------------------
// runOneEntityMigration — see behavior_migration.h for contract.
//
// Mirrors the per-entity body of HotReloadManager::applyMigrations, with the
// SEH-guarded module callback abstracted behind Tier2Runner so the helper is
// testable without LoadLibrary or a real DLL. Caller (HotReloadManager in
// production, doctest fixtures in tests) wires the runner.
// ---------------------------------------------------------------------------
OneEntityMigrationOutcome runOneEntityMigration(
    nlohmann::json& fields,
    const OneEntityMigrationInput& in) {

    OneEntityMigrationOutcome out;

    // Defensive: empty schemas are valid inputs. Use empty when null.
    static const MigrationSchema kEmptySchema{};
    const MigrationSchema& oldS = in.oldSchema ? *in.oldSchema : kEmptySchema;
    const MigrationSchema& newS = in.newSchema ? *in.newSchema : kEmptySchema;

    // ----- Decision: does Tier 2 need to run? -----
    // Tier 2 is "required" when the protocol moved AND the new vtable
    // declared a migrate slot. "WillRun" is the subset where the host
    // can safely call the module — degraded modules trigger preserve+
    // retry without ever invoking the faulted DLL.
    const bool tier2Required = in.migrationOldProtocolVersion != in.currentProtocol
                            && in.hasMigrateSlot;
    const bool tier2WillRun  = tier2Required && !in.moduleDegraded
                            && (bool)in.runTier2;

    // Snapshot for rollback. Cheap (per-entity, once per reload). The
    // host-API setters used inside the runner mutate fields directly,
    // so a runner that writes a few keys then faults leaves partial
    // writes that would otherwise leak into Tier 1 or back into the
    // pre-swap view of "the original payload".
    nlohmann::json beforeMigrate = fields;

    // ----- Tier 2 dispatch -----
    bool tier2Succeeded         = false;
    bool tier2RequiredButFailed = false;

    if (tier2WillRun) {
        Tier2Result tr = in.runTier2(fields, in.migrationOldProtocolVersion);
        out.tier2Ran     = true;
        out.tier2Outcome = tr.outcome;

        char ebuf[512];
        switch (tr.outcome) {
            case Tier2RunOutcome::Ok: {
                tier2Succeeded = true;
                if (fields != beforeMigrate) {
                    char ibuf[256];
                    std::snprintf(ibuf, sizeof(ibuf),
                        "behavior='%s' migrate(fromVersion=%u -> %u) modified authored fields",
                        in.behaviorName.c_str(),
                        in.migrationOldProtocolVersion, in.currentProtocol);
                    out.infoLines.emplace_back(ibuf);
                    out.fireDirtySeam = true;
                }
                break;
            }
            case Tier2RunOutcome::NonOk: {
                fields = beforeMigrate;  // rollback partial writes
                tier2RequiredButFailed = true;
                std::snprintf(ebuf, sizeof(ebuf),
                    "behavior='%s' migrate(fromVersion=%u) returned %d — "
                    "rolling back partial writes; SKIPPING Tier 1 + protocol stamp; "
                    "entity preserved at protocol=%u for retry on next healthy reload",
                    in.behaviorName.c_str(), in.migrationOldProtocolVersion,
                    tr.resultCode, in.migrationOldProtocolVersion);
                out.errorLines.emplace_back(ebuf);
                break;
            }
            case Tier2RunOutcome::FaultedSEH:
            case Tier2RunOutcome::FaultedCpp: {
                fields = beforeMigrate;  // rollback partial writes
                tier2RequiredButFailed = true;
                const char* faultKind = (tr.outcome == Tier2RunOutcome::FaultedSEH)
                                            ? "SEH" : "C++ exception";
                std::snprintf(ebuf, sizeof(ebuf),
                    "behavior='%s' migrate(fromVersion=%u) faulted (%s) — "
                    "rolling back partial writes; SKIPPING Tier 1 + protocol stamp; "
                    "entity preserved at protocol=%u for retry on next healthy reload",
                    in.behaviorName.c_str(), in.migrationOldProtocolVersion,
                    faultKind, in.migrationOldProtocolVersion);
                out.errorLines.emplace_back(ebuf);
                if (!tr.faultDetail.empty()) {
                    out.errorLines.emplace_back(tr.faultDetail);
                }
                break;
            }
        }
    } else if (tier2Required) {
        // Required but module is degraded (or runner is null). Same
        // preserve+retry semantics as the fault path: keep the entity
        // at its old protocol and fields, log loudly, wait for the
        // next healthy reload.
        tier2RequiredButFailed = true;
        char ebuf[512];
        std::snprintf(ebuf, sizeof(ebuf),
            "behavior='%s' migrate(fromVersion=%u -> %u) required but "
            "module is degraded — SKIPPING Tier 1 + protocol stamp; "
            "entity preserved at protocol=%u for retry on next healthy reload",
            in.behaviorName.c_str(), in.migrationOldProtocolVersion,
            in.currentProtocol, in.migrationOldProtocolVersion);
        out.errorLines.emplace_back(ebuf);
    }

    // ----- Failure path: mark faulted, return without Tier 1 -----
    if (tier2RequiredButFailed) {
        out.markFaulted = true;
        char fbuf[512];
        std::snprintf(fbuf, sizeof(fbuf),
            "behavior='%s' v%u->v%u migration required but did not complete; "
            "v%u dispatch suppressed until next healthy reload re-runs migrate()",
            in.behaviorName.c_str(),
            in.migrationOldProtocolVersion, in.currentProtocol,
            in.currentProtocol);
        out.faultMessage = fbuf;
        return out;
    }

    // Suppress unused-variable warning when nothing references
    // tier2Succeeded above (it's a documentation aid for the failure path
    // and a debug-time invariant).
    (void)tier2Succeeded;

    // ----- Tier 1: pure host schema diff -----
    BehaviorMigrationResult diff =
        applyHostSchemaDiff(fields, oldS, newS, in.behaviorName);

    out.tier1Ran = true;

    for (auto& s : diff.infoLines) out.infoLines.emplace_back(std::move(s));
    for (auto& s : diff.warnLines) out.warnLines.emplace_back(std::move(s));
    if (diff.authoredChanged) out.fireDirtySeam = true;

    // ----- Stamp protocol -----
    // Entity has been advanced past any required Tier 2 AND through the
    // host's Tier 1 policy. Caller writes bc->payloadProtocolVersion =
    // currentProtocol when stampNewProtocol is true.
    out.stampNewProtocol = true;

    return out;
}

} // namespace fate
