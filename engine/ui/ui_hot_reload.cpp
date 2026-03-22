#include "engine/ui/ui_hot_reload.h"
#include "engine/core/logger.h"
#include <filesystem>

namespace fate {

void UIHotReload::watchFile(const std::string& filepath) {
    FileInfo info;
    info.lastModTime = getFileModTime(filepath);
    watchedFiles_[filepath] = info;
}

std::vector<std::string> UIHotReload::checkForChanges() {
    std::vector<std::string> changed;
    for (auto& [path, info] : watchedFiles_) {
        uint64_t modTime = getFileModTime(path);
        if (modTime != 0 && modTime != info.lastModTime) {
            info.lastModTime = modTime;
            changed.push_back(path);
        }
    }
    return changed;
}

uint64_t UIHotReload::getFileModTime(const std::string& path) {
    try {
        auto ftime = std::filesystem::last_write_time(path);
        return static_cast<uint64_t>(ftime.time_since_epoch().count());
    } catch (...) {
        return 0;
    }
}

} // namespace fate
