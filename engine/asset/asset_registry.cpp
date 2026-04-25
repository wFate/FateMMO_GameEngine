#include "engine/asset/asset_registry.h"
#include "engine/asset/asset_source.h"
#include "engine/job/job_system.h"
#include "engine/core/logger.h"
#include <algorithm>
#include <filesystem>
#include <thread>
#include <deque>
#include <mutex>

namespace fate {

namespace {
// Ring of most-recent reads, across all threads. Capped to avoid unbounded
// growth; /vfs_status only needs the last ~N. Lock-guarded because readBytes/
// readText run on fiber-job workers (see IAssetSource doc).
constexpr size_t kReadLogCapacity = 32;
std::mutex g_readLogMutex;
std::deque<AssetRegistry::ReadLogEntry> g_readLog;

void pushReadLog(std::string_view key, const std::optional<std::vector<uint8_t>>* bytes,
                 const std::optional<std::string>* text) {
    AssetRegistry::ReadLogEntry e;
    e.key.assign(key.data(), key.size());
    if (bytes && *bytes) { e.ok = true; e.bytes = (*bytes)->size(); }
    else if (text && *text) { e.ok = true; e.bytes = (*text)->size(); }
    std::lock_guard lock(g_readLogMutex);
    if (g_readLog.size() >= kReadLogCapacity) g_readLog.pop_front();
    g_readLog.push_back(std::move(e));
}
} // namespace

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

// Logical asset key. Forward-slash, no filesystem resolution, no symlink
// walk, no absolute-path conversion. Stable across loose-files (DirectFs)
// and packaged .pak (Vfs). All registry callers and the FileWatcher
// callback go through this — they MUST produce matching strings or hot
// reload silently no-ops. Phase 2 (2026-04-24) replaced the prior
// weakly_canonical-based identity with this; canonicalizePath() is now
// just an alias kept for grep continuity.
std::string AssetRegistry::toAssetKey(std::string_view path) {
    std::string s(path);
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

static std::string canonicalizePath(const std::string& path) {
    return AssetRegistry::toAssetKey(path);
}

std::optional<std::vector<uint8_t>> AssetRegistry::readBytes(std::string_view assetKey) {
    auto* src = instance().source();
    if (!src) { pushReadLog(assetKey, nullptr, nullptr); return std::nullopt; }
    auto result = src->readBytes(assetKey);
    pushReadLog(assetKey, &result, nullptr);
    return result;
}

std::optional<std::string> AssetRegistry::readText(std::string_view assetKey) {
    auto* src = instance().source();
    if (!src) { pushReadLog(assetKey, nullptr, nullptr); return std::nullopt; }
    auto result = src->readText(assetKey);
    pushReadLog(assetKey, nullptr, &result);
    return result;
}

std::vector<AssetRegistry::ReadLogEntry> AssetRegistry::recentReads() {
    std::lock_guard lock(g_readLogMutex);
    // Return newest-first so /vfs_status output reads top-down naturally.
    return {g_readLog.rbegin(), g_readLog.rend()};
}

AssetHandle AssetRegistry::load(const std::string& path) {
    std::string canon = canonicalizePath(path);

    // Fast reject: already tried and failed
    if (failedPaths_.count(canon)) return AssetHandle{};

    // Check if already loaded or in-flight
    auto it = pathToIndex_.find(canon);
    if (it != pathToIndex_.end()) {
        auto& slot = slots_[it->second];
        // If an async load is in progress, wait for it and finalize so the
        // caller gets a Ready handle (sync load contract).
        if (slot.state == AssetState::Loading) {
            bool foundDecode = false;
            for (auto dit = activeDecodes_.begin(); dit != activeDecodes_.end(); ++dit) {
                if ((*dit)->slotIndex == it->second) {
                    foundDecode = true;
                    while (!(*dit)->complete.load(std::memory_order_acquire))
                        std::this_thread::yield();
                    // Finalize on this (main) thread
                    auto& req = *dit;
                    bool ok = false;
                    if (!req->failed) {
                        if (req->upload) {
                            slot.data = req->upload(req->result);
                            ok = (slot.data != nullptr);
                        } else {
                            slot.data = req->result;
                            ok = true;
                        }
                    }
                    if (ok) {
                        slot.state = AssetState::Ready;
                    } else {
                        if (!req->failed && req->upload && req->result && req->destroyDecoded)
                            req->destroyDecoded(req->result);
                        pathToIndex_.erase(slot.path);
                        slot.state = AssetState::Empty;
                        slot.path.clear();
                        slot.generation++;
                        freeList_.push_back(it->second);
                        activeDecodes_.erase(dit);
                        return AssetHandle{};
                    }
                    activeDecodes_.erase(dit);
                    break;
                }
            }
            // Invariant violation: Loading slot without a matching active
            // decode request. Treat as load failure — otherwise callers
            // get a "Ready"-looking handle backed by a never-populated slot.
            if (!foundDecode) {
                LOG_ERROR("AssetRegistry", "Loading slot %u (%s) has no active decode — marking failed",
                          it->second, canon.c_str());
                pathToIndex_.erase(slot.path);
                failedPaths_.insert(canon);
                slot.state = AssetState::Empty;
                slot.path.clear();
                slot.generation++;
                freeList_.push_back(it->second);
                return AssetHandle{};
            }
        }
        return AssetHandle::make(it->second, slot.generation);
    }

    // Find loader by extension
    const AssetLoader* loader = findLoader(canon);
    if (!loader) {
        LOG_ERROR("AssetRegistry", "No loader for: %s", canon.c_str());
        failedPaths_.insert(canon);
        return AssetHandle{};
    }

    // Allocate slot
    uint32_t idx = allocSlot();
    auto& slot = slots_[idx];
    slot.path = canon;
    slot.kind = loader->kind;
    slot.data = loader->load(canon);
    slot.state = slot.data ? AssetState::Ready : AssetState::Failed;

    if (slot.state != AssetState::Ready) {
        LOG_ERROR("AssetRegistry", "Failed to load: %s", canon.c_str());
        failedPaths_.insert(canon);
        slot.generation++;
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

// ---------------------------------------------------------------------------
// Async loading via fiber jobs
// ---------------------------------------------------------------------------

void AssetRegistry::asyncDecodeJobFunc(void* param) {
    auto* req = static_cast<AsyncDecodeRequest*>(param);
    req->result = req->decode(req->path);
    req->failed = (req->result == nullptr);
    req->complete.store(true, std::memory_order_release);
}

AssetHandle AssetRegistry::loadAsync(const std::string& path) {
    std::string canon = canonicalizePath(path);

    // Already loaded or in-flight?
    auto it = pathToIndex_.find(canon);
    if (it != pathToIndex_.end()) {
        auto& slot = slots_[it->second];
        return AssetHandle::make(it->second, slot.generation);
    }

    // Find loader by extension
    const AssetLoader* loader = findLoader(canon);
    if (!loader) {
        LOG_ERROR("AssetRegistry", "No loader for: %s", canon.c_str());
        failedPaths_.insert(canon);
        return AssetHandle{};
    }

    // If loader has no async decode, fall back to synchronous load
    if (!loader->decode) {
        return load(canon);
    }

    // Allocate slot in Loading state
    uint32_t idx = allocSlot();
    auto& slot = slots_[idx];
    slot.path = canon;
    slot.kind = loader->kind;
    slot.state = AssetState::Loading;

    pathToIndex_[canon] = idx;

    // Create async request and submit fiber job
    auto req = std::make_unique<AsyncDecodeRequest>();
    req->slotIndex = idx;
    req->path = canon;
    req->decode = loader->decode;
    req->upload = loader->upload;
    req->destroyDecoded = loader->destroyDecoded;

    auto* rawReq = req.get();
    activeDecodes_.push_back(std::move(req));

    Job job;
    job.function = &asyncDecodeJobFunc;
    job.param = rawReq;
    JobSystem::instance().submitFireAndForget(&job, 1);

    return AssetHandle::make(idx, slot.generation);
}

void AssetRegistry::processAsyncLoads(int maxPerFrame) {
    int processed = 0;
    auto it = activeDecodes_.begin();
    while (it != activeDecodes_.end() && processed < maxPerFrame) {
        auto& req = *it;
        if (!req->complete.load(std::memory_order_acquire)) {
            ++it;
            continue;
        }

        auto& slot = slots_[req->slotIndex];
        bool ok = false;

        if (!req->failed) {
            if (req->upload) {
                // Main-thread GPU finalize — upload() consumes the decoded data
                slot.data = req->upload(req->result);
                ok = (slot.data != nullptr);
            } else {
                // No upload step: decoded result IS the final asset
                slot.data = req->result;
                ok = true;
            }
        }

        if (ok) {
            slot.state = AssetState::Ready;
            LOG_DEBUG("AssetRegistry", "Async loaded: %s", slot.path.c_str());
        } else {
            LOG_ERROR("AssetRegistry", "Async load failed: %s", slot.path.c_str());
            // Clean up decoded data if upload failed (decode succeeded but upload didn't)
            if (!req->failed && req->upload && req->result && req->destroyDecoded) {
                req->destroyDecoded(req->result);
            }
            pathToIndex_.erase(slot.path);
            slot.state = AssetState::Empty;
            slot.path.clear();
            slot.generation++;
            freeList_.push_back(req->slotIndex);
        }

        it = activeDecodes_.erase(it);
        ++processed;
    }
}

bool AssetRegistry::isReady(AssetHandle handle) const {
    if (!handle.valid()) return false;
    uint32_t idx = handle.index();
    if (idx >= slots_.size()) return false;
    auto& slot = slots_[idx];
    return slot.state == AssetState::Ready && slot.generation == handle.generation();
}

size_t AssetRegistry::pendingAsyncCount() const {
    return activeDecodes_.size();
}

AssetHandle AssetRegistry::find(const std::string& path) const {
    std::string canon = canonicalizePath(path);
    auto it = pathToIndex_.find(canon);
    if (it == pathToIndex_.end()) return AssetHandle{};
    return AssetHandle::make(it->second, slots_[it->second].generation);
}

void AssetRegistry::clear() {
    // Drain in-flight async decodes before destroying slots
    for (auto& req : activeDecodes_) {
        while (!req->complete.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        if (req->result) {
            if (req->upload && req->destroyDecoded) {
                req->destroyDecoded(req->result);
            } else if (!req->upload) {
                const AssetLoader* loader = findLoader(req->path);
                if (loader) loader->destroy(req->result);
            }
        }
    }
    activeDecodes_.clear();

    for (size_t i = 1; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        if (slot.state == AssetState::Ready && slot.data) {
            const AssetLoader* loader = findLoader(slot.path);
            if (loader) loader->destroy(slot.data);
            slot.data = nullptr;
            slot.state = AssetState::Empty;
        }
    }
    slots_.resize(1); // keep slot 0
    slots_[0] = AssetSlot{};
    freeList_.clear();
    pathToIndex_.clear();
    failedPaths_.clear();
    loaders_.clear();
    {
        std::lock_guard lock(reloadQueueMutex_);
        pendingReloads_.clear();
    }
    // Tear down the asset source while the logger is still alive — VfsSource's
    // PhysFS deinit logs on failure. Static singleton destruction order would
    // otherwise be undefined.
    source_.reset();
}

void AssetRegistry::setSource(std::unique_ptr<IAssetSource> source) {
    source_ = std::move(source);
}

IAssetSource* AssetRegistry::source() {
    // Lazy default: tests, tools, and any caller that didn't go through
    // App::init / ServerApp::init still get a working DirectFsSource.
    // Callers that need VFS install explicitly via setSource at startup
    // before any asset reads happen — that always wins because it runs
    // first.
    if (!source_) {
        source_ = std::make_unique<DirectFsSource>();
    }
    return source_.get();
}

const IAssetSource* AssetRegistry::source() const {
    return source_.get();
}

size_t AssetRegistry::assetCount() const {
    size_t count = 0;
    for (size_t i = 1; i < slots_.size(); ++i) {
        if (slots_[i].state == AssetState::Ready) ++count;
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
        // Reset slot data but preserve generation so old handles remain stale
        auto& slot = slots_[idx];
        slot.path.clear();
        slot.data = nullptr;
        slot.state = AssetState::Empty;
        slot.kind = {};
        return idx;
    }
    uint32_t idx = static_cast<uint32_t>(slots_.size());
    slots_.push_back(AssetSlot{});
    return idx;
}

} // namespace fate
