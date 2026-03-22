#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace fate {

// Visual node editor for dialogue trees
class DialogueNodeEditor {
public:
    void init();
    void shutdown();
    void draw();  // Call each frame from editor UI

    void loadFromJson(const nlohmann::json& data);
    nlohmann::json saveToJson() const;

    void loadFromFile(const std::string& path);
    void saveToFile(const std::string& path);

    bool isOpen() const { return open_; }
    void setOpen(bool o) { open_ = o; }

private:
    bool open_ = false;
    bool initialized_ = false;

    // Node data matching DialogueNode structure
    struct EditorNode {
        int id = 0;
        std::string speakerName;
        std::string text;
        float posX = 0, posY = 0; // editor position

        struct Choice {
            int id = 0;          // unique choice ID for imnodes link source
            std::string text;
            int nextNodeId = -1; // target node ID (-1 = end)
        };
        std::vector<Choice> choices;
    };

    std::vector<EditorNode> nodes_;
    int nextNodeId_ = 1;
    int nextChoiceId_ = 1000;
    int nextLinkId_ = 10000;

    // Track links: linkId -> {choiceAttrId, targetAttrId}
    struct LinkInfo {
        int id;            // link ID for imnodes
        int choiceAttrId;  // output pin attribute ID
        int targetAttrId;  // input pin attribute ID
    };
    std::vector<LinkInfo> links_;

    std::string currentFilePath_;

    void drawMenuBar();
    void rebuildLinks();
    int allocNodeId() { return nextNodeId_++; }
    int allocChoiceId() { return nextChoiceId_++; }
    int allocLinkId() { return nextLinkId_++; }

    // Attribute ID helpers: input attr = nodeId * 100, output attr = choiceId
    static int inputAttrId(int nodeId) { return nodeId * 100; }
};

} // namespace fate
