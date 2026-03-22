#pragma once
#include "engine/core/types.h"
#include <string>
#include <functional>

namespace fate {

struct DragPayload {
    std::string type;
    std::string data;
    std::string sourceId;
    bool active = false;
    void clear() { type.clear(); data.clear(); sourceId.clear(); active = false; }
};

using UIClickCallback = std::function<void(const std::string& nodeId)>;
using UITextCallback = std::function<void(const std::string& nodeId, const std::string& text)>;

} // namespace fate
