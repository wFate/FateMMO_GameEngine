#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace fate {

class UIHotReload {
public:
    void watchFile(const std::string& filepath);
    std::vector<std::string> checkForChanges();

private:
    struct FileInfo {
        uint64_t lastModTime = 0;
    };
    std::unordered_map<std::string, FileInfo> watchedFiles_;

    static uint64_t getFileModTime(const std::string& path);
};

} // namespace fate
