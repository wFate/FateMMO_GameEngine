#include <doctest/doctest.h>
#include "game/shared/dialogue_tree.h"

using namespace fate;

TEST_CASE("DialogueTree: create dialogue node with choices") {
    DialogueNode node;
    node.nodeId = 1;
    node.npcText = "Hello traveler!";
    node.choices.push_back({"Tell me about this town", 2, {}});
    node.choices.push_back({"Goodbye", 0, {}});

    CHECK(node.nodeId == 1);
    CHECK(node.npcText == "Hello traveler!");
    CHECK(node.choices.size() == 2);
    CHECK(node.choices[0].nextNodeId == 2);
    CHECK(node.choices[1].nextNodeId == 0);
}

TEST_CASE("DialogueTree: dialogue action data") {
    DialogueActionData action;
    action.action = DialogueAction::GiveItem;
    action.targetId = "potion_hp_small";
    action.value = 3;

    CHECK(action.action == DialogueAction::GiveItem);
    CHECK(action.targetId == "potion_hp_small");
    CHECK(action.value == 3);
}

TEST_CASE("DialogueTree: dialogue condition data") {
    DialogueConditionData cond;
    cond.condition = DialogueCondition::MinLevel;
    cond.value = 25;

    CHECK(cond.condition == DialogueCondition::MinLevel);
    CHECK(cond.value == 25);
}

TEST_CASE("DialogueTree: choice with action") {
    DialogueChoice choice;
    choice.buttonText = "Accept reward";
    choice.nextNodeId = 0;
    choice.onSelect.action = DialogueAction::GiveGold;
    choice.onSelect.value = 500;

    CHECK(choice.onSelect.action == DialogueAction::GiveGold);
    CHECK(choice.onSelect.value == 500);
}

TEST_CASE("DialogueTree: node with condition gate") {
    DialogueNode node;
    node.nodeId = 5;
    node.npcText = "You look experienced enough...";
    node.condition.condition = DialogueCondition::MinLevel;
    node.condition.value = 50;

    CHECK(node.condition.condition == DialogueCondition::MinLevel);
    CHECK(node.condition.value == 50);
}
