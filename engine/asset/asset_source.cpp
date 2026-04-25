#include "engine/asset/asset_source.h"
#include "engine/vfs/virtual_fs.h"
#include "engine/core/logger.h"

#include <physfs.h>

#include <cstdio>
#include <filesystem>

namespace fate {

std::optional<std::string> IAssetSource::readText(std::string_view assetKey) {
    auto bytes = readBytes(assetKey);
    if (!bytes) return std::nullopt;
    return std::string(bytes->begin(), bytes->end());
}

// ---- DirectFsSource ---------------------------------------------------------

DirectFsSource::DirectFsSource(std::string rootDir) : rootDir_(std::move(rootDir)) {}

std::string DirectFsSource::resolve(std::string_view assetKey) const {
    namespace fs = std::filesystem;
    if (rootDir_.empty()) return std::string(assetKey);
    fs::path p(assetKey);
    if (p.is_absolute()) return std::string(assetKey);
    return (fs::path(rootDir_) / p).string();
}

std::optional<std::vector<uint8_t>> DirectFsSource::readBytes(std::string_view assetKey) {
    std::string full = resolve(assetKey);
    std::FILE* f = std::fopen(full.c_str(), "rb");
    if (!f) return std::nullopt;
    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return std::nullopt; }
    long size = std::ftell(f);
    if (size < 0) { std::fclose(f); return std::nullopt; }
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    size_t n = (size > 0) ? std::fread(buf.data(), 1, buf.size(), f) : 0;
    std::fclose(f);
    if (n != buf.size()) return std::nullopt;
    return buf;
}

bool DirectFsSource::exists(std::string_view assetKey) {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(resolve(assetKey), ec);
}

std::optional<std::string> DirectFsSource::resolveToDisk(std::string_view assetKey) {
    return resolve(assetKey);
}

std::vector<std::string> DirectFsSource::listFiles(std::string_view dirKey) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    fs::path dir(resolve(dirKey));
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        out.push_back(entry.path().filename().string());
    }
    return out;
}

bool DirectFsSource::isDirectory(std::string_view dirKey) {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::is_directory(resolve(dirKey), ec);
}

// ---- VfsSource --------------------------------------------------------------

struct VfsSource::Impl {
    ::VirtualFS vfs;
};

VfsSource::VfsSource(const char* appName)
    : impl_(std::make_unique<Impl>()) {
    initialized_ = impl_->vfs.init(appName);
    if (!initialized_) {
        LOG_ERROR("VfsSource", "PhysFS init failed for app '%s'", appName ? appName : "(null)");
    }
}

VfsSource::~VfsSource() = default;

bool VfsSource::mount(const std::string& realPath, const std::string& mountPoint,
                      bool appendToSearch) {
    if (!initialized_) return false;
    return impl_->vfs.mount(realPath, mountPoint, appendToSearch);
}

std::optional<std::vector<uint8_t>> VfsSource::readBytes(std::string_view assetKey) {
    if (!initialized_) return std::nullopt;
    return impl_->vfs.readFile(std::string(assetKey));
}

std::optional<std::string> VfsSource::readText(std::string_view assetKey) {
    if (!initialized_) return std::nullopt;
    return impl_->vfs.readText(std::string(assetKey));
}

bool VfsSource::exists(std::string_view assetKey) {
    if (!initialized_) return false;
    return impl_->vfs.exists(std::string(assetKey));
}

std::vector<std::string> VfsSource::listFiles(std::string_view dirKey) {
    if (!initialized_) return {};
    return impl_->vfs.listDir(std::string(dirKey));
}

bool VfsSource::isDirectory(std::string_view dirKey) {
    if (!initialized_) return false;
    return PHYSFS_isDirectory(std::string(dirKey).c_str()) != 0;
}

std::optional<std::string> VfsSource::resolveToDisk(std::string_view assetKey) {
    if (!initialized_) return std::nullopt;
    // PHYSFS_getRealDir returns the mount root that contains the file. For
    // loose-directory mounts that gives us the on-disk parent — combine with
    // the asset key to recover the original disk path. Archive-only entries
    // return nullopt because no disk file exists.
    std::string keyStr(assetKey);
    const char* realDir = PHYSFS_getRealDir(keyStr.c_str());
    if (!realDir || !*realDir) return std::nullopt;
    namespace fs = std::filesystem;
    fs::path p = fs::path(realDir) / fs::path(assetKey);
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return std::nullopt; // probably inside a .pak
    return p.string();
}

} // namespace fate
