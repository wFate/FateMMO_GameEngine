#pragma once
#include <string>
#include <nlohmann/json_fwd.hpp>

namespace fate {

class UINode;
enum class AnchorPreset : uint8_t;

class UISerializer {
public:
    static std::string serializeScreen(const std::string& screenId, const UINode* root);
    static bool saveToFile(const std::string& filepath, const std::string& screenId, const UINode* root);

private:
    static nlohmann::json serializeNode(const UINode* node);
    static std::string presetToString(AnchorPreset preset);
};

} // namespace fate
