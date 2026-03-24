#include "engine/editor/animation_editor.h"
#include "engine/core/logger.h"
#include "engine/render/texture.h"
#include "game/animation_loader.h"
#include <imgui.h>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <set>
#include <stb_image.h>
#include <stb_image_write.h>

namespace fate {

namespace fs = std::filesystem;

namespace {
static void drawCheckerboard(ImDrawList* dl, ImVec2 pos, float w, float h, int checkSize = 8) {
    ImU32 c1 = IM_COL32(40, 40, 40, 255);
    ImU32 c2 = IM_COL32(50, 50, 50, 255);
    for (int y = 0; y < (int)h; y += checkSize) {
        for (int x = 0; x < (int)w; x += checkSize) {
            ImU32 c = ((x / checkSize + y / checkSize) % 2 == 0) ? c1 : c2;
            float x1 = pos.x + (float)x, y1 = pos.y + (float)y;
            float x2 = x1 + fminf((float)checkSize, w - (float)x);
            float y2 = y1 + fminf((float)checkSize, h - (float)y);
            dl->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), c);
        }
    }
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void AnimationEditor::init() {}

void AnimationEditor::shutdown() {
    frameTexCache_.clear();
}

void AnimationEditor::draw() {
    if (!open_) return;

    if (ImGui::Begin("Animation Editor", &open_, ImGuiWindowFlags_MenuBar)) {
        drawMenuBar();
        drawTopBar();
        ImGui::Separator();
        drawFrameWorkspace();
        drawPreview();
    }
    ImGui::End();
}

void AnimationEditor::openFile(const std::string& path) {
    open_ = true;
    if (!path.empty()) {
        // Detect file type by extension
        if (path.find(".frameset") != std::string::npos) {
            loadFrameSet(path);
        } else {
            loadTemplate(path);
        }
    }
}

// ---------------------------------------------------------------------------
// Task 3: Template File I/O
// ---------------------------------------------------------------------------
void AnimationEditor::newTemplate(const std::string& entityType) {
    template_ = AnimTemplate{};
    template_.entityType = entityType;
    template_.name = "Untitled";
    templatePath_.clear();
    frameSets_.clear();
    selectedStateIdx_ = 0;
    selectedFrameIdx_ = -1;

    auto makeState = [](const std::string& n, float rate, bool loop, int hit) {
        AnimState s;
        s.name = n;
        s.frameRate = rate;
        s.loop = loop;
        s.hitFrame = hit;
        s.frameCount = {{"down", 1}, {"up", 1}, {"side", 1}};
        return s;
    };

    if (entityType == "player") {
        template_.states.push_back(makeState("idle",   8.0f,  true, -1));
        template_.states.push_back(makeState("walk",   8.0f,  true, -1));
        template_.states.push_back(makeState("attack", 10.0f, false, 1));
        template_.states.push_back(makeState("cast",   8.0f,  true, -1));
        template_.states.push_back(makeState("death",  8.0f,  true, -1));
    } else if (entityType == "mob") {
        template_.states.push_back(makeState("idle",   8.0f,  true, -1));
        template_.states.push_back(makeState("walk",   8.0f,  true, -1));
        template_.states.push_back(makeState("attack", 10.0f, false, 1));
        template_.states.push_back(makeState("death",  8.0f,  true, -1));
    } else if (entityType == "npc") {
        template_.states.push_back(makeState("idle",   8.0f,  true, -1));
    }

    LOG_INFO("AnimEditor", "Created new %s template", entityType.c_str());
}

void AnimationEditor::loadTemplate(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        LOG_ERROR("AnimEditor", "Failed to open template: %s", path.c_str());
        return;
    }

    try {
        nlohmann::json j;
        in >> j;

        template_ = AnimTemplate{};
        template_.version = j.value("version", 1);
        template_.name = j.value("name", "Untitled");
        template_.entityType = j.value("entityType", "player");

        for (auto& sj : j["states"]) {
            AnimState s;
            s.name = sj.value("name", "unknown");
            s.frameRate = sj.value("frameRate", 8.0f);
            s.loop = sj.value("loop", true);
            s.hitFrame = sj.value("hitFrame", -1);
            if (sj.contains("frameCount")) {
                for (auto& [dir, cnt] : sj["frameCount"].items()) {
                    s.frameCount[dir] = cnt.get<int>();
                }
            }
            template_.states.push_back(std::move(s));
        }

        templatePath_ = path;
        selectedStateIdx_ = 0;
        selectedFrameIdx_ = -1;
        LOG_INFO("AnimEditor", "Loaded template: %s (%d states)",
                 template_.name.c_str(), (int)template_.states.size());
    } catch (const std::exception& e) {
        LOG_ERROR("AnimEditor", "Error parsing template %s: %s", path.c_str(), e.what());
    }
}

void AnimationEditor::saveTemplate() {
    if (templatePath_.empty()) {
        LOG_WARN("AnimEditor", "No template path set — cannot save");
        return;
    }

    nlohmann::json j;
    j["version"] = template_.version;
    j["name"] = template_.name;
    j["entityType"] = template_.entityType;

    nlohmann::json statesArr = nlohmann::json::array();
    for (auto& s : template_.states) {
        nlohmann::json sj;
        sj["name"] = s.name;
        sj["frameRate"] = s.frameRate;
        sj["loop"] = s.loop;
        sj["hitFrame"] = s.hitFrame;
        nlohmann::json fc;
        for (auto& [dir, cnt] : s.frameCount) {
            fc[dir] = cnt;
        }
        sj["frameCount"] = fc;
        statesArr.push_back(sj);
    }
    j["states"] = statesArr;

    // Ensure parent directory exists
    fs::path p(templatePath_);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path());
    }

    std::ofstream out(templatePath_);
    if (!out.is_open()) {
        LOG_ERROR("AnimEditor", "Failed to write template: %s", templatePath_.c_str());
        return;
    }
    out << j.dump(2);
    LOG_INFO("AnimEditor", "Saved template: %s", templatePath_.c_str());
}

// ---------------------------------------------------------------------------
// Task 3: Menu Bar
// ---------------------------------------------------------------------------
void AnimationEditor::drawMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            // New submenu
            if (ImGui::BeginMenu("New")) {
                if (ImGui::MenuItem("Player")) newTemplate("player");
                if (ImGui::MenuItem("Mob"))    newTemplate("mob");
                if (ImGui::MenuItem("NPC"))    newTemplate("npc");
                ImGui::EndMenu();
            }

            // Open with input popup
            if (ImGui::BeginMenu("Open")) {
                ImGui::InputText("Path", openPathBuf_, sizeof(openPathBuf_));
                if (ImGui::Button("Load")) {
                    loadTemplate(openPathBuf_);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Save", nullptr, false, !templatePath_.empty())) {
                saveTemplate();
            }

            ImGui::Separator();

            // Frame set I/O
            if (ImGui::BeginMenu("Load Frame Set")) {
                ImGui::InputText("Path##fs", loadFrameSetBuf_, sizeof(loadFrameSetBuf_));
                if (ImGui::Button("Load##fs")) {
                    loadFrameSet(loadFrameSetBuf_);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Save Frame Set", nullptr, false,
                                !template_.name.empty())) {
                saveFrameSet(currentLayerVariantKey());
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Save Meta JSON", nullptr, false,
                                slicerMode_ && !sheetTexturePath_.empty())) {
                saveMetaJson(sheetTexturePath_);
            }

            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

// ---------------------------------------------------------------------------
// Task 3: Top Bar
// ---------------------------------------------------------------------------
void AnimationEditor::drawTopBar() {
    ImGui::Text("Template: %s", template_.name.c_str());
    ImGui::SameLine();
    ImGui::Text("(%s)", template_.entityType.c_str());

    ImGui::SameLine(0.0f, 20.0f);
    // Layer radio buttons
    if (ImGui::RadioButton("Body",   selectedLayer_ == "body"))   selectedLayer_ = "body";
    ImGui::SameLine();
    if (ImGui::RadioButton("Weapon", selectedLayer_ == "weapon")) selectedLayer_ = "weapon";
    ImGui::SameLine();
    if (ImGui::RadioButton("Gloves", selectedLayer_ == "gloves")) selectedLayer_ = "gloves";

    ImGui::SameLine(0.0f, 20.0f);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputText("Variant", variantBuf_, sizeof(variantBuf_));
    selectedVariant_ = variantBuf_;
}

// ---------------------------------------------------------------------------
// Task 4: State List
// ---------------------------------------------------------------------------
void AnimationEditor::drawStateList() {
    ImGui::BeginChild("StateList", ImVec2(130, 0), true);

    for (int i = 0; i < (int)template_.states.size(); ++i) {
        bool selected = (i == selectedStateIdx_);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.290f, 0.541f, 0.859f, 0.30f));
        }
        if (ImGui::Selectable(template_.states[i].name.c_str(), selected)) {
            selectedStateIdx_ = i;
            selectedFrameIdx_ = -1;
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
    }

    ImGui::Separator();

    // Add state button
    if (ImGui::Button("+", ImVec2(30, 0))) {
        ImGui::OpenPopup("AddStatePopup");
        newStateBuf_[0] = '\0';
    }

    if (ImGui::BeginPopup("AddStatePopup")) {
        ImGui::InputText("State Name", newStateBuf_, sizeof(newStateBuf_));
        if (ImGui::Button("Add") && newStateBuf_[0] != '\0') {
            AnimState ns;
            ns.name = newStateBuf_;
            ns.frameRate = 8.0f;
            ns.loop = true;
            ns.hitFrame = -1;
            ns.frameCount = {{"down", 1}, {"up", 1}, {"side", 1}};
            template_.states.push_back(std::move(ns));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // Remove state button
    if (ImGui::Button("-", ImVec2(30, 0))) {
        if (selectedStateIdx_ >= 0 && selectedStateIdx_ < (int)template_.states.size()) {
            template_.states.erase(template_.states.begin() + selectedStateIdx_);
            if (selectedStateIdx_ >= (int)template_.states.size()) {
                selectedStateIdx_ = (int)template_.states.size() - 1;
            }
            selectedFrameIdx_ = -1;
        }
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Task 4: Frame Strip
// ---------------------------------------------------------------------------
void AnimationEditor::drawFrameStrip() {
    auto& frames = currentFrameList();
    int removeIdx = -1;
    int dragSrc = -1;
    int dragDst = -1;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

    for (int i = 0; i < (int)frames.size(); ++i) {
        ImGui::PushID(i);

        bool isSelected = (i == selectedFrameIdx_);

        ImVec2 framePos = ImGui::GetCursorScreenPos();
        drawCheckerboard(ImGui::GetWindowDrawList(), framePos, 48.0f, 48.0f);

        // Try to load texture
        unsigned int texId = loadFrameTexture(frames[i]);
        if (texId != 0) {
            if (ImGui::ImageButton("frame", (ImTextureID)(intptr_t)texId, ImVec2(48, 48))) {
                selectedFrameIdx_ = i;
            }
        } else {
            // Placeholder button with frame index
            char label[16];
            snprintf(label, sizeof(label), "%d", i);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button(label, ImVec2(48, 48))) {
                selectedFrameIdx_ = i;
            }
            ImGui::PopStyleColor();
        }

        if (isSelected) {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(min, max, IM_COL32(74, 138, 219, 255), 2.0f, 0, 2.0f);
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextItem("FrameCtx")) {
            if (ImGui::MenuItem("Remove")) {
                removeIdx = i;
            }
            ImGui::EndPopup();
        }

        // Drag-to-reorder source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("ANIM_FRAME_REORDER", &i, sizeof(int));
            ImGui::Text("Frame %d", i);
            ImGui::EndDragDropSource();
        }

        // Drag-to-reorder target
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ANIM_FRAME_REORDER")) {
                dragSrc = *(const int*)payload->Data;
                dragDst = i;
            }
            // Also accept ASSET drops on individual frame slots
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET")) {
                std::string assetPath((const char*)payload->Data, payload->DataSize - 1);
                auto ext = fs::path(assetPath).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".png" || ext == ".jpg" || ext == ".bmp") {
                    frames.push_back(assetPath);
                    // Update frameCount in template
                    if (selectedStateIdx_ >= 0 && selectedStateIdx_ < (int)template_.states.size()) {
                        auto& st = template_.states[selectedStateIdx_];
                        st.frameCount[directionName(selectedDirection_)] = (int)frames.size();
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        ImGui::PopID();
    }

    // "Drop here" zone at end of strip for ASSET drops
    {
        ImVec2 dropPos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRect(
            dropPos, ImVec2(dropPos.x + 48, dropPos.y + 48),
            IM_COL32(128, 128, 128, 60), 3.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.06f));
        if (ImGui::Button("+##drop", ImVec2(48, 48))) {}
        ImGui::PopStyleColor(2);
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET")) {
            std::string assetPath((const char*)payload->Data, payload->DataSize - 1);
            auto ext = fs::path(assetPath).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".png" || ext == ".jpg" || ext == ".bmp") {
                frames.push_back(assetPath);
                if (selectedStateIdx_ >= 0 && selectedStateIdx_ < (int)template_.states.size()) {
                    auto& st = template_.states[selectedStateIdx_];
                    st.frameCount[directionName(selectedDirection_)] = (int)frames.size();
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::PopStyleVar(); // ItemSpacing

    // Apply reorder
    if (dragSrc >= 0 && dragDst >= 0 && dragSrc != dragDst) {
        std::string tmp = std::move(frames[dragSrc]);
        frames.erase(frames.begin() + dragSrc);
        int insertAt = (dragDst > dragSrc) ? dragDst - 1 : dragDst;
        if (insertAt < 0) insertAt = 0;
        if (insertAt > (int)frames.size()) insertAt = (int)frames.size();
        frames.insert(frames.begin() + insertAt, std::move(tmp));
    }

    // Apply removal
    if (removeIdx >= 0 && removeIdx < (int)frames.size()) {
        frames.erase(frames.begin() + removeIdx);
        if (selectedStateIdx_ >= 0 && selectedStateIdx_ < (int)template_.states.size()) {
            auto& st = template_.states[selectedStateIdx_];
            st.frameCount[directionName(selectedDirection_)] = (int)frames.size();
        }
        if (selectedFrameIdx_ >= (int)frames.size()) {
            selectedFrameIdx_ = (int)frames.size() - 1;
        }
    }
}

// ---------------------------------------------------------------------------
// Task 4: State Properties
// ---------------------------------------------------------------------------
void AnimationEditor::drawStateProperties() {
    if (selectedStateIdx_ < 0 || selectedStateIdx_ >= (int)template_.states.size()) return;

    auto& state = template_.states[selectedStateIdx_];

    ImGui::DragFloat("Frame Rate", &state.frameRate, 0.5f, 1.0f, 60.0f);
    ImGui::Checkbox("Loop", &state.loop);

    // hitFrame combo
    auto& frames = currentFrameList();
    int frameCount = (int)frames.size();

    // Build combo items: "None" + frame indices
    int comboValue = state.hitFrame + 1; // 0 = None, 1..N = frames 0..N-1
    const char* preview = "None";
    char prevBuf[16];
    if (state.hitFrame >= 0) {
        snprintf(prevBuf, sizeof(prevBuf), "%d", state.hitFrame);
        preview = prevBuf;
    }

    if (ImGui::BeginCombo("Hit Frame", preview)) {
        if (ImGui::Selectable("None", state.hitFrame == -1)) {
            state.hitFrame = -1;
        }
        for (int i = 0; i < frameCount; ++i) {
            char label[16];
            snprintf(label, sizeof(label), "%d", i);
            if (ImGui::Selectable(label, state.hitFrame == i)) {
                state.hitFrame = i;
            }
        }
        ImGui::EndCombo();
    }
    (void)comboValue; // suppress unused warning
}

// ---------------------------------------------------------------------------
// Task 4: Frame Workspace (combines state list + direction tabs + strip + properties)
// ---------------------------------------------------------------------------
void AnimationEditor::drawFrameWorkspace() {
    if (slicerMode_) {
        // Left panel: state list
        ImGui::BeginChild("##stateListPanel", ImVec2(130, 0), true);
        drawStateList();
        ImGui::EndChild();
        ImGui::SameLine();

        // Right panel: slicer + strip + properties
        ImGui::BeginChild("##slicerPanel", ImVec2(0, 0));
        for (int d = 0; d < 3; ++d) {
            if (d > 0) ImGui::SameLine();
            if (ImGui::RadioButton(directionName(d), selectedDirection_ == d))
                selectedDirection_ = d;
        }
        ImGui::Separator();
        drawSlicerView();
        ImGui::Separator();
        drawSlicerFrameStrip();
        ImGui::Separator();
        drawStateProperties();
        ImGui::EndChild();
        return;
    }

    drawStateList();
    ImGui::SameLine();
    ImGui::BeginGroup();

    // Direction tabs
    ImGui::RadioButton("Down", &selectedDirection_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Up", &selectedDirection_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Side", &selectedDirection_, 2);

    drawFrameStrip();
    ImGui::Separator();
    drawStateProperties();

    ImGui::EndGroup();
}

// ---------------------------------------------------------------------------
// Task 5: Frame Set File I/O
// ---------------------------------------------------------------------------
void AnimationEditor::loadFrameSet(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        LOG_ERROR("AnimEditor", "Failed to open frame set: %s", path.c_str());
        return;
    }

    try {
        nlohmann::json j;
        in >> j;

        FrameSet fs;
        fs.version = j.value("version", 1);
        fs.templateName = j.value("template", "");
        fs.layer = j.value("layer", "body");
        fs.variant = j.value("variant", "");
        fs.packedSheet = j.value("packedSheet", "");
        fs.packedMeta = j.value("packedMeta", "");

        if (j.contains("frames")) {
            for (auto& [stateName, dirObj] : j["frames"].items()) {
                for (auto& [dir, pathsArr] : dirObj.items()) {
                    for (auto& p : pathsArr) {
                        fs.frames[stateName][dir].push_back(p.get<std::string>());
                    }
                }
            }
        }

        std::string key = fs.layer + ":" + fs.variant;
        frameSets_[key] = std::move(fs);
        selectedStateIdx_ = 0;
        selectedFrameIdx_ = -1;
        LOG_INFO("AnimEditor", "Loaded frame set: %s (key=%s)", path.c_str(), key.c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("AnimEditor", "Error parsing frame set %s: %s", path.c_str(), e.what());
    }
}

void AnimationEditor::saveFrameSet(const std::string& layerVariantKey) {
    auto it = frameSets_.find(layerVariantKey);
    if (it == frameSets_.end()) {
        LOG_WARN("AnimEditor", "No frame set for key: %s", layerVariantKey.c_str());
        return;
    }

    auto& fset = it->second;
    fset.templateName = template_.name;

    nlohmann::json j;
    j["version"] = fset.version;
    j["template"] = fset.templateName;
    j["layer"] = fset.layer;
    j["variant"] = fset.variant;

    nlohmann::json framesObj;
    for (auto& [stateName, dirMap] : fset.frames) {
        nlohmann::json dirObj;
        for (auto& [dir, paths] : dirMap) {
            dirObj[dir] = paths;
        }
        framesObj[stateName] = dirObj;
    }
    j["frames"] = framesObj;
    j["packedSheet"] = fset.packedSheet;
    j["packedMeta"] = fset.packedMeta;

    // Output path based on template name
    std::string dir = "assets/animations";
    fs::create_directories(dir);
    std::string outPath = dir + "/" + template_.name + "_" + fset.layer + "_" + fset.variant + ".frameset";

    std::ofstream out(outPath);
    if (!out.is_open()) {
        LOG_ERROR("AnimEditor", "Failed to write frame set: %s", outPath.c_str());
        return;
    }
    out << j.dump(2);
    LOG_INFO("AnimEditor", "Saved frame set: %s", outPath.c_str());

    // Also pack the sprite sheet
    packFrameSet(layerVariantKey);
}

// ---------------------------------------------------------------------------
// Task 6: Preview Playback
// ---------------------------------------------------------------------------
void AnimationEditor::drawPreview() {
    ImGui::Separator();
    ImGui::Text("Preview");

    if (selectedStateIdx_ < 0 || selectedStateIdx_ >= (int)template_.states.size()) {
        ImGui::TextDisabled("No state selected");
        return;
    }

    auto& state = template_.states[selectedStateIdx_];
    auto& frames = currentFrameList();
    int frameCount = (int)frames.size();

    if (frameCount == 0) {
        ImGui::TextDisabled("No frames");
        return;
    }

    // Advance timer if playing
    if (previewPlaying_ && state.frameRate > 0.0f) {
        previewTimer_ += ImGui::GetIO().DeltaTime;
        float frameDuration = 1.0f / state.frameRate;
        float totalDuration = frameCount * frameDuration;

        if (state.loop) {
            if (totalDuration > 0.0f) {
                while (previewTimer_ >= totalDuration) previewTimer_ -= totalDuration;
            }
            previewFrame_ = (int)(previewTimer_ * state.frameRate) % frameCount;
        } else {
            previewFrame_ = (int)(previewTimer_ * state.frameRate);
            if (previewFrame_ >= frameCount) {
                previewFrame_ = frameCount - 1;
                previewPlaying_ = false;
            }
        }
    }

    // Clamp preview frame
    if (previewFrame_ < 0) previewFrame_ = 0;
    if (previewFrame_ >= frameCount) previewFrame_ = frameCount - 1;

    // Show current frame at 4x zoom
    if (previewFrame_ >= 0 && previewFrame_ < (int)frames.size()) {
        unsigned int texId = loadFrameTexture(frames[previewFrame_]);
        if (texId != 0) {
            ImVec2 previewPos = ImGui::GetCursorScreenPos();
            drawCheckerboard(ImGui::GetWindowDrawList(), previewPos, 128.0f, 128.0f);
            ImGui::Image((ImTextureID)(intptr_t)texId, ImVec2(128, 128));
        } else {
            ImGui::Dummy(ImVec2(128, 128));
        }
    }

    if (fontSmall_) ImGui::PushFont(fontSmall_);
    ImGui::Text("Frame %d / %d", previewFrame_ + 1, frameCount);
    if (fontSmall_) ImGui::PopFont();

    // Transport controls
    if (ImGui::Button(previewPlaying_ ? "Pause" : "Play")) {
        previewPlaying_ = !previewPlaying_;
        if (previewPlaying_ && previewFrame_ >= frameCount - 1 && !state.loop) {
            // Restart from beginning if at end of non-looping animation
            previewTimer_ = 0.0f;
            previewFrame_ = 0;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Step")) {
        previewPlaying_ = false;
        previewFrame_ = (previewFrame_ + 1) % frameCount;
        previewTimer_ = previewFrame_ / state.frameRate;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        previewPlaying_ = false;
        previewTimer_ = 0.0f;
        previewFrame_ = 0;
    }
}

// ---------------------------------------------------------------------------
// Task 7: Sprite Sheet Packing
// ---------------------------------------------------------------------------
void AnimationEditor::packFrameSet(const std::string& layerVariantKey) {
    auto it = frameSets_.find(layerVariantKey);
    if (it == frameSets_.end()) {
        LOG_WARN("AnimEditor", "No frame set to pack for key: %s", layerVariantKey.c_str());
        return;
    }

    auto& fset = it->second;

    // 1. Collect all frame paths in order: states -> directions (down, up, side)
    std::vector<std::string> allPaths;
    for (auto& state : template_.states) {
        const char* dirs[] = {"down", "up", "side"};
        for (auto dir : dirs) {
            auto stIt = fset.frames.find(state.name);
            if (stIt != fset.frames.end()) {
                auto dIt = stIt->second.find(dir);
                if (dIt != stIt->second.end()) {
                    for (auto& p : dIt->second) {
                        allPaths.push_back(p);
                    }
                }
            }
        }
    }

    if (allPaths.empty()) {
        LOG_WARN("AnimEditor", "No frames to pack");
        return;
    }

    // 2. Load all frames
    struct FrameData {
        unsigned char* pixels = nullptr;
        int w = 0, h = 0;
    };
    std::vector<FrameData> loaded;
    loaded.reserve(allPaths.size());

    for (auto& p : allPaths) {
        FrameData fd;
        int ch = 0;
        fd.pixels = stbi_load(p.c_str(), &fd.w, &fd.h, &ch, 4);
        if (!fd.pixels) {
            LOG_ERROR("AnimEditor", "Failed to load frame: %s", p.c_str());
            // Free already loaded
            for (auto& prev : loaded) {
                if (prev.pixels) stbi_image_free(prev.pixels);
            }
            return;
        }
        loaded.push_back(fd);
    }

    // 3. Validate dimensions
    int frameW = loaded[0].w;
    int frameH = loaded[0].h;
    for (size_t i = 1; i < loaded.size(); ++i) {
        if (loaded[i].w != frameW || loaded[i].h != frameH) {
            LOG_ERROR("AnimEditor", "Frame size mismatch: frame %d is %dx%d, expected %dx%d",
                      (int)i, loaded[i].w, loaded[i].h, frameW, frameH);
            for (auto& fd : loaded) stbi_image_free(fd.pixels);
            return;
        }
    }

    // 4. Layout
    int totalFrames = (int)loaded.size();
    int columns = totalFrames;
    if (columns * frameW > 2048) {
        columns = 2048 / frameW;
        if (columns < 1) columns = 1;
    }
    int rows = (totalFrames + columns - 1) / columns;
    int sheetW = columns * frameW;
    int sheetH = rows * frameH;

    // 5. Allocate and copy
    std::vector<unsigned char> sheetPixels(sheetW * sheetH * 4, 0);
    for (int i = 0; i < totalFrames; ++i) {
        int col = i % columns;
        int row = i / columns;
        int dstX = col * frameW;
        int dstY = row * frameH;

        for (int y = 0; y < frameH; ++y) {
            unsigned char* dstRow = sheetPixels.data() + ((dstY + y) * sheetW + dstX) * 4;
            unsigned char* srcRow = loaded[i].pixels + y * frameW * 4;
            memcpy(dstRow, srcRow, frameW * 4);
        }
    }

    // 6. Write PNG
    std::string packedDir = "assets/animations/packed";
    fs::create_directories(packedDir);
    std::string sheetPath = packedDir + "/" + template_.name + "_"
                          + fset.layer + "_" + fset.variant + "_sheet.png";
    std::string metaPath = packedDir + "/" + template_.name + "_"
                         + fset.layer + "_" + fset.variant + "_sheet.json";

    if (!stbi_write_png(sheetPath.c_str(), sheetW, sheetH, 4,
                        sheetPixels.data(), sheetW * 4)) {
        LOG_ERROR("AnimEditor", "Failed to write sprite sheet: %s", sheetPath.c_str());
        for (auto& fd : loaded) stbi_image_free(fd.pixels);
        return;
    }

    // 7. Build metadata JSON with 4-direction entries
    nlohmann::json meta;
    meta["sheet"] = sheetPath;
    meta["frameWidth"] = frameW;
    meta["frameHeight"] = frameH;
    meta["columns"] = columns;
    meta["totalFrames"] = totalFrames;

    nlohmann::json entries = nlohmann::json::array();
    int frameIdx = 0;
    for (auto& state : template_.states) {
        const char* dirs[] = {"down", "up", "side"};
        for (auto dir : dirs) {
            auto stIt = fset.frames.find(state.name);
            if (stIt == fset.frames.end()) continue;
            auto dIt = stIt->second.find(dir);
            if (dIt == stIt->second.end()) continue;

            int count = (int)dIt->second.size();
            if (count == 0) continue;

            if (std::string(dir) == "down" || std::string(dir) == "up") {
                nlohmann::json e;
                e["name"] = state.name + "_" + dir;
                e["startFrame"] = frameIdx;
                e["frameCount"] = count;
                e["flipX"] = false;
                entries.push_back(e);
            } else {
                // "side" -> left (no flip) and right (flipX)
                nlohmann::json eLeft;
                eLeft["name"] = state.name + "_left";
                eLeft["startFrame"] = frameIdx;
                eLeft["frameCount"] = count;
                eLeft["flipX"] = false;
                entries.push_back(eLeft);

                nlohmann::json eRight;
                eRight["name"] = state.name + "_right";
                eRight["startFrame"] = frameIdx;
                eRight["frameCount"] = count;
                eRight["flipX"] = true;
                entries.push_back(eRight);
            }
            frameIdx += count;
        }
    }
    meta["entries"] = entries;

    std::ofstream metaOut(metaPath);
    if (!metaOut.is_open()) {
        LOG_ERROR("AnimEditor", "Failed to write metadata: %s", metaPath.c_str());
    } else {
        metaOut << meta.dump(2);
    }

    // Update frame set references
    fset.packedSheet = sheetPath;
    fset.packedMeta = metaPath;

    // 8. Free loaded frame data
    for (auto& fd : loaded) {
        stbi_image_free(fd.pixels);
    }

    LOG_INFO("AnimEditor", "Packed %d frames -> %s (%dx%d)", totalFrames,
             sheetPath.c_str(), sheetW, sheetH);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
std::string AnimationEditor::currentLayerVariantKey() const {
    return selectedLayer_ + ":" + selectedVariant_;
}

std::vector<std::string>& AnimationEditor::currentFrameList() {
    auto key = currentLayerVariantKey();
    auto& fs = frameSets_[key];
    if (fs.templateName.empty()) {
        fs.templateName = template_.name;
        fs.layer = selectedLayer_;
        fs.variant = selectedVariant_;
    }
    if (selectedStateIdx_ < 0 || selectedStateIdx_ >= (int)template_.states.size()) {
        fallbackFrames_.clear();
        return fallbackFrames_;
    }
    auto& state = template_.states[selectedStateIdx_];
    return fs.frames[state.name][directionName(selectedDirection_)];
}

const char* AnimationEditor::directionName(int idx) const {
    switch (idx) {
        case 0: return "down";
        case 1: return "up";
        case 2: return "side";
        default: return "down";
    }
}

unsigned int AnimationEditor::loadFrameTexture(const std::string& path) {
    if (path.empty()) return 0;

    // Check local cache first
    auto it = frameTexCache_.find(path);
    if (it != frameTexCache_.end()) return it->second;

    auto tex = TextureCache::instance().load(path);
    if (tex && tex->id() != 0) {
        frameTexCache_[path] = tex->id();
        return tex->id();
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Slicer Mode: Grid View
// ---------------------------------------------------------------------------
void AnimationEditor::drawSlicerView() {
    auto tex = TextureCache::instance().get(sheetTexturePath_);
    if (!tex || tex->width() == 0) {
        ImGui::TextDisabled("No sprite sheet loaded");
        return;
    }

    int texW = tex->width();
    int texH = tex->height();

    // Cell size inputs
    ImGui::Text("Cell Size:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::InputInt("W##cellW", &slicerCellW_, 0, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::InputInt("H##cellH", &slicerCellH_, 0, 0);
    slicerCellW_ = std::max(1, slicerCellW_);
    slicerCellH_ = std::max(1, slicerCellH_);

    int columns = texW / slicerCellW_;
    int rows = texH / slicerCellH_;
    int totalFrames = columns * rows;

    ImGui::SameLine();
    ImGui::Text("(%d cols x %d rows = %d frames)", columns, rows, totalFrames);

    if (texW % slicerCellW_ != 0 || texH % slicerCellH_ != 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "Sheet doesn't divide evenly!");
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
    ImGui::SetNextItemWidth(80);
    ImGui::SliderFloat("Zoom", &slicerZoom_, 0.5f, 8.0f, "%.1fx");

    // Scrollable sheet view with grid overlay
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.y = std::max(canvasSize.y * 0.6f, 200.0f);
    ImGui::BeginChild("##slicerCanvas", canvasSize, true,
                      ImGuiWindowFlags_HorizontalScrollbar);

    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    float dispW = texW * slicerZoom_;
    float dispH = texH * slicerZoom_;

    // Draw sprite sheet image
    ImTextureID texId = (ImTextureID)(intptr_t)tex->id();
    drawList->AddImage(texId, cursorPos,
                       ImVec2(cursorPos.x + dispW, cursorPos.y + dispH),
                       ImVec2(0, 1), ImVec2(1, 0));

    float cellDispW = slicerCellW_ * slicerZoom_;
    float cellDispH = slicerCellH_ * slicerZoom_;
    ImU32 gridColor = IM_COL32(255, 255, 255, 60);
    ImU32 hoverColor = IM_COL32(100, 180, 255, 100);
    ImU32 assignedColor = IM_COL32(100, 255, 100, 80);

    // Collect assigned frames for current state+direction
    std::set<int> currentAssigned;
    if (selectedStateIdx_ >= 0 && selectedStateIdx_ < (int)template_.states.size()) {
        auto& state = template_.states[selectedStateIdx_];
        std::string dir = directionName(selectedDirection_);
        auto it = slicerFrameAssignments_.find(state.name);
        if (it != slicerFrameAssignments_.end()) {
            auto dit = it->second.find(dir);
            if (dit != it->second.end()) {
                for (int f : dit->second) currentAssigned.insert(f);
            }
        }
    }

    ImVec2 mousePos = ImGui::GetMousePos();
    slicerHoveredFrame_ = -1;

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < columns; ++col) {
            int frameIdx = row * columns + col;
            float x0 = cursorPos.x + col * cellDispW;
            float y0 = cursorPos.y + row * cellDispH;
            float x1 = x0 + cellDispW;
            float y1 = y0 + cellDispH;

            drawList->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), gridColor);

            if (currentAssigned.count(frameIdx)) {
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), assignedColor);
            }

            if (mousePos.x >= x0 && mousePos.x < x1 &&
                mousePos.y >= y0 && mousePos.y < y1) {
                slicerHoveredFrame_ = frameIdx;
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), hoverColor);
            }

            char label[8];
            snprintf(label, sizeof(label), "%d", frameIdx);
            drawList->AddText(ImVec2(x0 + 2, y0 + 1), IM_COL32(255, 255, 0, 200), label);
        }
    }

    // Click to assign frame to current state+direction
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && slicerHoveredFrame_ >= 0) {
        if (selectedStateIdx_ >= 0 && selectedStateIdx_ < (int)template_.states.size()) {
            auto& state = template_.states[selectedStateIdx_];
            std::string dir = directionName(selectedDirection_);
            slicerFrameAssignments_[state.name][dir].push_back(slicerHoveredFrame_);
            state.frameCount[dir] = (int)slicerFrameAssignments_[state.name][dir].size();
        }
    }

    ImGui::Dummy(ImVec2(dispW, dispH));
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Slicer Mode: Frame Strip
// ---------------------------------------------------------------------------
void AnimationEditor::drawSlicerFrameStrip() {
    if (selectedStateIdx_ < 0 || selectedStateIdx_ >= (int)template_.states.size()) return;

    auto& state = template_.states[selectedStateIdx_];
    std::string dir = directionName(selectedDirection_);
    auto& frames = slicerFrameAssignments_[state.name][dir];

    ImGui::Text("Frames (%d):", (int)frames.size());
    ImGui::BeginChild("##slicerStrip", ImVec2(0, 64), true, ImGuiWindowFlags_HorizontalScrollbar);

    auto tex = TextureCache::instance().get(sheetTexturePath_);
    int texW = tex ? tex->width() : 1;
    int texH = tex ? tex->height() : 1;
    int columns = (slicerCellW_ > 0) ? texW / slicerCellW_ : 1;

    for (int i = 0; i < (int)frames.size(); ++i) {
        int frameIdx = frames[i];
        int col = frameIdx % columns;
        int row = frameIdx / columns;
        float u0 = (float)(col * slicerCellW_) / texW;
        float u1 = u0 + (float)slicerCellW_ / texW;
        float v_bottom = (float)(row * slicerCellH_) / texH;
        float v_top = v_bottom + (float)slicerCellH_ / texH;
        ImVec2 uv0(u0, 1.0f - v_top);
        ImVec2 uv1(u1, 1.0f - v_bottom);

        ImGui::PushID(i);
        ImTextureID slicerTexId = (ImTextureID)(intptr_t)(tex ? tex->id() : 0);
        if (ImGui::ImageButton("##fr", slicerTexId, ImVec2(48, 48), uv0, uv1)) {
            // Select frame
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Frame")) {
                frames.erase(frames.begin() + i);
                state.frameCount[dir] = (int)frames.size();
                ImGui::EndPopup();
                ImGui::PopID();
                break;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::SameLine();
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Slicer Mode: openWithSheet entry point (stub — full impl in Task 7)
// ---------------------------------------------------------------------------
void AnimationEditor::openWithSheet(const std::string& texturePath) {
    open_ = true;
    slicerMode_ = true;
    sheetTexturePath_ = texturePath;
    sheetTexture_ = loadFrameTexture(texturePath);
}

// ---------------------------------------------------------------------------
// Slicer Mode: meta JSON save/load
// ---------------------------------------------------------------------------
void AnimationEditor::saveMetaJson(const std::string& sheetPath) {
    std::string metaPath = sheetPath;
    auto dotPos = metaPath.rfind('.');
    if (dotPos != std::string::npos)
        metaPath = metaPath.substr(0, dotPos) + ".meta.json";

    auto tex = TextureCache::instance().get(sheetPath);
    int sheetW = tex ? tex->width() : slicerCellW_;
    int sheetH = tex ? tex->height() : slicerCellH_;
    int columns = (slicerCellW_ > 0) ? sheetW / slicerCellW_ : 1;
    int rows = (slicerCellH_ > 0) ? sheetH / slicerCellH_ : 1;

    nlohmann::json j;
    j["version"] = 1;
    j["frameWidth"] = slicerCellW_;
    j["frameHeight"] = slicerCellH_;
    j["columns"] = columns;
    j["totalFrames"] = columns * rows;
    j["states"] = nlohmann::json::object();

    static const char* dirs[] = {"down", "up", "side"};
    for (auto& state : template_.states) {
        for (int d = 0; d < 3; ++d) {
            std::string dirKey = dirs[d];
            auto& assigned = slicerFrameAssignments_[state.name][dirKey];
            if (assigned.empty()) continue;

            int startFrame = assigned.front();
            int frameCount = (int)assigned.size();

            nlohmann::json sj;
            sj["startFrame"] = startFrame;
            sj["frameCount"] = frameCount;
            sj["frameRate"] = state.frameRate;
            sj["loop"] = state.loop;
            sj["hitFrame"] = state.hitFrame;

            if (dirKey == "side") {
                j["states"][state.name + "_right"] = sj;
                sj["flipX"] = true;
                j["states"][state.name + "_left"] = sj;
            } else {
                j["states"][state.name + "_" + dirKey] = sj;
            }
        }
    }

    // Save to build dir
    {
        auto parentDir = fs::path(metaPath).parent_path();
        if (!parentDir.empty() && !fs::exists(parentDir))
            fs::create_directories(parentDir);
        std::ofstream f(metaPath);
        if (f.is_open()) {
            f << j.dump(2);
            LOG_INFO("AnimEditor", "Saved meta: %s", metaPath.c_str());
        }
    }

    // Dual-save to source dir
    if (!sourceDir_.empty()) {
        std::string srcPath = sourceDir_ + "/" + metaPath;
        auto parentDir = fs::path(srcPath).parent_path();
        if (!parentDir.empty() && !fs::exists(parentDir))
            fs::create_directories(parentDir);
        std::ofstream f(srcPath);
        if (f.is_open()) {
            f << j.dump(2);
            LOG_INFO("AnimEditor", "Saved meta (source): %s", srcPath.c_str());
        }
    }
}

void AnimationEditor::loadMetaJson(const std::string& sheetPath) {
    std::string metaPath = sheetPath;
    auto dotPos = metaPath.rfind('.');
    if (dotPos != std::string::npos)
        metaPath = metaPath.substr(0, dotPos) + ".meta.json";

    PackedSheetMeta meta;
    if (!AnimationLoader::loadPackedMeta(metaPath, meta)) {
        LOG_WARN("AnimEditor", "No meta found: %s", metaPath.c_str());
        return;
    }

    slicerCellW_ = meta.frameWidth;
    slicerCellH_ = meta.frameHeight;
    reconstructStatesFromMeta(meta);
    LOG_INFO("AnimEditor", "Loaded meta: %s (%d states)", metaPath.c_str(),
             (int)template_.states.size());
}

void AnimationEditor::reconstructStatesFromMeta(const PackedSheetMeta& meta) {
    static const char* directions[] = {"_down", "_up", "_left", "_right"};

    std::unordered_map<std::string, std::unordered_map<std::string, const PackedStateMeta*>> grouped;
    for (auto& [name, s] : meta.states) {
        std::string base = name;
        std::string dir = "down";
        for (auto* suffix : directions) {
            size_t dirLen = std::strlen(suffix);
            if (name.size() > dirLen && name.substr(name.size() - dirLen) == suffix) {
                base = name.substr(0, name.size() - dirLen);
                dir = suffix + 1; // skip underscore
                break;
            }
        }
        if (dir == "left" || dir == "right") dir = "side";
        grouped[base][dir] = &s;
    }

    template_.states.clear();
    slicerFrameAssignments_.clear();

    for (auto& [base, dirMap] : grouped) {
        AnimState state;
        state.name = base;
        auto* first = dirMap.begin()->second;
        state.frameRate = first->frameRate;
        state.loop = first->loop;
        state.hitFrame = first->hitFrame;

        for (auto& [dir, sm] : dirMap) {
            state.frameCount[dir] = sm->frameCount;
            std::vector<int> frames;
            for (int i = 0; i < sm->frameCount; ++i)
                frames.push_back(sm->startFrame + i);
            slicerFrameAssignments_[base][dir] = frames;
        }

        template_.states.push_back(state);
    }
}

// ---------------------------------------------------------------------------
// Slicer Mode: quick-template stubs (implemented in Task 6)
// ---------------------------------------------------------------------------
void AnimationEditor::newMobTemplate() {
    // Implemented in Task 6
}

void AnimationEditor::newPlayerTemplate() {
    // Implemented in Task 6
}

} // namespace fate
