#include "engine/vfs/virtual_fs.h"
#include <physfs.h>
#include <spdlog/spdlog.h>

VirtualFS::~VirtualFS() {
    if (initialized_) {
        shutdown();
    }
}

VirtualFS::VirtualFS(VirtualFS&& other) noexcept
    : initialized_(other.initialized_) {
    other.initialized_ = false;
}

VirtualFS& VirtualFS::operator=(VirtualFS&& other) noexcept {
    if (this != &other) {
        if (initialized_) {
            shutdown();
        }
        initialized_ = other.initialized_;
        other.initialized_ = false;
    }
    return *this;
}

bool VirtualFS::init(const char* appName) {
    if (initialized_) {
        spdlog::warn("VirtualFS::init called while already initialized");
        return true;
    }

    if (!PHYSFS_init(appName)) {
        spdlog::error("VirtualFS: PHYSFS_init failed: {}",
                       PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return false;
    }

    initialized_ = true;
    PHYSFS_Version ver;
    PHYSFS_getLinkedVersion(&ver);
    spdlog::info("VirtualFS initialized (PhysicsFS {}.{}.{})",
                 ver.major, ver.minor, ver.patch);
    return true;
}

void VirtualFS::shutdown() {
    if (!initialized_) return;

    if (!PHYSFS_deinit()) {
        spdlog::error("VirtualFS: PHYSFS_deinit failed: {}",
                       PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    }
    initialized_ = false;
    spdlog::info("VirtualFS shut down");
}

bool VirtualFS::mount(const std::string& path, const std::string& mountPoint,
                      bool appendToSearchPath) {
    if (!initialized_) {
        spdlog::error("VirtualFS::mount called before init");
        return false;
    }

    if (!PHYSFS_mount(path.c_str(), mountPoint.c_str(), appendToSearchPath ? 1 : 0)) {
        spdlog::error("VirtualFS: mount '{}' at '{}' failed: {}",
                       path, mountPoint,
                       PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return false;
    }

    spdlog::info("VirtualFS: mounted '{}' at '/{}'", path, mountPoint);
    return true;
}

std::optional<std::vector<uint8_t>> VirtualFS::readFile(const std::string& path) const {
    if (!initialized_) return std::nullopt;

    PHYSFS_File* file = PHYSFS_openRead(path.c_str());
    if (!file) {
        return std::nullopt;
    }

    PHYSFS_sint64 length = PHYSFS_fileLength(file);
    if (length < 0) {
        PHYSFS_close(file);
        return std::nullopt;
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(length));
    PHYSFS_sint64 bytesRead = PHYSFS_readBytes(file, buffer.data(), static_cast<PHYSFS_uint64>(length));
    PHYSFS_close(file);

    if (bytesRead != length) {
        spdlog::error("VirtualFS: readFile '{}' expected {} bytes, got {}",
                       path, length, bytesRead);
        return std::nullopt;
    }

    return buffer;
}

std::optional<std::string> VirtualFS::readText(const std::string& path) const {
    auto data = readFile(path);
    if (!data) return std::nullopt;
    return std::string(data->begin(), data->end());
}

bool VirtualFS::exists(const std::string& path) const {
    if (!initialized_) return false;
    return PHYSFS_exists(path.c_str()) != 0;
}

std::vector<std::string> VirtualFS::listDir(const std::string& dir) const {
    std::vector<std::string> result;
    if (!initialized_) return result;

    char** files = PHYSFS_enumerateFiles(dir.c_str());
    if (!files) return result;

    for (char** i = files; *i != nullptr; ++i) {
        result.emplace_back(*i);
    }

    PHYSFS_freeList(files);
    return result;
}
