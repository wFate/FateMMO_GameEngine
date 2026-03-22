#include "engine/ui/ui_theme.h"
#include "engine/core/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace fate {

const UIStyle UITheme::defaultStyle_{};

bool UITheme::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("UI", "Failed to open theme file: %s", path.c_str());
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return parseJson(content);
}

bool UITheme::loadFromString(const std::string& jsonStr) {
    return parseJson(jsonStr);
}

bool UITheme::hasStyle(const std::string& name) const {
    return styles_.count(name) > 0;
}

const UIStyle& UITheme::getStyle(const std::string& name) const {
    auto it = styles_.find(name);
    if (it != styles_.end()) return it->second;
    return defaultStyle_;
}

void UITheme::setStyle(const std::string& name, const UIStyle& style) {
    styles_[name] = style;
}

static Color parseColor(const nlohmann::json& j) {
    if (j.is_array() && j.size() >= 3) {
        float r = j[0].get<float>();
        float g = j[1].get<float>();
        float b = j[2].get<float>();
        float a = (j.size() >= 4) ? j[3].get<float>() : 1.0f;
        return {r, g, b, a};
    }
    return Color::white();
}

static NineSlice parseNineSlice(const nlohmann::json& j) {
    if (j.is_array() && j.size() >= 4) {
        return {j[0].get<float>(), j[1].get<float>(),
                j[2].get<float>(), j[3].get<float>()};
    }
    return {};
}

bool UITheme::parseJson(const std::string& jsonStr) {
    try {
        auto data = nlohmann::json::parse(jsonStr);
        if (!data.contains("styles") || !data["styles"].is_object()) {
            LOG_ERROR("UI", "Theme JSON missing 'styles' object");
            return false;
        }

        for (auto& [name, obj] : data["styles"].items()) {
            UIStyle style;
            style.styleName = name;

            if (obj.contains("backgroundColor"))
                style.backgroundColor = parseColor(obj["backgroundColor"]);
            if (obj.contains("borderColor"))
                style.borderColor = parseColor(obj["borderColor"]);
            if (obj.contains("borderWidth"))
                style.borderWidth = obj["borderWidth"].get<float>();
            if (obj.contains("backgroundTexture"))
                style.backgroundTexture = obj["backgroundTexture"].get<std::string>();
            if (obj.contains("nineSlice"))
                style.nineSlice = parseNineSlice(obj["nineSlice"]);
            if (obj.contains("fontName"))
                style.fontName = obj["fontName"].get<std::string>();
            if (obj.contains("fontSize"))
                style.fontSize = obj["fontSize"].get<float>();
            if (obj.contains("textColor"))
                style.textColor = parseColor(obj["textColor"]);
            if (obj.contains("hoverColor"))
                style.hoverColor = parseColor(obj["hoverColor"]);
            if (obj.contains("pressedColor"))
                style.pressedColor = parseColor(obj["pressedColor"]);
            if (obj.contains("disabledColor"))
                style.disabledColor = parseColor(obj["disabledColor"]);
            if (obj.contains("opacity"))
                style.opacity = obj["opacity"].get<float>();

            styles_[name] = style;
        }

        LOG_INFO("UI", "Loaded theme with %zu styles", styles_.size());
        return true;
    }
    catch (const nlohmann::json::exception& e) {
        LOG_ERROR("UI", "Theme JSON parse error: %s", e.what());
        return false;
    }
}

} // namespace fate
