#include "engine/core/atomic_write.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace fate {

namespace fs = std::filesystem;

bool writeFileAtomic(const std::string& path,
                     std::string_view content,
                     std::string* errorOut) {
    auto setErr = [errorOut](std::string msg) {
        if (errorOut) *errorOut = std::move(msg);
    };

    if (path.empty()) {
        setErr("empty target path");
        return false;
    }

    fs::path target(path);

    // Create parent directory if needed. fs::create_directories is a no-op
    // when the directory already exists; an error here means we cannot stage
    // the write at all.
    auto parent = target.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        if (!fs::exists(parent, ec)) {
            fs::create_directories(parent, ec);
            if (ec) {
                setErr("create_directories failed: " + ec.message());
                return false;
            }
        }
    }

    fs::path tmp = target;
    tmp += ".tmp";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            setErr("open .tmp failed");
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out.good()) {
            std::error_code ec;
            fs::remove(tmp, ec);
            setErr("write to .tmp failed");
            return false;
        }
    }

    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (!ec) return true;

    // Cross-volume rename or destination locked — fall back to copy + remove.
    fs::copy_file(tmp, target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::error_code rmEc;
        fs::remove(tmp, rmEc);
        setErr("rename and copy_file fallback failed: " + ec.message());
        return false;
    }
    std::error_code rmEc;
    fs::remove(tmp, rmEc);
    return true;
}

} // namespace fate
