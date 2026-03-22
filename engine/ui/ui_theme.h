#pragma once
#include "engine/ui/ui_style.h"
#include <string>
#include <unordered_map>

namespace fate {

class UITheme {
public:
    bool loadFromFile(const std::string& path);
    bool loadFromString(const std::string& jsonStr);

    bool hasStyle(const std::string& name) const;
    const UIStyle& getStyle(const std::string& name) const;

    void setStyle(const std::string& name, const UIStyle& style);

    // Editor: list all style names
    std::vector<std::string> styleNames() const {
        std::vector<std::string> names;
        names.reserve(styles_.size());
        for (auto& [k, v] : styles_) names.push_back(k);
        return names;
    }

private:
    std::unordered_map<std::string, UIStyle> styles_;
    static const UIStyle defaultStyle_;

    bool parseJson(const std::string& jsonStr);
};

} // namespace fate
