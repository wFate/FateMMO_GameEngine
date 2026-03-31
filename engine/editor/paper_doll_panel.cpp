#include "engine/editor/paper_doll_panel.h"
#ifdef FATE_HAS_GAME
#include "game/data/paper_doll_catalog.h"
#endif // FATE_HAS_GAME
#include "engine/render/texture.h"
#include "engine/core/logger.h"
#include <imgui.h>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace fate {

// ---------------------------------------------------------------------------
// Main draw
// ---------------------------------------------------------------------------

void PaperDollPanel::draw() {
    if (!open_) return;

#ifndef FATE_HAS_GAME
    ImGui::SetNextWindowSize({700, 500}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Paper Doll Manager", &open_)) {
        ImGui::End();
        return;
    }
    ImGui::Text("Paper Doll Manager requires game code");
    ImGui::End();
    return;
#else
    ImGui::SetNextWindowSize({700, 500}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Paper Doll Manager", &open_)) {
        ImGui::End();
        return;
    }

    auto& catalog = PaperDollCatalog::instance();
    if (!catalog.isLoaded()) {
        ImGui::Text("Catalog not loaded. Check assets/paper_doll.json");
        ImGui::End();
        return;
    }

    // Save button at top-right
    float saveX = ImGui::GetWindowWidth() - 80.0f;
    ImGui::SameLine(saveX);
    if (ImGui::Button("Save")) {
        catalog.save("");  // uses stored absolute path from load()
    }

    // Split: left = preview, right = tabs
    ImGui::Columns(2, "##PaperDollCols", true);
    ImGui::SetColumnWidth(0, 200.0f);

    // === LEFT: Composite Preview ===
    drawCompositePreview();

    ImGui::NextColumn();

    // === RIGHT: Category Tabs ===
    if (ImGui::BeginTabBar("##PaperDollTabs")) {
        if (ImGui::BeginTabItem("Bodies"))     { selectedTab_ = 0; drawBodiesTab();                    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Hair"))       { selectedTab_ = 1; drawHairTab();                      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Armor"))      { selectedTab_ = 2; drawEquipmentTab("armor");          ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Hat"))        { selectedTab_ = 3; drawEquipmentTab("hat");            ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Weapon"))     { selectedTab_ = 4; drawEquipmentTab("weapon");         ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Animations")) { selectedTab_ = 5; drawAnimationsTab();                ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::Columns(1);
    ImGui::End();
#endif // FATE_HAS_GAME
}

// ---------------------------------------------------------------------------
// Composite Preview (left panel)
// ---------------------------------------------------------------------------

#ifdef FATE_HAS_GAME
void PaperDollPanel::drawCompositePreview() {
    auto& catalog = PaperDollCatalog::instance();
    const char* genderStr = previewGender_ == 0 ? "Male" : "Female";

    // Direction selector
    const char* dirs[] = {"Front", "Back", "Side"};
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Direction", &previewDirection_, dirs, 3);

    // Gender selector
    const char* genders[] = {"Male", "Female"};
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Gender##preview", &previewGender_, genders, 2);

    ImGui::Separator();

    // Layer toggles
    ImGui::Checkbox("Body",   &showBody_);
    ImGui::Checkbox("Hair",   &showHair_);
    ImGui::Checkbox("Armor",  &showArmor_);
    ImGui::Checkbox("Hat",    &showHat_);
    ImGui::Checkbox("Weapon", &showWeapon_);

    ImGui::Separator();

    // Draw composite preview using ImGui drawlist
    float previewScale = 3.0f;
    float pw = catalog.frameWidth() * previewScale;
    float ph = catalog.frameHeight() * previewScale;
    ImVec2 previewPos = ImGui::GetCursorScreenPos();

    auto pickDir = [&](const SpriteSet& ss) -> std::shared_ptr<Texture> {
        switch (previewDirection_) {
            case 0: return ss.front;
            case 1: return ss.back;
            case 2: return ss.side;
            default: return ss.front;
        }
    };

    // Reserve space
    ImGui::Dummy({pw, ph});

    // Draw layers using ImGui draw list
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pMin = previewPos;
    ImVec2 pMax = {previewPos.x + pw, previewPos.y + ph};

    auto drawLayerPreview = [&](const SpriteSet& ss) {
        auto tex = pickDir(ss);
        if (!tex) return;
        // Source rect: first frame only
        float fw = static_cast<float>(catalog.frameWidth());
        float fh = static_cast<float>(catalog.frameHeight());
        float texW = static_cast<float>(tex->width());
        float texH = static_cast<float>(tex->height());
        if (texW == 0 || texH == 0) return;
        // GL textures are bottom-up, ImGui is top-down — flip V
        ImVec2 uv0 = {0, fh / texH};
        ImVec2 uv1 = {fw / texW, 0};
        dl->AddImage((ImTextureID)(intptr_t)tex->id(), pMin, pMax, uv0, uv1);
    };

    if (showBody_) drawLayerPreview(catalog.getBody(genderStr));
    if (showHair_ && !previewHairName_.empty())
        drawLayerPreview(catalog.getHairstyle(genderStr, previewHairName_));
    if (showArmor_ && !previewArmorStyle_.empty())
        drawLayerPreview(catalog.getEquipment("armor", previewArmorStyle_));
    if (showHat_ && !previewHatStyle_.empty())
        drawLayerPreview(catalog.getEquipment("hat", previewHatStyle_));
    if (showWeapon_ && !previewWeaponStyle_.empty())
        drawLayerPreview(catalog.getEquipment("weapon", previewWeaponStyle_));
}

// ---------------------------------------------------------------------------
// Bodies Tab
// ---------------------------------------------------------------------------

void PaperDollPanel::drawBodiesTab() {
    auto& catalog = PaperDollCatalog::instance();

    // Iterate the known genders
    const char* genders[] = {"Male", "Female"};
    for (const char* gender : genders) {
        if (ImGui::TreeNode(gender)) {
            auto paths = catalog.getBodyPaths(gender);

            // Front
            ImGui::Text("Front:"); ImGui::SameLine();
            ImGui::TextDisabled("%s", paths.front.empty() ? "(none)" : paths.front.c_str());
            ImGui::SameLine();
            std::string frontPath = paths.front;
            if (browseButton(("##body_front_" + std::string(gender)).c_str(), frontPath)) {
                catalog.setBodyPath(gender, "front", frontPath);
            }

            // Back
            ImGui::Text("Back: "); ImGui::SameLine();
            ImGui::TextDisabled("%s", paths.back.empty() ? "(none)" : paths.back.c_str());
            ImGui::SameLine();
            std::string backPath = paths.back;
            if (browseButton(("##body_back_" + std::string(gender)).c_str(), backPath)) {
                catalog.setBodyPath(gender, "back", backPath);
            }

            // Side
            ImGui::Text("Side: "); ImGui::SameLine();
            ImGui::TextDisabled("%s", paths.side.empty() ? "(none)" : paths.side.c_str());
            ImGui::SameLine();
            std::string sidePath = paths.side;
            if (browseButton(("##body_side_" + std::string(gender)).c_str(), sidePath)) {
                catalog.setBodyPath(gender, "side", sidePath);
            }

            ImGui::TreePop();
        }
    }
}

// ---------------------------------------------------------------------------
// Hair Tab
// ---------------------------------------------------------------------------

void PaperDollPanel::drawHairTab() {
    auto& catalog = PaperDollCatalog::instance();
    const char* genderStr = previewGender_ == 0 ? "Male" : "Female";

    // Gender selector
    const char* genders[] = {"Male", "Female"};
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("Gender##hair", &previewGender_, genders, 2);

    auto names = catalog.getHairstyleNames(genderStr);

    // Thumbnail grid (text buttons)
    for (size_t i = 0; i < names.size(); ++i) {
        const auto& name = names[i];
        bool selected = (name == selectedEntry_ && selectedTab_ == 1);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        if (ImGui::Button(name.c_str(), {100, 30})) {
            selectedEntry_ = name;
            previewHairName_ = name;
        }
        if (selected) ImGui::PopStyleColor();
        if (i + 1 < names.size()) ImGui::SameLine();
    }
    ImGui::NewLine();

    // Add / Remove
    static char newName[64] = "";
    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputText("##newHairName", newName, sizeof(newName));
    ImGui::SameLine();
    if (ImGui::Button("+ Add##hair")) {
        if (std::strlen(newName) > 0) {
            catalog.addHairstyle(genderStr, newName);
            selectedEntry_ = newName;
            previewHairName_ = newName;
            newName[0] = '\0';
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("- Remove##hair") && !selectedEntry_.empty()) {
        catalog.removeHairstyle(genderStr, selectedEntry_);
        selectedEntry_.clear();
        previewHairName_.clear();
    }

    ImGui::Separator();

    // Selected entry details -- 3 direction paths with browse buttons
    if (!selectedEntry_.empty()) {
        ImGui::Text("Selected: %s", selectedEntry_.c_str());
        auto paths = catalog.getHairstylePaths(genderStr, selectedEntry_);

        ImGui::Text("Front:"); ImGui::SameLine();
        ImGui::TextDisabled("%s", paths.front.empty() ? "(none)" : paths.front.c_str());
        ImGui::SameLine();
        std::string fp = paths.front;
        if (browseButton(("##hair_front_" + selectedEntry_).c_str(), fp)) {
            catalog.setHairstylePath(genderStr, selectedEntry_, "front", fp);
        }

        ImGui::Text("Back: "); ImGui::SameLine();
        ImGui::TextDisabled("%s", paths.back.empty() ? "(none)" : paths.back.c_str());
        ImGui::SameLine();
        std::string bp = paths.back;
        if (browseButton(("##hair_back_" + selectedEntry_).c_str(), bp)) {
            catalog.setHairstylePath(genderStr, selectedEntry_, "back", bp);
        }

        ImGui::Text("Side: "); ImGui::SameLine();
        ImGui::TextDisabled("%s", paths.side.empty() ? "(none)" : paths.side.c_str());
        ImGui::SameLine();
        std::string sp = paths.side;
        if (browseButton(("##hair_side_" + selectedEntry_).c_str(), sp)) {
            catalog.setHairstylePath(genderStr, selectedEntry_, "side", sp);
        }
    }
}

// ---------------------------------------------------------------------------
// Equipment Tab (reusable for armor / hat / weapon)
// ---------------------------------------------------------------------------

void PaperDollPanel::drawEquipmentTab(const std::string& category) {
    auto& catalog = PaperDollCatalog::instance();

    auto styles = catalog.getEquipmentStyles(category);

    // Style buttons
    for (size_t i = 0; i < styles.size(); ++i) {
        const auto& style = styles[i];
        bool selected = (style == selectedEntry_ && (
            (category == "armor"  && selectedTab_ == 2) ||
            (category == "hat"    && selectedTab_ == 3) ||
            (category == "weapon" && selectedTab_ == 4)));
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        if (ImGui::Button((style + "##" + category).c_str(), {100, 30})) {
            selectedEntry_ = style;
            if (category == "armor")       previewArmorStyle_  = style;
            else if (category == "hat")    previewHatStyle_    = style;
            else if (category == "weapon") previewWeaponStyle_ = style;
        }
        if (selected) ImGui::PopStyleColor();
        if (i + 1 < styles.size()) ImGui::SameLine();
    }
    ImGui::NewLine();

    // Add / Remove
    static char newStyle[64] = "";
    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputText(("##newStyle_" + category).c_str(), newStyle, sizeof(newStyle));
    ImGui::SameLine();
    if (ImGui::Button(("+ Add##eq_" + category).c_str())) {
        if (std::strlen(newStyle) > 0) {
            catalog.addEquipmentStyle(category, newStyle);
            selectedEntry_ = newStyle;
            if (category == "armor")       previewArmorStyle_  = newStyle;
            else if (category == "hat")    previewHatStyle_    = newStyle;
            else if (category == "weapon") previewWeaponStyle_ = newStyle;
            newStyle[0] = '\0';
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(("- Remove##eq_" + category).c_str()) && !selectedEntry_.empty()) {
        catalog.removeEquipmentStyle(category, selectedEntry_);
        if (category == "armor")       previewArmorStyle_.clear();
        else if (category == "hat")    previewHatStyle_.clear();
        else if (category == "weapon") previewWeaponStyle_.clear();
        selectedEntry_.clear();
    }

    ImGui::Separator();

    // Selected entry details
    if (!selectedEntry_.empty()) {
        ImGui::Text("Selected: %s", selectedEntry_.c_str());
        auto paths = catalog.getEquipmentPaths(category, selectedEntry_);

        ImGui::Text("Front:"); ImGui::SameLine();
        ImGui::TextDisabled("%s", paths.front.empty() ? "(none)" : paths.front.c_str());
        ImGui::SameLine();
        std::string fp = paths.front;
        if (browseButton(("##eq_front_" + category + "_" + selectedEntry_).c_str(), fp)) {
            catalog.setEquipmentPath(category, selectedEntry_, "front", fp);
        }

        ImGui::Text("Back: "); ImGui::SameLine();
        ImGui::TextDisabled("%s", paths.back.empty() ? "(none)" : paths.back.c_str());
        ImGui::SameLine();
        std::string bp = paths.back;
        if (browseButton(("##eq_back_" + category + "_" + selectedEntry_).c_str(), bp)) {
            catalog.setEquipmentPath(category, selectedEntry_, "back", bp);
        }

        ImGui::Text("Side: "); ImGui::SameLine();
        ImGui::TextDisabled("%s", paths.side.empty() ? "(none)" : paths.side.c_str());
        ImGui::SameLine();
        std::string sp = paths.side;
        if (browseButton(("##eq_side_" + category + "_" + selectedEntry_).c_str(), sp)) {
            catalog.setEquipmentPath(category, selectedEntry_, "side", sp);
        }
    }
}

// ---------------------------------------------------------------------------
// Animations Tab (read-only display)
// ---------------------------------------------------------------------------

void PaperDollPanel::drawAnimationsTab() {
    auto& catalog = PaperDollCatalog::instance();
    auto names = catalog.getAnimationNames();

    for (const auto& name : names) {
        if (ImGui::TreeNode(name.c_str())) {
            auto info = catalog.getAnimation(name);
            ImGui::Text("Start Frame: %d", info.startFrame);
            ImGui::Text("Frame Count: %d", info.frameCount);
            ImGui::Text("Frame Rate:  %.1f", info.frameRate);
            ImGui::Text("Loop:        %s", info.loop ? "Yes" : "No");
            if (info.hitFrame >= 0) ImGui::Text("Hit Frame:   %d", info.hitFrame);

            if (!info.layerOffsetsY.empty()) {
                ImGui::Text("Layer Offsets:");
                ImGui::Indent();
                for (const auto& [layer, offsets] : info.layerOffsetsY) {
                    std::string vals;
                    for (size_t i = 0; i < offsets.size(); ++i) {
                        if (i > 0) vals += ", ";
                        vals += std::to_string(static_cast<int>(offsets[i]));
                    }
                    ImGui::Text("%s Y: [%s]", layer.c_str(), vals.c_str());
                }
                ImGui::Unindent();
            }
            ImGui::TreePop();
        }
    }
}

#endif // FATE_HAS_GAME

// ---------------------------------------------------------------------------
// Browse Button Helper
// ---------------------------------------------------------------------------

bool PaperDollPanel::browseButton(const char* id, std::string& path) {
#ifdef _WIN32
    ImGui::PushID(id);
    bool clicked = ImGui::Button("Browse");
    ImGui::PopID();
    if (!clicked) return false;

    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "PNG Files\0*.png\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        // Make path relative to assets/ if possible
        std::string absPath = filename;
        // Convert backslashes to forward slashes
        for (auto& c : absPath) if (c == '\\') c = '/';
        // Try to find "assets/" in the path and make relative
        auto pos = absPath.find("assets/");
        if (pos != std::string::npos) {
            path = absPath.substr(pos);
        } else {
            path = absPath;
        }
        return true;
    }
    return false;
#else
    ImGui::PushID(id);
    char buf[256];
    std::strncpy(buf, path.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText("", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        path = buf;
        ImGui::PopID();
        return true;
    }
    ImGui::PopID();
    return false;
#endif
}

} // namespace fate
