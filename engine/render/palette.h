#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "engine/core/types.h"

namespace fate {

class PaletteRegistry {
public:
    using Palette = std::vector<Color>;

    bool loadFromJson(const std::string& jsonStr);
    bool loadFromFile(const std::string& path);

    [[nodiscard]] bool has(const std::string& name) const;
    [[nodiscard]] const Palette* get(const std::string& name) const;
    [[nodiscard]] std::vector<std::string> names() const;

private:
    std::unordered_map<std::string, Palette> palettes_;
};

} // namespace fate
