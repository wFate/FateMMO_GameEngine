#include "engine/module/behavior_registry.h"
#include "engine/core/logger.h"

namespace fate {

BehaviorRegistry& BehaviorRegistry::instance() {
    static BehaviorRegistry s_instance;
    return s_instance;
}

FateModuleResult BehaviorRegistry::registerBehavior(const char* name, const FateBehaviorVTable* vtable) {
    if (!name || !*name || !vtable) return FATE_MODULE_ERR_BAD_ARG;

    // Re-registering the same name overwrites — that is exactly how a reload
    // installs new code on existing BehaviorComponents.
    if (staging_active_) {
        staging_[std::string(name)] = *vtable;
    } else {
        map_[std::string(name)] = *vtable;
    }
    return FATE_MODULE_OK;
}

const FateBehaviorVTable* BehaviorRegistry::find(const std::string& name) const {
    auto it = map_.find(name);
    return (it == map_.end()) ? nullptr : &it->second;
}

void BehaviorRegistry::clear() {
    if (!map_.empty()) {
        LOG_INFO("BehaviorRegistry", "Clearing %zu behaviors (gen %u -> %u)",
                 map_.size(), generation_, generation_ + 1);
    }
    map_.clear();
    staging_.clear();
    staging_active_ = false;
    ++generation_;
}

void BehaviorRegistry::beginStaging() {
    staging_.clear();
    staging_active_ = true;
}

void BehaviorRegistry::commitStaging() {
    if (!staging_active_) return;
    LOG_INFO("BehaviorRegistry", "Commit staging: %zu behaviors (gen %u -> %u)",
             staging_.size(), generation_, generation_ + 1);
    map_ = std::move(staging_);
    staging_.clear();
    staging_active_ = false;
    ++generation_;
}

void BehaviorRegistry::abortStaging() {
    if (!staging_active_) return;
    LOG_WARN("BehaviorRegistry", "Abort staging: discarding %zu pending behaviors", staging_.size());
    staging_.clear();
    staging_active_ = false;
}

} // namespace fate
