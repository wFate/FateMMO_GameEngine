#pragma once
#include "engine/ui/ui_node.h"
#include <deque>
#include <string>

namespace fate {

class ChatTicker : public UINode {
public:
    ChatTicker(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;

    void addMessage(const std::string& msg);

    float scrollSpeed    = 40.0f;  // pixels per second
    float scrollOffset   = 0.0f;
    float lastUpdateTime = 0.0f;

private:
    std::deque<std::string> messages_;
    static constexpr size_t MAX_MESSAGES = 50;
    std::string currentDisplay_;
};

} // namespace fate
