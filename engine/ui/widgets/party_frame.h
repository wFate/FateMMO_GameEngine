#pragma once
#include "engine/ui/ui_node.h"
#include <vector>

namespace fate {

struct PartyFrameMemberInfo {
    std::string name;
    float hp = 0, maxHp = 1;
    float mp = 0, maxMp = 1;
    int level = 1;
    bool isLeader = false;
};

class PartyFrame : public UINode {
public:
    PartyFrame(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;

    std::vector<PartyFrameMemberInfo> members;  // max 2 (excludes self, party max 3)
    float cardWidth  = 170.0f;
    float cardHeight = 48.0f;
    float cardSpacing = 4.0f;
};

} // namespace fate
