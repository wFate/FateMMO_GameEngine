#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fate {

// Phase 1 of VFS integration. Read-only abstract source for asset bytes.
// Future phases will route every asset loader, scene loader, audio loader,
// etc. through this interface so the implementation (direct disk vs. mounted
// PhysicsFS pack) is swappable at startup without touching consumers.
//
// Path semantics (the *asset key*): forward-slash, relative, identical to
// what the engine uses today on disk — e.g. "assets/textures/foo.png",
// "assets/scenes/Beach.json". Implementations decide how to physically
// resolve the key. Do NOT pass absolute disk paths through this API.
//
// Thread safety: read methods (readBytes / readText / exists) MUST be safe
// to call from worker threads (asset decode runs on fiber-job workers).
// Construction/mount happens on the main thread at startup.
class IAssetSource {
public:
    virtual ~IAssetSource() = default;

    virtual std::optional<std::vector<uint8_t>> readBytes(std::string_view assetKey) = 0;

    // Default impl wraps readBytes; override only if the backend has a
    // cheaper text path.
    virtual std::optional<std::string> readText(std::string_view assetKey);

    virtual bool exists(std::string_view assetKey) = 0;

    // List the immediate children of a directory key. Returns just the
    // entry names (no path prefix), matching PHYSFS_enumerateFiles. Empty
    // result means "missing dir" or "empty dir" — callers can't tell them
    // apart, which matches PhysFS semantics. Used by audio SFX dir-load.
    virtual std::vector<std::string> listFiles(std::string_view dirKey) = 0;

    // True iff dirKey resolves to a directory (loose-fs entry or PhysFS
    // mount-pointed virtual dir). Used by callers that need to recurse —
    // listFiles is shallow, so consumers walk it themselves.
    virtual bool isDirectory(std::string_view dirKey) = 0;

    // If the source can map an asset key back to a real on-disk file, it
    // returns the path. Archive-only / .pak sources return nullopt.
    // Used by editor inspectors and the hot-reload watcher's reverse lookup.
    virtual std::optional<std::string> resolveToDisk(std::string_view assetKey) {
        (void)assetKey;
        return std::nullopt;
    }

    // Stable name used in logs ("DirectFS" / "VFS").
    virtual const char* name() const = 0;
};

// Direct-filesystem implementation. Preserves the engine's current behavior:
// keys like "assets/foo.png" resolve relative to CWD (project root). When
// rootDir is non-empty, it is prepended to relative keys.
class DirectFsSource : public IAssetSource {
public:
    explicit DirectFsSource(std::string rootDir = {});

    std::optional<std::vector<uint8_t>> readBytes(std::string_view assetKey) override;
    bool exists(std::string_view assetKey) override;
    std::vector<std::string> listFiles(std::string_view dirKey) override;
    bool isDirectory(std::string_view dirKey) override;
    std::optional<std::string> resolveToDisk(std::string_view assetKey) override;
    const char* name() const override { return "DirectFS"; }

    const std::string& rootDir() const { return rootDir_; }

private:
    std::string resolve(std::string_view assetKey) const;
    std::string rootDir_;
};

// PhysicsFS-backed implementation. Owns a VirtualFS and its mount table.
// Mount loose asset directories and/or .pak archives, then install via
// AssetRegistry::setSource().
class VfsSource : public IAssetSource {
public:
    // appName is forwarded to PHYSFS_init (used for user-dir resolution).
    explicit VfsSource(const char* appName);
    ~VfsSource() override;

    VfsSource(const VfsSource&) = delete;
    VfsSource& operator=(const VfsSource&) = delete;

    // Mount a directory or archive.
    //   appendToSearch=true  → appended (lower priority, fallback)
    //   appendToSearch=false → prepended (higher priority, overlay)
    bool mount(const std::string& realPath, const std::string& mountPoint = "",
               bool appendToSearch = true);

    bool initialized() const { return initialized_; }

    std::optional<std::vector<uint8_t>> readBytes(std::string_view assetKey) override;
    std::optional<std::string> readText(std::string_view assetKey) override;
    bool exists(std::string_view assetKey) override;
    std::vector<std::string> listFiles(std::string_view dirKey) override;
    bool isDirectory(std::string_view dirKey) override;
    std::optional<std::string> resolveToDisk(std::string_view assetKey) override;
    const char* name() const override { return "VFS"; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool initialized_ = false;
};

} // namespace fate
