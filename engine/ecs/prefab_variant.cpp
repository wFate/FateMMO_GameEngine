#include "engine/ecs/prefab_variant.h"

namespace fate {

nlohmann::json applyPrefabPatches(const nlohmann::json& base,
                                   const nlohmann::json& patches) {
    return base.patch(patches);
}

nlohmann::json computePrefabDiff(const nlohmann::json& base,
                                  const nlohmann::json& modified) {
    return nlohmann::json::diff(base, modified);
}

} // namespace fate
