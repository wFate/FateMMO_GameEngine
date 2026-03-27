#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "game/shared/game_types.h"
#include "game/shared/dialogue_tree.h"
#include "engine/core/types.h"

namespace fate {

struct ShopItem {
    std::string itemId;
    std::string itemName;
    int64_t buyPrice = 0;
    int64_t sellPrice = 0;
    uint16_t stock = 0;   // 0 = unlimited
};

struct TeleportDestination {
    std::string destinationName;
    std::string sceneId;
    Vec2 targetPosition;
    int64_t cost = 0;
    uint16_t requiredLevel = 0;
    std::string requiredItem;     // item ID to consume (empty = no item required)
    uint16_t requiredItemQty = 0; // quantity to consume (0 = no item required)
};

struct NPCTemplate {
    std::string name;
    uint32_t npcId = 0;
    Vec2 position;
    FaceDirection facing = FaceDirection::Down;
    std::string spriteSheet;

    bool isQuestGiver = false;
    bool isMerchant = false;
    bool isBanker = false;
    bool isGuildNPC = false;
    bool isTeleporter = false;
    bool isStoryNPC = false;
    bool isDungeonNPC = false;
    bool isArenaNPC = false;
    bool isBattlefieldNPC = false;
    bool isMarketplaceNPC = false;
    bool isLeaderboardNPC = false;
    std::string dungeonSceneId;
    std::string leaderboardLoreSnippet;

    std::vector<uint32_t> questIds;
    std::vector<ShopItem> shopItems;
    std::string shopName;
    uint16_t bankSlots = 30;
    int64_t guildCreationCost = 0;
    uint16_t guildRequiredLevel = 0;
    std::vector<TeleportDestination> destinations;
    std::vector<DialogueNode> dialogueTree;
    uint32_t dialogueRootNodeId = 0;

    std::string dialogueGreeting;
    float interactionRadius = 2.0f;

    /// Check if a player position is within interaction range of this NPC.
    /// interactionRadius is in tiles; positions are in pixels (tiles * 32).
    bool isInRange(const Vec2& playerPos, const Vec2& npcPos) const {
        float dx = playerPos.x - npcPos.x;
        float dy = playerPos.y - npcPos.y;
        float distSq = dx * dx + dy * dy;
        float maxDist = interactionRadius * 32.0f; // convert tiles to pixels
        return distSq <= maxDist * maxDist;
    }
};

} // namespace fate
