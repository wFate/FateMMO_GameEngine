// Standalone migration tool: reads QuestData::getAllQuests() and emits one JSON file
// per quest under assets/dialogue/quests/<id>.json.
//
// Run once. Refuses to overwrite existing non-stub JSON.

#include "game/shared/quest_data.h"
#include "game/shared/dialogue_registry.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace fate;

static std::string hintTagForLevel(int level) {
    if (level <= 10) return "explicit";
    if (level <= 25) return "partial";
    if (level <= 40) return "cryptic";
    return "mystery";
}

static nlohmann::json buildOfferTree(const QuestDefinition& q) {
    nlohmann::json tree;
    tree["rootNodeId"] = 0;
    nlohmann::json n0;
    n0["id"] = 0;
    n0["npcText"] = q.offerDialogue.empty() ? std::string("I have work for you.") : q.offerDialogue;
    nlohmann::json cAccept;
    cAccept["text"]   = "Accept";
    cAccept["hint"]   = hintTagForLevel(q.requiredLevel);
    cAccept["action"] = {{"type", "AcceptQuest"}, {"value", static_cast<int64_t>(q.questId)}};
    nlohmann::json cLeave;
    cLeave["text"]   = "Not now.";
    cLeave["action"] = {{"type", "EndDialogue"}};
    nlohmann::json choicesArr = nlohmann::json::array();
    choicesArr.push_back(cAccept);
    choicesArr.push_back(cLeave);
    n0["choices"] = choicesArr;
    nlohmann::json nodesArr = nlohmann::json::array();
    nodesArr.push_back(n0);
    tree["nodes"] = nodesArr;
    return tree;
}

static nlohmann::json buildInProgressTree(const QuestDefinition& q) {
    nlohmann::json tree;
    tree["rootNodeId"] = 0;
    nlohmann::json n0;
    n0["id"] = 0;
    n0["npcText"] = q.inProgressDialogue.empty() ? std::string("Still working on it?") : q.inProgressDialogue;
    nlohmann::json cOk;
    cOk["text"]   = "Still working on it.";
    cOk["action"] = {{"type", "EndDialogue"}};
    nlohmann::json choicesArr = nlohmann::json::array();
    choicesArr.push_back(cOk);
    n0["choices"] = choicesArr;
    nlohmann::json nodesArr = nlohmann::json::array();
    nodesArr.push_back(n0);
    tree["nodes"] = nodesArr;
    return tree;
}

static nlohmann::json buildTurnInTree(const QuestDefinition& q) {
    nlohmann::json tree;
    tree["rootNodeId"] = 0;
    nlohmann::json n0;
    n0["id"] = 0;
    n0["npcText"] = q.turnInDialogue.empty() ? std::string("Let me see it.") : q.turnInDialogue;
    nlohmann::json cTurnIn;
    cTurnIn["text"]   = "Turn in.";
    cTurnIn["action"] = {{"type", "CompleteQuest"}, {"value", static_cast<int64_t>(q.questId)}};
    nlohmann::json choicesArr = nlohmann::json::array();
    choicesArr.push_back(cTurnIn);
    n0["choices"] = choicesArr;
    nlohmann::json nodesArr = nlohmann::json::array();
    nodesArr.push_back(n0);
    tree["nodes"] = nodesArr;
    return tree;
}

int main(int argc, char** argv) {
    std::string assetsRoot = (argc > 1) ? argv[1] : "assets";
    fs::path questsDir = fs::path(assetsRoot) / "dialogue" / "quests";

    // Safety: refuse if directory already has JSON content.
    if (fs::exists(questsDir) && fs::is_directory(questsDir)) {
        for (const auto& e : fs::directory_iterator(questsDir)) {
            if (e.is_regular_file() && e.path().extension() == ".json") {
                std::cerr << "ERROR: " << questsDir.string() << " already contains JSON files.\n"
                          << "Migration refuses to overwrite. Remove them manually if intentional.\n";
                return 2;
            }
        }
    }

    fs::create_directories(questsDir);

    int questsWritten = 0;
    const auto& quests = QuestData::getAllQuests();
    for (const auto& kv : quests) {
        const auto& q = kv.second;
        nlohmann::json out;
        out["schemaVersion"] = 1;
        out["questId"] = q.questId;
        nlohmann::json trees;
        trees["offer"]      = buildOfferTree(q);
        trees["inProgress"] = buildInProgressTree(q);
        trees["turnIn"]     = buildTurnInTree(q);
        out["trees"] = trees;
        out["legacyOfferDialogue"]      = q.offerDialogue;
        out["legacyInProgressDialogue"] = q.inProgressDialogue;
        out["legacyTurnInDialogue"]     = q.turnInDialogue;

        fs::path p = questsDir / (std::to_string(q.questId) + ".json");
        std::ofstream f(p);
        f << out.dump(2);
        ++questsWritten;
    }

    // NOTE: NPC framing trees are authored per content session, not bulk-migrated.
    // Until authored, NPCs without a tree fall back to the existing non-story
    // quest/role pill path in NpcDialoguePanel. Quest subtrees are served via
    // DialogueRegistry::getQuestTree().

    std::ofstream report(fs::path(assetsRoot) / "dialogue" / "migration_report.txt");
    report << "Quests written: " << questsWritten << "\n";
    report << "NPC stubs written: 0 (deferred to content sessions)\n";

    std::cout << "Migration done: " << questsWritten << " quests\n";
    return 0;
}
