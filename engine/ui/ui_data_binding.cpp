#include "engine/ui/ui_data_binding.h"

namespace fate {

std::string UIDataBinding::getValue(const std::string& path) const {
    if (!provider_) return "";
    return provider_(path);
}

std::string UIDataBinding::resolve(const std::string& templateStr) const {
    if (!provider_) return templateStr;

    std::string result;
    result.reserve(templateStr.size());
    size_t i = 0;
    while (i < templateStr.size()) {
        if (templateStr[i] == '{') {
            size_t end = templateStr.find('}', i + 1);
            if (end != std::string::npos) {
                std::string path = templateStr.substr(i + 1, end - i - 1);
                result += provider_(path);
                i = end + 1;
            } else {
                result += templateStr[i++];
            }
        } else {
            result += templateStr[i++];
        }
    }
    return result;
}

} // namespace fate
