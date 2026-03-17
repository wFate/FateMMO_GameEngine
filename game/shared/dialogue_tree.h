#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fate {

enum class DialogueAction : uint8_t {
    None = 0,
    GiveItem,
    GiveXP,
    GiveGold,
    SetFlag,
    Heal
};

struct DialogueActionData {
    DialogueAction action = DialogueAction::None;
    std::string targetId;
    int32_t value = 0;
};

enum class DialogueCondition : uint8_t {
    None = 0,
    HasFlag,
    MinLevel,
    HasItem,
    HasClass
};

struct DialogueConditionData {
    DialogueCondition condition = DialogueCondition::None;
    std::string targetId;
    int32_t value = 0;
};

struct DialogueChoice {
    std::string buttonText;
    uint32_t nextNodeId = 0;
    DialogueActionData onSelect;
};

struct DialogueNode {
    uint32_t nodeId = 0;
    std::string npcText;
    std::vector<DialogueChoice> choices;
    DialogueConditionData condition;
};

} // namespace fate
