#include "engine/editor/node_editor.h"
#include "engine/core/logger.h"

#include "imgui.h"
#include "imnodes.h"

#include <fstream>
#include <algorithm>
#include <cstring>

namespace fate {

void DialogueNodeEditor::init() {
    if (initialized_) return;
    ImNodes::CreateContext();
    ImNodes::StyleColorsDark();

    // Harmonize with editor palette
    ImNodesStyle& nodeStyle = ImNodes::GetStyle();
    nodeStyle.Colors[ImNodesCol_NodeBackground]       = IM_COL32(30, 30, 34, 255);
    nodeStyle.Colors[ImNodesCol_NodeBackgroundHovered] = IM_COL32(38, 38, 44, 255);
    nodeStyle.Colors[ImNodesCol_NodeBackgroundSelected]= IM_COL32(42, 45, 50, 255);
    nodeStyle.Colors[ImNodesCol_TitleBar]              = IM_COL32(42, 45, 50, 255);
    nodeStyle.Colors[ImNodesCol_TitleBarHovered]       = IM_COL32(51, 56, 66, 255);
    nodeStyle.Colors[ImNodesCol_TitleBarSelected]      = IM_COL32(74, 138, 219, 255);
    nodeStyle.Colors[ImNodesCol_Link]                  = IM_COL32(74, 138, 219, 200);
    nodeStyle.Colors[ImNodesCol_LinkHovered]           = IM_COL32(94, 154, 232, 255);
    nodeStyle.Colors[ImNodesCol_LinkSelected]          = IM_COL32(74, 138, 219, 255);
    nodeStyle.Colors[ImNodesCol_GridBackground]        = IM_COL32(20, 20, 22, 255);
    nodeStyle.Colors[ImNodesCol_GridLine]              = IM_COL32(42, 42, 48, 100);
    nodeStyle.Colors[ImNodesCol_Pin]                   = IM_COL32(74, 138, 219, 255);
    nodeStyle.Colors[ImNodesCol_PinHovered]            = IM_COL32(94, 154, 232, 255);
    nodeStyle.Flags |= ImNodesStyleFlags_GridLines;

    initialized_ = true;
}

void DialogueNodeEditor::shutdown() {
    if (!initialized_) return;
    ImNodes::DestroyContext();
    initialized_ = false;
}

void DialogueNodeEditor::draw() {
    if (!open_) return;

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Dialogue Editor", &open_, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    drawMenuBar();

    if (!initialized_) {
        ImGui::Text("Node editor not initialized.");
        ImGui::End();
        return;
    }

    ImNodes::BeginNodeEditor();

    // Draw nodes
    for (auto& node : nodes_) {
        ImNodes::BeginNode(node.id);

        // Title bar
        ImNodes::BeginNodeTitleBar();
        char speakerBuf[128];
        std::strncpy(speakerBuf, node.speakerName.c_str(), sizeof(speakerBuf) - 1);
        speakerBuf[sizeof(speakerBuf) - 1] = '\0';
        ImGui::PushItemWidth(120.0f);
        std::string speakerLabel = "##speaker" + std::to_string(node.id);
        if (ImGui::InputText(speakerLabel.c_str(), speakerBuf, sizeof(speakerBuf))) {
            node.speakerName = speakerBuf;
        }
        ImGui::PopItemWidth();
        ImNodes::EndNodeTitleBar();

        // Input pin (for incoming links)
        ImNodes::BeginInputAttribute(inputAttrId(node.id));
        ImGui::Text("In");
        ImNodes::EndInputAttribute();

        // Dialogue text
        char textBuf[512];
        std::strncpy(textBuf, node.text.c_str(), sizeof(textBuf) - 1);
        textBuf[sizeof(textBuf) - 1] = '\0';
        ImGui::PushItemWidth(180.0f);
        std::string textLabel = "##text" + std::to_string(node.id);
        if (ImGui::InputTextMultiline(textLabel.c_str(), textBuf, sizeof(textBuf),
                                       ImVec2(180, 60))) {
            node.text = textBuf;
        }
        ImGui::PopItemWidth();

        // Choices (output pins)
        for (size_t i = 0; i < node.choices.size(); ++i) {
            auto& choice = node.choices[i];
            ImNodes::BeginOutputAttribute(choice.id);

            char choiceBuf[128];
            std::strncpy(choiceBuf, choice.text.c_str(), sizeof(choiceBuf) - 1);
            choiceBuf[sizeof(choiceBuf) - 1] = '\0';
            ImGui::PushItemWidth(140.0f);
            std::string choiceLabel = "##choice" + std::to_string(choice.id);
            if (ImGui::InputText(choiceLabel.c_str(), choiceBuf, sizeof(choiceBuf))) {
                choice.text = choiceBuf;
            }
            ImGui::PopItemWidth();

            // Delete choice button
            ImGui::SameLine();
            std::string delLabel = "X##delchoice" + std::to_string(choice.id);
            if (ImGui::SmallButton(delLabel.c_str())) {
                // Remove links associated with this choice
                links_.erase(
                    std::remove_if(links_.begin(), links_.end(),
                        [&](const LinkInfo& l) { return l.choiceAttrId == choice.id; }),
                    links_.end());
                node.choices.erase(node.choices.begin() + static_cast<int>(i));
                ImNodes::EndOutputAttribute();
                break; // iterator invalidated
            }

            ImNodes::EndOutputAttribute();
        }

        // Add choice button
        std::string addLabel = "+ Choice##" + std::to_string(node.id);
        if (ImGui::SmallButton(addLabel.c_str())) {
            EditorNode::Choice newChoice;
            newChoice.id = allocChoiceId();
            newChoice.text = "Option";
            newChoice.nextNodeId = -1;
            node.choices.push_back(newChoice);
        }

        ImNodes::EndNode();
    }

    // Draw links
    for (auto& link : links_) {
        ImNodes::Link(link.id, link.choiceAttrId, link.targetAttrId);
    }

    ImNodes::EndNodeEditor();

    // Handle new link creation
    int startAttr = 0, endAttr = 0;
    if (ImNodes::IsLinkCreated(&startAttr, &endAttr)) {
        // startAttr is an output (choice id), endAttr is an input (nodeId * 100)
        // But imnodes may swap them — figure out which is output vs input
        int choiceAttr = startAttr;
        int targetAttr = endAttr;

        // Check if startAttr is actually an input attr (nodeId * 100)
        bool startIsInput = false;
        for (auto& n : nodes_) {
            if (inputAttrId(n.id) == startAttr) {
                startIsInput = true;
                break;
            }
        }
        if (startIsInput) {
            std::swap(choiceAttr, targetAttr);
        }

        // Find the target node from the input attribute
        int targetNodeId = -1;
        for (auto& n : nodes_) {
            if (inputAttrId(n.id) == targetAttr) {
                targetNodeId = n.id;
                break;
            }
        }

        // Set nextNodeId on the choice
        if (targetNodeId >= 0) {
            for (auto& n : nodes_) {
                for (auto& c : n.choices) {
                    if (c.id == choiceAttr) {
                        c.nextNodeId = targetNodeId;
                    }
                }
            }

            LinkInfo newLink;
            newLink.id = allocLinkId();
            newLink.choiceAttrId = choiceAttr;
            newLink.targetAttrId = targetAttr;
            links_.push_back(newLink);
        }
    }

    // Handle link deletion
    int destroyedLinkId = 0;
    if (ImNodes::IsLinkDestroyed(&destroyedLinkId)) {
        auto it = std::find_if(links_.begin(), links_.end(),
            [destroyedLinkId](const LinkInfo& l) { return l.id == destroyedLinkId; });
        if (it != links_.end()) {
            // Clear the nextNodeId on the source choice
            for (auto& n : nodes_) {
                for (auto& c : n.choices) {
                    if (c.id == it->choiceAttrId) {
                        c.nextNodeId = -1;
                    }
                }
            }
            links_.erase(it);
        }
    }

    // Right-click context menu
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("NodeEditorContext");
    }
    if (ImGui::BeginPopup("NodeEditorContext")) {
        if (ImGui::MenuItem("Add Node")) {
            EditorNode newNode;
            newNode.id = allocNodeId();
            newNode.speakerName = "NPC";
            newNode.text = "Dialogue text...";

            // Add a default choice
            EditorNode::Choice defaultChoice;
            defaultChoice.id = allocChoiceId();
            defaultChoice.text = "Continue";
            defaultChoice.nextNodeId = -1;
            newNode.choices.push_back(defaultChoice);

            // Position near mouse
            ImVec2 mousePos = ImGui::GetMousePosOnOpeningCurrentPopup();
            ImNodes::SetNodeScreenSpacePos(newNode.id, mousePos);

            nodes_.push_back(std::move(newNode));
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

void DialogueNodeEditor::drawMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New")) {
                nodes_.clear();
                links_.clear();
                nextNodeId_ = 1;
                nextChoiceId_ = 1000;
                nextLinkId_ = 10000;
                currentFilePath_.clear();
            }
            if (ImGui::MenuItem("Open...")) {
                // For now, load from a default path (could use file dialog later)
                if (!currentFilePath_.empty()) {
                    loadFromFile(currentFilePath_);
                }
            }
            if (ImGui::MenuItem("Save", nullptr, false, !currentFilePath_.empty())) {
                saveToFile(currentFilePath_);
            }
            if (ImGui::BeginMenu("Save As...")) {
                static char pathBuf[256] = "assets/dialogues/untitled.json";
                ImGui::InputText("Path", pathBuf, sizeof(pathBuf));
                if (ImGui::Button("Save")) {
                    saveToFile(pathBuf);
                    currentFilePath_ = pathBuf;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void DialogueNodeEditor::rebuildLinks() {
    links_.clear();
    for (auto& node : nodes_) {
        for (auto& choice : node.choices) {
            if (choice.nextNodeId > 0) {
                // Verify target node exists
                bool found = false;
                for (auto& target : nodes_) {
                    if (target.id == choice.nextNodeId) {
                        found = true;
                        break;
                    }
                }
                if (found) {
                    LinkInfo link;
                    link.id = allocLinkId();
                    link.choiceAttrId = choice.id;
                    link.targetAttrId = inputAttrId(choice.nextNodeId);
                    links_.push_back(link);
                }
            }
        }
    }
}

void DialogueNodeEditor::loadFromJson(const nlohmann::json& data) {
    nodes_.clear();
    links_.clear();
    nextNodeId_ = 1;
    nextChoiceId_ = 1000;
    nextLinkId_ = 10000;

    if (!data.contains("nodes") || !data["nodes"].is_array()) return;

    float offsetX = 50.0f;
    float offsetY = 50.0f;
    int col = 0;

    for (auto& jn : data["nodes"]) {
        EditorNode node;
        node.id = jn.value("id", allocNodeId());
        node.speakerName = jn.value("speaker", std::string("NPC"));
        node.text = jn.value("text", std::string(""));
        node.posX = jn.value("posX", offsetX + col * 250.0f);
        node.posY = jn.value("posY", offsetY);
        col++;

        if (node.id >= nextNodeId_) nextNodeId_ = node.id + 1;

        if (jn.contains("choices") && jn["choices"].is_array()) {
            for (auto& jc : jn["choices"]) {
                EditorNode::Choice choice;
                choice.id = jc.value("choiceId", allocChoiceId());
                choice.text = jc.value("text", std::string("Option"));
                choice.nextNodeId = jc.value("nextNodeId", -1);

                if (choice.id >= nextChoiceId_) nextChoiceId_ = choice.id + 1;

                node.choices.push_back(choice);
            }
        }

        nodes_.push_back(std::move(node));
    }

    rebuildLinks();
}

nlohmann::json DialogueNodeEditor::saveToJson() const {
    nlohmann::json result;
    result["nodes"] = nlohmann::json::array();

    for (auto& node : nodes_) {
        nlohmann::json jn;
        jn["id"] = node.id;
        jn["speaker"] = node.speakerName;
        jn["text"] = node.text;
        jn["posX"] = node.posX;
        jn["posY"] = node.posY;

        jn["choices"] = nlohmann::json::array();
        for (auto& choice : node.choices) {
            nlohmann::json jc;
            jc["choiceId"] = choice.id;
            jc["text"] = choice.text;
            jc["nextNodeId"] = choice.nextNodeId;
            jn["choices"].push_back(jc);
        }

        result["nodes"].push_back(jn);
    }

    return result;
}

void DialogueNodeEditor::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        LOG_WARN("DialogueEditor", "Failed to open: %s", path.c_str());
        return;
    }

    nlohmann::json data;
    try {
        f >> data;
    } catch (const std::exception& e) {
        LOG_WARN("DialogueEditor", "JSON parse error in %s: %s", path.c_str(), e.what());
        return;
    }

    loadFromJson(data);
    currentFilePath_ = path;
    LOG_INFO("DialogueEditor", "Loaded dialogue from %s", path.c_str());
}

void DialogueNodeEditor::saveToFile(const std::string& path) {
    auto data = saveToJson();
    std::ofstream f(path);
    if (!f.is_open()) {
        LOG_WARN("DialogueEditor", "Failed to write: %s", path.c_str());
        return;
    }
    f << data.dump(2);
    currentFilePath_ = path;
    LOG_INFO("DialogueEditor", "Saved dialogue to %s", path.c_str());
}

} // namespace fate
