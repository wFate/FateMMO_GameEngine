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

struct TrainableSkill {
    std::string skillId;
    uint16_t requiredLevel = 0;
    int64_t goldCost = 0;
    uint16_t skillPointCost = 0;
    ClassType requiredClass = ClassType::Any;
};

struct TeleportDestination {
    std::string destinationName;
    std::string sceneId;
    Vec2 targetPosition;
    int64_t cost = 0;
    uint16_t requiredLevel = 0;
};

struct NPCTemplate {
    std::string name;
    uint32_t npcId = 0;
    Vec2 position;
    FaceDirection facing = FaceDirection::Down;
    std::string spriteSheet;

    bool isQuestGiver = false;
    bool isMerchant = false;
    bool isSkillTrainer = false;
    bool isBanker = false;
    bool isGuildNPC = false;
    bool isTeleporter = false;
    bool isStoryNPC = false;

    std::vector<uint32_t> questIds;
    std::vector<ShopItem> shopItems;
    std::string shopName;
    std::vector<TrainableSkill> trainableSkills;
    ClassType trainerClass = ClassType::Any;
    uint16_t bankSlots = 30;
    float bankFeePercent = 0.0f;
    int64_t guildCreationCost = 0;
    uint16_t guildRequiredLevel = 0;
    std::vector<TeleportDestination> destinations;
    std::vector<DialogueNode> dialogueTree;
    uint32_t dialogueRootNodeId = 0;

    std::string dialogueGreeting;
    float interactionRadius = 2.0f;
};

} // namespace fate
