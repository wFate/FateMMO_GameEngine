#include "engine/asset/asset_registry.h"
#include "engine/core/logger.h"
#include <algorithm>
#include <filesystem>

namespace fate {

AssetRegistry& AssetRegistry::instance() {
    static AssetRegistry s_instance;
    return s_instance;
}

AssetRegistry::AssetRegistry() {
    // Reserve slot 0 as null sentinel
    slots_.push_back(AssetSlot{});
}

void AssetRegistry::registerLoader(AssetLoader loader) {
    loaders_.push_back(std::move(loader));
}

// Canonicalize path for consistent lookups (forward slashes, absolute)
static std::string canonicalizePath(const std::string& path) {
    namespace fs = std::filesystem;
    auto canon = fs::weakly_canonical(fs::path(path)).string();
    std::replace(canon.begin(), canon.end(), '\\', '/');
    return canon;
}

AssetHandle AssetRegistry::load(const std::string& path) {
    std::string canon = canonicalizePath(path);

    // Check if already loaded
    auto it = pathToIndex_.find(canon);
    if (it != pathToIndex_.end()) {
        auto& slot = slots_[it->second];
        return AssetHandle::make(it->second, slot.generation);
    }

    // Find loader by extension
    const AssetLoader* loader = findLoader(canon);
    if (!loader) {
        LOG_ERROR("AssetRegistry", "No loader for: %s", canon.c_str());
        return AssetHandle{};
    }

    // Allocate slot
    uint32_t idx = allocSlot();
    auto& slot = slots_[idx];
    slot.path = canon;
    slot.kind = loader->kind;
    slot.data = loader->load(canon);
    slot.loaded = (slot.data != nullptr);

    if (!slot.loaded) {
        LOG_ERROR("AssetRegistry", "Failed to load: %s", canon.c_str());
        freeList_.push_back(idx);
        return AssetHandle{};
    }

    pathToIndex_[canon] = idx;

    // For shaders: register partner path as alias so either file change triggers reload
    if (loader->kind == AssetKind::Shader) {
        namespace fs = std::filesystem;
        auto ext = fs::path(canon).extension().string();
        std::string partner;
        auto stem = fs::path(canon).parent_path() / fs::path(canon).stem();
        if (ext == ".vert") partner = stem.string() + ".frag";
        else if (ext == ".frag") partner = stem.string() + ".vert";
        if (!partner.empty()) {
            std::replace(partner.begin(), partner.end(), '\\', '/');
            pathToIndex_[partner] = idx; // alias points to same slot
        }
    }

    return AssetHandle::make(idx, slot.generation);
}

void AssetRegistry::queueReload(const std::string& path) {
    std::string canon = canonicalizePath(path);
    std::lock_guard lock(reloadQueueMutex_);
    for (auto& pending : pendingReloads_) {
        if (pending.path == canon) {
            pending.lastEventTime = -1.0f; // mark for timestamp update
            return;
        }
    }
    pendingReloads_.push_back({canon, -1.0f});
}

void AssetRegistry::processReloads(float currentTime) {
    std::vector<PendingReload> toProcess;
    {
        std::lock_guard lock(reloadQueueMutex_);
        // Stamp any newly queued entries
        for (auto& p : pendingReloads_) {
            if (p.lastEventTime < 0.0f) p.lastEventTime = currentTime;
        }
        // Copy out entries past debounce delay, keep the rest
        std::vector<PendingReload> remaining;
        for (auto& p : pendingReloads_) {
            if ((currentTime - p.lastEventTime) >= kDebounceDelay) {
                toProcess.push_back(std::move(p));
            } else {
                remaining.push_back(std::move(p));
            }
        }
        pendingReloads_ = std::move(remaining);
    }

    // Process reloads on main thread
    for (const auto& pending : toProcess) {
        auto it = pathToIndex_.find(pending.path);
        if (it == pathToIndex_.end()) continue;

        auto& slot = slots_[it->second];
        const AssetLoader* loader = findLoader(slot.path);
        if (!loader) continue;

        if (!loader->validate(slot.path)) {
            LOG_WARN("AssetRegistry", "Validation failed, skipping reload: %s", slot.path.c_str());
            continue;
        }

        if (loader->reload(slot.data, slot.path)) {
            slot.generation++;
            LOG_INFO("AssetRegistry", "Reloaded: %s (gen %u)", slot.path.c_str(), slot.generation);
        } else {
            LOG_WARN("AssetRegistry", "Reload failed: %s", slot.path.c_str());
        }
    }
}

AssetHandle AssetRegistry::find(const std::string& path) const {
    std::string canon = canonicalizePath(path);
    auto it = pathToIndex_.find(canon);
    if (it == pathToIndex_.end()) return AssetHandle{};
    return AssetHandle::make(it->second, slots_[it->second].generation);
}

void AssetRegistry::clear() {
    for (size_t i = 1; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        if (slot.loaded && slot.data) {
            const AssetLoader* loader = findLoader(slot.path);
            if (loader) loader->destroy(slot.data);
            slot.data = nullptr;
            slot.loaded = false;
        }
    }
    slots_.resize(1); // keep slot 0
    slots_[0] = AssetSlot{};
    freeList_.clear();
    pathToIndex_.clear();
    loaders_.clear();
    {
        std::lock_guard lock(reloadQueueMutex_);
        pendingReloads_.clear();
    }
}

size_t AssetRegistry::assetCount() const {
    size_t count = 0;
    for (size_t i = 1; i < slots_.size(); ++i) {
        if (slots_[i].loaded) ++count;
    }
    return count;
}

const AssetLoader* AssetRegistry::findLoader(const std::string& path) const {
    namespace fs = std::filesystem;
    std::string ext = fs::path(path).extension().string();
    for (const auto& loader : loaders_) {
        for (const auto& e : loader.extensions) {
            if (e == ext) return &loader;
        }
    }
    return nullptr;
}

uint32_t AssetRegistry::allocSlot() {
    if (!freeList_.empty()) {
        uint32_t idx = freeList_.back();
        freeList_.pop_back();
        return idx;
    }
    uint32_t idx = static_cast<uint32_t>(slots_.size());
    slots_.push_back(AssetSlot{});
    return idx;
}

} // namespace fate
