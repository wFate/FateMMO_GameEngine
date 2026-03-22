#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace fate {

struct PrefabVariant {
    std::string name;           // e.g. "WarriorPlayer"
    std::string parentName;     // e.g. "BasePlayer"
    nlohmann::json patches;     // JSON Patch array (RFC 6902)
};

// Apply patches to a base JSON, return the composed result
nlohmann::json applyPrefabPatches(const nlohmann::json& base,
                                   const nlohmann::json& patches);

// Compute the minimal JSON Patch diff between base and modified
nlohmann::json computePrefabDiff(const nlohmann::json& base,
                                  const nlohmann::json& modified);

} // namespace fate
