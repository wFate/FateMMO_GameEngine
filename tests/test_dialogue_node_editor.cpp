#include <doctest/doctest.h>
#include "engine/editor/node_editor.h"
#include <nlohmann/json.hpp>

using namespace fate;
using json = nlohmann::json;

TEST_CASE("DialogueNodeEditor save/load round trip") {
    DialogueNodeEditor editor;

    // Create test data via JSON
    json input = {
        {"nodes", json::array({
            {{"id", 1}, {"speaker", "NPC"}, {"text", "Hello!"},
             {"choices", json::array({
                 {{"text", "Hi"}, {"nextNodeId", 2}},
                 {{"text", "Bye"}, {"nextNodeId", -1}}
             })}},
            {{"id", 2}, {"speaker", "NPC"}, {"text", "How are you?"},
             {"choices", json::array({
                 {{"text", "Fine"}, {"nextNodeId", -1}}
             })}}
        })}
    };

    editor.loadFromJson(input);
    auto output = editor.saveToJson();

    REQUIRE(output.contains("nodes"));
    CHECK(output["nodes"].size() == 2);
    CHECK(output["nodes"][0]["speaker"] == "NPC");
    CHECK(output["nodes"][0]["text"] == "Hello!");
    CHECK(output["nodes"][0]["choices"].size() == 2);
    CHECK(output["nodes"][0]["choices"][0]["nextNodeId"] == 2);
}

TEST_CASE("DialogueNodeEditor empty save") {
    DialogueNodeEditor editor;
    auto output = editor.saveToJson();
    CHECK(output["nodes"].size() == 0);
}

TEST_CASE("DialogueNodeEditor preserves speaker and text across round trip") {
    DialogueNodeEditor editor;

    json input = {
        {"nodes", json::array({
            {{"id", 5}, {"speaker", "Guard"}, {"text", "Halt! Who goes there?"},
             {"choices", json::array({
                 {{"text", "A friend"}, {"nextNodeId", -1}}
             })}}
        })}
    };

    editor.loadFromJson(input);
    auto output = editor.saveToJson();

    CHECK(output["nodes"][0]["id"] == 5);
    CHECK(output["nodes"][0]["speaker"] == "Guard");
    CHECK(output["nodes"][0]["text"] == "Halt! Who goes there?");
    CHECK(output["nodes"][0]["choices"][0]["text"] == "A friend");
}

TEST_CASE("DialogueNodeEditor load clears previous data") {
    DialogueNodeEditor editor;

    json first = {
        {"nodes", json::array({
            {{"id", 1}, {"speaker", "A"}, {"text", "First"},
             {"choices", json::array()}}
        })}
    };
    editor.loadFromJson(first);
    CHECK(editor.saveToJson()["nodes"].size() == 1);

    json second = {
        {"nodes", json::array({
            {{"id", 10}, {"speaker", "B"}, {"text", "Second"},
             {"choices", json::array()}},
            {{"id", 11}, {"speaker", "C"}, {"text", "Third"},
             {"choices", json::array()}}
        })}
    };
    editor.loadFromJson(second);
    auto output = editor.saveToJson();
    CHECK(output["nodes"].size() == 2);
    CHECK(output["nodes"][0]["speaker"] == "B");
    CHECK(output["nodes"][1]["speaker"] == "C");
}
