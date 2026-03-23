#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include "game/shared/npc_types.h"
#include <functional>
#include <vector>

namespace fate {

class TeleporterPanel : public UINode {
public:
    TeleporterPanel(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;
    bool onKeyInput(int scancode, bool pressed) override;
    void update(float dt);

    // Data (set by GameApp each frame)
    struct Destination {
        std::string name;
        std::string sceneId;
        Vec2 position;
        int64_t cost = 0;
        uint16_t requiredLevel = 0;
    };
    std::vector<Destination> destinations;
    int64_t playerGold = 0;
    uint16_t playerLevel = 0;
    std::string title = "Teleporter";

    // Error display
    std::string errorMessage;
    float errorTimer = 0.0f;

    // Callbacks
    std::function<void(uint32_t npcId, uint8_t destIndex)> onTeleport;
    UIClickCallback onClose;

    uint32_t npcId = 0;

    void open(uint32_t npcId, const std::vector<TeleportDestination>& dests,
              int64_t gold, uint16_t level);
    void close();
    bool isOpen() const { return visible(); }

private:
    void rebuild();
    float scrollOffset_ = 0.0f;
};

} // namespace fate
