#pragma once
// engine/module/behavior_registry.h
//
// Host-side registry of behavior name -> module callbacks (FateBehaviorVTable).
// Populated by the module via FateHostApi::registerBehavior during init/reload.
// Entirely main-thread; no locks. Reload drains this on the same frame the
// new module is loaded — see HotReloadManager::performSwap.

#include "engine/module/fate_module_abi.h"
#include <string>
#include <unordered_map>

namespace fate {

class BehaviorRegistry {
public:
    static BehaviorRegistry& instance();

    BehaviorRegistry(const BehaviorRegistry&) = delete;
    BehaviorRegistry& operator=(const BehaviorRegistry&) = delete;

    // Returns OK on success; ERR_BAD_ARG when name is null/empty or vtable is null.
    // While beginStaging() is active the call writes to the staging map instead
    // of live, so a failed module init can be rolled back without disturbing
    // the in-flight registrations.
    FateModuleResult registerBehavior(const char* name, const FateBehaviorVTable* vtable);

    // Lookup against the live map; nullptr if not registered.
    const FateBehaviorVTable* find(const std::string& name) const;

    // Drop every live binding. Called by HotReloadManager when the host shuts
    // down (no module to reload into). For reload swaps prefer the staging
    // protocol below — clearing live before the new module's init succeeds
    // would leave behaviors unbound on failure.
    void clear();

    // Staging protocol — atomic registry swap on reload.
    //
    //   beginStaging();                 // route registerBehavior to staging
    //   newModule->init(...);           // populates staging
    //   if (init succeeded) commitStaging();  // staging -> live, gen++
    //   else                abortStaging();   // discard staging, live untouched
    void beginStaging();
    void commitStaging();
    void abortStaging();
    bool isStaging() const { return staging_active_; }

    // Monotonically incremented every clear(). BehaviorComponent uses this to
    // detect when its previously-bound vtable was discarded so the dispatcher
    // can re-issue onStart on the new binding.
    uint32_t generation() const { return generation_; }

    // Read-only iteration for editor diagnostics (e.g. "Loaded behaviors: ..."
    // in the View menu). Not used on hot paths.
    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (const auto& [name, vtable] : map_) fn(name, vtable);
    }

private:
    BehaviorRegistry() = default;

    std::unordered_map<std::string, FateBehaviorVTable> map_;
    std::unordered_map<std::string, FateBehaviorVTable> staging_;
    bool     staging_active_ = false;
    uint32_t generation_ = 0;
};

} // namespace fate
