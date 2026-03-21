#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

class VirtualFS {
public:
    VirtualFS() = default;
    ~VirtualFS();

    // Non-copyable, movable
    VirtualFS(const VirtualFS&) = delete;
    VirtualFS& operator=(const VirtualFS&) = delete;
    VirtualFS(VirtualFS&& other) noexcept;
    VirtualFS& operator=(VirtualFS&& other) noexcept;

    bool init(const char* appName);
    void shutdown();

    // Mount a directory or archive at the given virtual mount point.
    // appendToSearchPath: if true, appended to end of search path (lower priority).
    //                     if false, prepended (higher priority / overlay).
    bool mount(const std::string& path, const std::string& mountPoint,
               bool appendToSearchPath = true);

    [[nodiscard]] std::optional<std::vector<uint8_t>> readFile(const std::string& path) const;
    [[nodiscard]] std::optional<std::string> readText(const std::string& path) const;
    [[nodiscard]] bool exists(const std::string& path) const;
    [[nodiscard]] std::vector<std::string> listDir(const std::string& dir) const;

private:
    bool initialized_ = false;
};
