#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include <vector>

namespace fate {

struct GuildMemberInfo {
    std::string name;
    int level = 1;
    std::string rank;  // "Leader", "Officer", "Member"
    bool online = false;
};

class GuildPanel : public UINode {
public:
    GuildPanel(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;

    std::string guildName;
    int guildLevel = 1;
    int memberCount = 0;
    std::vector<GuildMemberInfo> members;
    float scrollOffset = 0.0f;

    UIClickCallback onClose;
};

} // namespace fate
