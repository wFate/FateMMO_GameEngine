#include "engine/editor/editor.h"
#include "engine/core/logger.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "engine/ecs/component_meta.h"
#ifdef FATE_HAS_GAME
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/player_controller.h"
#include "game/components/box_collider.h"
#include "game/components/polygon_collider.h"
#include "game/components/animator.h"
#include "game/components/zone_component.h"
#include "game/components/game_components.h"
#include "game/components/faction_component.h"
#include "game/components/pet_component.h"
#include "game/shared/faction_npc_data.h"
#include "game/components/spawn_point_component.h"
#include "game/systems/spawn_system.h"
#endif // FATE_HAS_GAME
#include "engine/scene/scene.h"
#include "engine/scene/scene_manager.h"
#include "engine/editor/undo.h"
#include "engine/ecs/prefab.h"
#ifdef FATE_HAS_GAME
#include "game/animation_loader.h"
#include "game/data/paper_doll_catalog.h"
#endif // FATE_HAS_GAME
#include "engine/render/text_style.h"
#include "engine/render/font_registry.h"
#include "game/systems/combat_text_config.h"
#include <unordered_set>
#include <string>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================================
// Text style inspector helper (nameplate font/style/effects)
// ============================================================================

static bool inspectTextStyle(const char* prefix,
                             std::string& fontName,
                             fate::TextStyle& textStyle,
                             fate::TextEffects& textEffects) {
    bool changed = false;

    // Font name combo
    auto fontNames = fate::FontRegistry::instance().fontNames();
    char label[64];
    std::snprintf(label, sizeof(label), "Font##%s", prefix);
    int currentFont = 0; // 0 = "(default)"
    std::vector<const char*> items;
    items.push_back("(default)");
    for (int i = 0; i < (int)fontNames.size(); ++i) {
        items.push_back(fontNames[i].c_str());
        if (fontNames[i] == fontName) currentFont = i + 1;
    }
    if (ImGui::Combo(label, &currentFont, items.data(), (int)items.size())) {
        fontName = (currentFont == 0) ? "" : fontNames[currentFont - 1];
        changed = true;
    }

    // Text style combo
    std::snprintf(label, sizeof(label), "Text Style##%s", prefix);
    int styleIdx = static_cast<int>(textStyle) - 1; // enum starts at 1
    if (styleIdx < 0 || styleIdx >= fate::kTextStyleCount) styleIdx = 0;
    if (ImGui::Combo(label, &styleIdx, fate::kTextStyleNames, fate::kTextStyleCount)) {
        textStyle = static_cast<fate::TextStyle>(styleIdx + 1);
        changed = true;
    }

    // Text effects (collapsible)
    std::snprintf(label, sizeof(label), "Text Effects##%s", prefix);
    if (ImGui::TreeNode(label)) {
        std::snprintf(label, sizeof(label), "Outline Color##%s", prefix);
        changed |= ImGui::ColorEdit4(label, &textEffects.outlineColor.r);
        std::snprintf(label, sizeof(label), "Outline Width##%s", prefix);
        changed |= ImGui::DragFloat(label, &textEffects.outlineWidth, 0.1f, 0.0f, 10.0f, "%.1f");
        std::snprintf(label, sizeof(label), "Glow Color##%s", prefix);
        changed |= ImGui::ColorEdit4(label, &textEffects.glowColor.r);
        std::snprintf(label, sizeof(label), "Glow Radius##%s", prefix);
        changed |= ImGui::DragFloat(label, &textEffects.glowRadius, 0.1f, 0.0f, 20.0f, "%.1f");
        std::snprintf(label, sizeof(label), "Shadow Color##%s", prefix);
        changed |= ImGui::ColorEdit4(label, &textEffects.shadowColor.r);
        std::snprintf(label, sizeof(label), "Shadow X##%s", prefix);
        changed |= ImGui::DragFloat(label, &textEffects.shadowOffset.x, 0.5f, -20.0f, 20.0f, "%.1f");
        std::snprintf(label, sizeof(label), "Shadow Y##%s", prefix);
        changed |= ImGui::DragFloat(label, &textEffects.shadowOffset.y, 0.5f, -20.0f, 20.0f, "%.1f");
        ImGui::TreePop();
    }

    return changed;
}

// ============================================================================
// CombatTextStyle inspector helper
// ============================================================================

static void inspectCombatTextStyle(const char* name, fate::CombatTextStyle& s) {
    if (!ImGui::TreeNode(name)) return;

    char label[96];
    char buf[64];

    // --- Core ---
    std::snprintf(label, sizeof(label), "Display Text##%s", name);
    strncpy(buf, s.text.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    if (ImGui::InputText(label, buf, sizeof(buf))) s.text = buf;

    std::snprintf(label, sizeof(label), "Color##%s", name);
    ImGui::ColorEdit4(label, &s.color.r);
    std::snprintf(label, sizeof(label), "Outline Color##%s", name);
    ImGui::ColorEdit4(label, &s.outlineColor.r);
    std::snprintf(label, sizeof(label), "Font Size##%s", name);
    ImGui::DragFloat(label, &s.fontSize, 0.5f, 4.0f, 48.0f, "%.1f");
    std::snprintf(label, sizeof(label), "Scale##%s", name);
    ImGui::DragFloat(label, &s.scale, 0.05f, 0.1f, 5.0f, "%.2f");

    // --- Motion ---
    if (ImGui::TreeNode("Motion")) {
        std::snprintf(label, sizeof(label), "Lifetime##%s", name);
        ImGui::DragFloat(label, &s.lifetime, 0.05f, 0.1f, 5.0f, "%.2fs");
        std::snprintf(label, sizeof(label), "Float Speed##%s", name);
        ImGui::DragFloat(label, &s.floatSpeed, 1.0f, 0.0f, 200.0f, "%.0f px/s");
        std::snprintf(label, sizeof(label), "Float Angle##%s", name);
        ImGui::DragFloat(label, &s.floatAngle, 1.0f, 0.0f, 360.0f, "%.0f deg");
        std::snprintf(label, sizeof(label), "Start Offset Y##%s", name);
        ImGui::DragFloat(label, &s.startOffsetY, 0.5f, -50.0f, 50.0f, "%.1f");
        std::snprintf(label, sizeof(label), "Random Spread X##%s", name);
        ImGui::DragFloat(label, &s.randomSpreadX, 0.5f, 0.0f, 50.0f, "%.1f");
        ImGui::TreePop();
    }

    // --- Fade & Pop ---
    if (ImGui::TreeNode("Fade & Pop")) {
        std::snprintf(label, sizeof(label), "Fade Delay##%s", name);
        ImGui::DragFloat(label, &s.fadeDelay, 0.05f, 0.0f, 3.0f, "%.2fs");
        std::snprintf(label, sizeof(label), "Pop Scale##%s", name);
        ImGui::DragFloat(label, &s.popScale, 0.05f, 0.5f, 5.0f, "%.2f");
        std::snprintf(label, sizeof(label), "Pop Duration##%s", name);
        ImGui::DragFloat(label, &s.popDuration, 0.01f, 0.0f, 1.0f, "%.2fs");
        ImGui::TreePop();
    }

    // --- Font & Text Style ---
    std::snprintf(label, sizeof(label), "ctFont_%s", name);
    inspectTextStyle(label, s.fontName, s.textStyle, s.textEffects);

    // --- Label (e.g., "CRIT!") ---
    if (ImGui::TreeNode("Label")) {
        std::snprintf(label, sizeof(label), "Label Text##%s", name);
        strncpy(buf, s.label.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
        if (ImGui::InputText(label, buf, sizeof(buf))) s.label = buf;
        std::snprintf(label, sizeof(label), "Label Font Size##%s", name);
        ImGui::DragFloat(label, &s.labelFontSize, 0.5f, 4.0f, 48.0f, "%.1f");
        std::snprintf(label, sizeof(label), "Label Color##%s", name);
        ImGui::ColorEdit4(label, &s.labelColor.r);
        std::snprintf(label, sizeof(label), "Label Offset Y##%s", name);
        ImGui::DragFloat(label, &s.labelOffsetY, 0.5f, -50.0f, 50.0f, "%.1f");
        std::snprintf(label, sizeof(label), "ctLabelFont_%s", name);
        inspectTextStyle(label, s.labelFontName, s.labelTextStyle, s.labelTextEffects);
        ImGui::TreePop();
    }

    ImGui::TreePop();
}

// ============================================================================
// Reflection-driven inspector helper
// ============================================================================

static void drawReflectedComponent(const fate::ComponentMeta& meta, void* data) {
    for (const auto& field : meta.fields) {
        uint8_t* ptr = static_cast<uint8_t*>(data) + field.offset;
        switch (field.type) {
            case fate::FieldType::Float:
                ImGui::DragFloat(field.name, reinterpret_cast<float*>(ptr), 0.1f);
                fate::Editor::instance().captureInspectorUndo();
                break;
            case fate::FieldType::Int:
                ImGui::DragInt(field.name, reinterpret_cast<int*>(ptr));
                fate::Editor::instance().captureInspectorUndo();
                break;
            case fate::FieldType::Bool:
                ImGui::Checkbox(field.name, reinterpret_cast<bool*>(ptr));
                fate::Editor::instance().captureInspectorUndo();
                break;
            case fate::FieldType::Vec2: {
                auto* v = reinterpret_cast<fate::Vec2*>(ptr);
                float vals[2] = { v->x, v->y };
                if (ImGui::DragFloat2(field.name, vals, 0.5f)) {
                    v->x = vals[0]; v->y = vals[1];
                }
                fate::Editor::instance().captureInspectorUndo();
                break;
            }
            case fate::FieldType::Color: {
                auto* c = reinterpret_cast<fate::Color*>(ptr);
                ImGui::ColorEdit4(field.name, &c->r);
                fate::Editor::instance().captureInspectorUndo();
                break;
            }
            case fate::FieldType::Rect: {
                auto* r = reinterpret_cast<fate::Rect*>(ptr);
                float vals[4] = { r->x, r->y, r->w, r->h };
                if (ImGui::DragFloat4(field.name, vals, 0.5f)) {
                    r->x = vals[0]; r->y = vals[1]; r->w = vals[2]; r->h = vals[3];
                }
                fate::Editor::instance().captureInspectorUndo();
                break;
            }
            case fate::FieldType::String: {
                auto* s = reinterpret_cast<std::string*>(ptr);
                char buf[256] = {};
                strncpy(buf, s->c_str(), sizeof(buf) - 1);
                if (ImGui::InputText(field.name, buf, sizeof(buf))) {
                    *s = buf;
                }
                fate::Editor::instance().captureInspectorUndo();
                break;
            }
            case fate::FieldType::UInt:
                ImGui::DragScalar(field.name, ImGuiDataType_U32, reinterpret_cast<uint32_t*>(ptr));
                fate::Editor::instance().captureInspectorUndo();
                break;
            default:
                ImGui::TextDisabled("%s: [custom/unsupported]", field.name);
                break;
        }
    }
}

namespace fate {

// ============================================================================
// Inspector undo capture
// ============================================================================

void Editor::captureInspectorUndo() {
    if (!selectedEntity_) return;
    if (ImGui::IsItemActivated()) {
        pendingInspectorSnapshot_ = PrefabLibrary::entityToJson(selectedEntity_);
        pendingInspectorHandle_ = selectedEntity_->handle();
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && !pendingInspectorSnapshot_.is_null()) {
        auto newState = PrefabLibrary::entityToJson(selectedEntity_);
        if (newState != pendingInspectorSnapshot_) {
            auto cmd = std::make_unique<PropertyCommand>();
            cmd->entityHandle = pendingInspectorHandle_;
            cmd->oldState = std::move(pendingInspectorSnapshot_);
            cmd->newState = std::move(newState);
            cmd->desc = "Inspector edit";
            UndoSystem::instance().push(std::move(cmd));
        }
        pendingInspectorSnapshot_ = nlohmann::json();
    }
}

// ============================================================================
// Inspector
// ============================================================================

void Editor::drawInspector() {
    if (ImGui::Begin("Inspector")) {
        if (!selectedEntity_) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select an entity");
            ImGui::End();
            return;
        }

        // -- Entity name (prominent input at top) --
        char nameBuf[128];
        strncpy(nameBuf, selectedEntity_->name().c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (fontHeading_) ImGui::PushFont(fontHeading_);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##EntityName", nameBuf, sizeof(nameBuf))) {
            selectedEntity_->setName(nameBuf);
        }
        captureInspectorUndo();
        if (fontHeading_) ImGui::PopFont();

        // Tag + Active on same line
        {
            char tagBuf[64];
            strncpy(tagBuf, selectedEntity_->tag().c_str(), sizeof(tagBuf) - 1);
            tagBuf[sizeof(tagBuf) - 1] = '\0';

            bool active = selectedEntity_->isActive();
            if (ImGui::Checkbox("##Active", &active)) {
                selectedEntity_->setActive(active);
            }
            captureInspectorUndo();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##Tag", "Tag", tagBuf, sizeof(tagBuf))) {
                selectedEntity_->setTag(tagBuf);
            }
            captureInspectorUndo();
        }

        ImGui::Spacing();

        // Helper macro for a two-column property row
        #define INSPECTOR_ROW(labelText) \
            ImGui::TableNextRow(); \
            ImGui::TableSetColumnIndex(0); \
            ImGui::AlignTextToFramePadding(); \
            ImGui::Text(labelText); \
            ImGui::TableSetColumnIndex(1); \
            ImGui::SetNextItemWidth(-1)

#ifdef FATE_HAS_GAME
        // Transform
        if (auto* t = selectedEntity_->getComponent<Transform>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool transformOpen = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
            if (fontHeading_) ImGui::PopFont();
            if (transformOpen) {
                if (ImGui::BeginTable("##TransformProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Position");
                    ImGui::DragFloat2("##pos", &t->position.x, 1.0f);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Tile");
                    Vec2 tile = Coords::toTile(t->position);
                    ImGui::Text("(%d, %d)", (int)tile.x, (int)tile.y);

                    INSPECTOR_ROW("Scale");
                    ImGui::DragFloat2("##scale", &t->scale.x, 0.01f, 0.01f, 10.0f);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Rotation");
                    float degrees = t->rotation * 57.2957795f;
                    if (ImGui::DragFloat("##rot", &degrees, 1.0f, -360.0f, 360.0f)) {
                        t->rotation = degrees * 0.0174532925f;
                    }
                    captureInspectorUndo();

                    INSPECTOR_ROW("Depth");
                    ImGui::DragFloat("##depth", &t->depth, 0.1f);
                    captureInspectorUndo();

                    ImGui::EndTable();
                }
            }
        }

        // Sprite
        if (auto* s = selectedEntity_->getComponent<SpriteComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool spriteOpen = ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen);
            if (fontHeading_) ImGui::PopFont();
            if (spriteOpen) {
                if (ImGui::BeginTable("##SpriteProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Texture");
                    {
                        // Sprite texture selector — dropdown of available sprite assets
                        std::string currentName = s->texturePath.empty() ? "(none)" :
                            fs::path(s->texturePath).filename().string();
                        ImGui::SetNextItemWidth(-1);
                        auto beforeTexCombo = PrefabLibrary::entityToJson(selectedEntity_);
                        if (ImGui::BeginCombo("##sprTex", currentName.c_str())) {
                            // Option to clear
                            if (ImGui::Selectable("(none)", s->texturePath.empty())) {
                                s->texturePath.clear();
                                s->texture = nullptr;
                            }
                            // List all sprite assets
                            for (auto& asset : assets_) {
                                if (asset.type != AssetType::Sprite) continue;
                                bool selected = (asset.relativePath == s->texturePath);
                                if (ImGui::Selectable(asset.name.c_str(), selected)) {
                                    s->texturePath = asset.relativePath;
                                    s->texture = TextureCache::instance().load(asset.relativePath);
                                    if (s->texture) {
                                        s->size = {(float)s->texture->width(), (float)s->texture->height()};
                                    }
                                }
                            }
                            ImGui::EndCombo();
                        }
                        auto afterTexCombo = PrefabLibrary::entityToJson(selectedEntity_);
                        if (afterTexCombo != beforeTexCombo) {
                            auto cmd = std::make_unique<PropertyCommand>();
                            cmd->entityHandle = selectedEntity_->handle();
                            cmd->oldState = std::move(beforeTexCombo);
                            cmd->newState = std::move(afterTexCombo);
                            cmd->desc = "Inspector combo change";
                            UndoSystem::instance().push(std::move(cmd));
                        }
                    }

                    if (s->texture) {
                        INSPECTOR_ROW("Tex Size");
                        ImGui::Text("%dx%d", s->texture->width(), s->texture->height());

                        INSPECTOR_ROW("Preview");
                        ImTextureID texId = (ImTextureID)(intptr_t)s->texture->id();
                        ImGui::Image(texId, ImVec2(48, 48), ImVec2(0, 1), ImVec2(1, 0));
                    }

                    INSPECTOR_ROW("Size");
                    ImGui::DragFloat2("##sprSize", &s->size.x, 1.0f, 1.0f, 2048.0f);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Tint");
                    ImGui::ColorEdit4("##tint", &s->tint.r, ImGuiColorEditFlags_NoLabel);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Flip");
                    ImGui::Checkbox("X##flipX", &s->flipX);
                    captureInspectorUndo();
                    ImGui::SameLine();
                    ImGui::Checkbox("Y##flipY", &s->flipY);
                    captureInspectorUndo();

                    ImGui::EndTable();
                }

                // Source rect (UV region of the texture -- for tileset tiles)
                if (ImGui::TreeNode("Source Rect (UV)")) {
                    ImGui::DragFloat4("XYWH", &s->sourceRect.x, 0.01f, 0.0f, 1.0f);
                    captureInspectorUndo();
                    if (s->texture) {
                        int px = (int)(s->sourceRect.x * s->texture->width());
                        int py = (int)(s->sourceRect.y * s->texture->height());
                        int pw = (int)(s->sourceRect.w * s->texture->width());
                        int ph = (int)(s->sourceRect.h * s->texture->height());
                        ImGui::Text("Pixel region: (%d, %d) %dx%d", px, py, pw, ph);
                    }
                    ImGui::TreePop();
                }

                // --- Animation metadata (read-only) ---
                if (ImGui::TreeNode("Animation")) {
                    if (s->frameWidth > 0 && s->frameHeight > 0) {
                        ImGui::Text("Frame Size: %d x %d", s->frameWidth, s->frameHeight);
                        ImGui::Text("Grid: %d columns, %d total", s->columns, s->totalFrames);
                    } else {
                        ImGui::TextDisabled("No animation metadata loaded");
                    }

                    if (!s->texturePath.empty()) {
                        std::string metaName = fs::path(s->texturePath).stem().string() + ".meta.json";
                        std::string metaPath = s->texturePath;
                        auto dotPos = metaPath.rfind('.');
                        if (dotPos != std::string::npos)
                            metaPath = metaPath.substr(0, dotPos) + ".meta.json";
                        bool metaExists = fs::exists(metaPath);
                        ImGui::Text("Meta File: %s %s", metaName.c_str(), metaExists ? "" : "(not found)");
                    }

                    if (ImGui::Button("Open Animation Editor##sprite")) {
                        if (!s->texturePath.empty()) {
                            animationEditor_.openWithSheet(s->texturePath);
                        } else {
                            animationEditor_.setOpen(true);
                        }
                    }
                    ImGui::TreePop();
                }
            }
        }

        // BoxCollider
        if (auto* c = selectedEntity_->getComponent<BoxCollider>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Box Collider", ImGuiTreeNodeFlags_DefaultOpen);
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmBoxCollider")) {
                if (ImGui::MenuItem("Remove Component")) {
                    selectedEntity_->removeComponent<BoxCollider>();
                    ImGui::EndPopup();
                    goto endInspectorComponents;
                }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<BoxCollider>()) {
                if (ImGui::BeginTable("##BoxColProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Size");
                    ImGui::DragFloat2("##boxSize", &c->size.x, 0.5f, 1.0f, 512.0f);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Offset");
                    ImGui::DragFloat2("##boxOff", &c->offset.x, 0.5f);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Trigger");
                    ImGui::Checkbox("##boxTrig", &c->isTrigger);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Static");
                    ImGui::Checkbox("##boxStatic", &c->isStatic);
                    captureInspectorUndo();

                    ImGui::EndTable();
                }

                auto* spr = selectedEntity_->getComponent<SpriteComponent>();
                if (spr && ImGui::Button("Fit to Sprite##box")) {
                    c->size = spr->size;
                    c->offset = {0.0f, 0.0f};
                }
            }
        }

        // PolygonCollider
        if (auto* pc = selectedEntity_->getComponent<PolygonCollider>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Polygon Collider", ImGuiTreeNodeFlags_DefaultOpen);
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmPolyCollider")) {
                if (ImGui::MenuItem("Remove Component")) {
                    selectedEntity_->removeComponent<PolygonCollider>();
                    ImGui::EndPopup();
                    goto endInspectorComponents;
                }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<PolygonCollider>()) {
                if (ImGui::BeginTable("##PolyColProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Trigger");
                    ImGui::Checkbox("##polyTrig", &pc->isTrigger);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Static");
                    ImGui::Checkbox("##polyStatic", &pc->isStatic);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Vertices");
                    ImGui::Text("%zu", pc->points.size());

                    ImGui::EndTable();
                }

                int removeIdx = -1;
                for (int i = 0; i < (int)pc->points.size(); i++) {
                    ImGui::PushID(i);
                    char label[16];
                    snprintf(label, sizeof(label), "V%d", i);
                    ImGui::DragFloat2(label, &pc->points[i].x, 0.5f);
                    captureInspectorUndo();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) removeIdx = i;
                    ImGui::PopID();
                }
                if (removeIdx >= 0) {
                    pc->points.erase(pc->points.begin() + removeIdx);
                }

                if (ImGui::Button("+ Vertex")) {
                    if (pc->points.empty()) pc->points.push_back({-8, -8});
                    else { Vec2 last = pc->points.back(); pc->points.push_back({last.x + 8, last.y}); }
                }
                ImGui::SameLine();
                if (ImGui::Button("Box##poly")) {
                    auto* spr = selectedEntity_->getComponent<SpriteComponent>();
                    float w = spr ? spr->size.x : 32.0f;
                    float h = spr ? spr->size.y : 32.0f;
                    *pc = PolygonCollider::makeBox(w, h);
                }
                ImGui::SameLine();
                if (ImGui::Button("Circle##poly")) {
                    auto* spr = selectedEntity_->getComponent<SpriteComponent>();
                    float r = spr ? (spr->size.x + spr->size.y) * 0.25f : 16.0f;
                    *pc = PolygonCollider::makeCircleApprox(r, 8);
                }
            }
        }

        // PlayerController
        if (auto* p = selectedEntity_->getComponent<PlayerController>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Player Controller", ImGuiTreeNodeFlags_DefaultOpen);
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmPlayerCtrl")) {
                if (ImGui::MenuItem("Remove Component")) {
                    selectedEntity_->removeComponent<PlayerController>();
                    ImGui::EndPopup();
                    goto endInspectorComponents;
                }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<PlayerController>()) {
                if (ImGui::BeginTable("##PlayerCtrlProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Speed");
                    ImGui::DragFloat("##moveSpd", &p->moveSpeed, 1.0f, 0.0f, 500.0f);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Local");
                    ImGui::Checkbox("##isLocal", &p->isLocalPlayer);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Facing");
                    const char* dirs[] = {"None", "Up", "Down", "Left", "Right"};
                    ImGui::Text("%s | %s", dirs[(int)p->facing], p->isMoving ? "Moving" : "Idle");

                    ImGui::EndTable();
                }
            }
        }

        // Animator
        if (auto* a = selectedEntity_->getComponent<Animator>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Animator");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmAnimator")) {
                if (ImGui::MenuItem("Remove Component")) {
                    selectedEntity_->removeComponent<Animator>();
                    ImGui::EndPopup();
                    goto endInspectorComponents;
                }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<Animator>()) {
                if (ImGui::BeginTable("##AnimatorProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Animation");
                    ImGui::Text("%s", a->currentAnimation.c_str());

                    INSPECTOR_ROW("State");
                    ImGui::Text("%s | %.2fs", a->playing ? "Playing" : "Stopped", a->timer);

                    if (!a->animations.empty()) {
                        INSPECTOR_ROW("States");
                        std::string stateList;
                        for (auto& [name, def] : a->animations) {
                            std::string base = name;
                            for (auto* suffix : {"_down", "_up", "_left", "_right"}) {
                                size_t suffLen = strlen(suffix);
                                if (base.size() > suffLen &&
                                    base.substr(base.size() - suffLen) == suffix) {
                                    base = base.substr(0, base.size() - suffLen);
                                    break;
                                }
                            }
                            if (stateList.find(base) == std::string::npos) {
                                if (!stateList.empty()) stateList += ", ";
                                stateList += base;
                            }
                        }
                        ImGui::TextWrapped("%s", stateList.c_str());
                    }

                    ImGui::EndTable();
                }
                if (ImGui::Button("Open in Animation Editor##anim")) {
                    auto* sprite = selectedEntity_->getComponent<SpriteComponent>();
                    if (sprite && !sprite->texturePath.empty()) {
                        animationEditor_.openWithSheet(sprite->texturePath);
                    } else {
                        animationEditor_.setOpen(true);
                    }
                }
            }
        }

        // ZoneComponent
        if (auto* z = selectedEntity_->getComponent<ZoneComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Zone", ImGuiTreeNodeFlags_DefaultOpen);
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmZone")) {
                if (ImGui::MenuItem("Remove Component")) {
                    selectedEntity_->removeComponent<ZoneComponent>();
                    ImGui::EndPopup();
                    goto endInspectorComponents;
                }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<ZoneComponent>()) {
                if (ImGui::BeginTable("##ZoneProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Zone Name");
                    char znameBuf[64];
                    strncpy(znameBuf, z->zoneName.c_str(), sizeof(znameBuf) - 1);
                    znameBuf[sizeof(znameBuf) - 1] = '\0';
                    if (ImGui::InputText("##zoneName", znameBuf, sizeof(znameBuf))) z->zoneName = znameBuf;
                    captureInspectorUndo();

                    INSPECTOR_ROW("Display");
                    char dispBuf[64];
                    strncpy(dispBuf, z->displayName.c_str(), sizeof(dispBuf) - 1);
                    dispBuf[sizeof(dispBuf) - 1] = '\0';
                    if (ImGui::InputText("##zoneDisp", dispBuf, sizeof(dispBuf))) z->displayName = dispBuf;
                    captureInspectorUndo();

                    INSPECTOR_ROW("Size");
                    ImGui::DragFloat2("##zoneSize", &z->size.x, 8.0f, 32.0f, 10000.0f);
                    captureInspectorUndo();

                    ImGui::EndTable();
                }

                {
                    const char* types[] = {"town", "zone", "dungeon"};
                    int typeIdx = 0;
                    if (z->zoneType == "zone") typeIdx = 1;
                    if (z->zoneType == "dungeon") typeIdx = 2;
                    auto beforeZoneType = PrefabLibrary::entityToJson(selectedEntity_);
                    if (ImGui::Combo("##zoneType", &typeIdx, types, 3)) {
                        z->zoneType = types[typeIdx];
                        auto afterZoneType = PrefabLibrary::entityToJson(selectedEntity_);
                        if (afterZoneType != beforeZoneType) {
                            auto cmd = std::make_unique<PropertyCommand>();
                            cmd->entityHandle = selectedEntity_->handle();
                            cmd->oldState = std::move(beforeZoneType);
                            cmd->newState = std::move(afterZoneType);
                            cmd->desc = "Inspector combo change";
                            UndoSystem::instance().push(std::move(cmd));
                        }
                    }
                }

                if (ImGui::BeginTable("##ZoneProps2", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Min Level");
                    ImGui::DragInt("##zMinLvl", &z->minLevel, 1, 1, 99);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Max Level");
                    ImGui::DragInt("##zMaxLvl", &z->maxLevel, 1, 1, 99);
                    captureInspectorUndo();

                    INSPECTOR_ROW("PvP");
                    ImGui::Checkbox("##zPvp", &z->pvpEnabled);
                    captureInspectorUndo();

                    ImGui::EndTable();
                }
            }
        }

        // PortalComponent
        if (auto* p = selectedEntity_->getComponent<PortalComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Portal", ImGuiTreeNodeFlags_DefaultOpen);
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmPortal")) {
                if (ImGui::MenuItem("Remove Component")) {
                    selectedEntity_->removeComponent<PortalComponent>();
                    ImGui::EndPopup();
                    goto endInspectorComponents;
                }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<PortalComponent>()) {
                if (ImGui::BeginTable("##PortalProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Trigger");
                    ImGui::DragFloat2("##pTrigSz", &p->triggerSize.x, 1.0f, 8.0f, 256.0f);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Scene");
                    char sceneBuf[64];
                    strncpy(sceneBuf, p->targetScene.c_str(), sizeof(sceneBuf) - 1);
                    sceneBuf[sizeof(sceneBuf) - 1] = '\0';
                    if (ImGui::InputText("##pScene", sceneBuf, sizeof(sceneBuf))) p->targetScene = sceneBuf;
                    captureInspectorUndo();

                    INSPECTOR_ROW("Zone");
                    char zoneBuf[64];
                    strncpy(zoneBuf, p->targetZone.c_str(), sizeof(zoneBuf) - 1);
                    zoneBuf[sizeof(zoneBuf) - 1] = '\0';
                    if (ImGui::InputText("##pZone", zoneBuf, sizeof(zoneBuf))) p->targetZone = zoneBuf;
                    captureInspectorUndo();

                    INSPECTOR_ROW("Spawn Pos");
                    ImGui::DragFloat2("##pSpawn", &p->targetSpawnPos.x, 1.0f);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Fade");
                    ImGui::Checkbox("##pFade", &p->useFadeTransition);
                    captureInspectorUndo();

                    if (p->useFadeTransition) {
                        INSPECTOR_ROW("Duration");
                        ImGui::DragFloat("##pFadeDur", &p->fadeDuration, 0.05f, 0.1f, 2.0f);
                        captureInspectorUndo();
                    }

                    INSPECTOR_ROW("Label");
                    ImGui::Checkbox("##pShowLbl", &p->showLabel);
                    captureInspectorUndo();

                    INSPECTOR_ROW("Override");
                    char labelBuf[64];
                    strncpy(labelBuf, p->label.c_str(), sizeof(labelBuf) - 1);
                    labelBuf[sizeof(labelBuf) - 1] = '\0';
                    if (ImGui::InputText("##pLabel", labelBuf, sizeof(labelBuf))) p->label = labelBuf;
                    captureInspectorUndo();

                    ImGui::EndTable();
                }
            }
        }

        // ---- Game Components (editable in inspector) ----

        // Character Stats
        if (auto* cs = selectedEntity_->getComponent<CharacterStatsComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Character Stats");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmCharStats")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<CharacterStatsComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<CharacterStatsComponent>()) {
                auto& s = cs->stats;
                char nameBuf[64]; strncpy(nameBuf, s.characterName.c_str(), sizeof(nameBuf)-1); nameBuf[sizeof(nameBuf)-1]=0;
                if (ImGui::InputText("Char Name##cs", nameBuf, sizeof(nameBuf))) s.characterName = nameBuf;
                captureInspectorUndo();

                // Class selector - reconfigures ClassDefinition on change
                {
                    const char* classNames[] = {"Warrior", "Mage", "Archer"};
                    int classIdx = (int)s.classDef.classType;
                    auto beforeClassCombo = PrefabLibrary::entityToJson(selectedEntity_);
                    if (ImGui::Combo("Class##cs", &classIdx, classNames, 3)) {
                        auto& cd = s.classDef;
                        cd.classType = (ClassType)classIdx;
                        switch (cd.classType) {
                            case ClassType::Warrior:
                                cd.displayName = "Warrior"; s.className = "Warrior";
                                cd.baseMaxHP = 70; cd.baseMaxMP = 30;
                                cd.baseStrength = 14; cd.baseVitality = 12;
                                cd.baseIntelligence = 5; cd.baseDexterity = 8; cd.baseWisdom = 5;
                                cd.baseHitRate = 4.0f; cd.attackRange = 1.0f;
                                cd.primaryResource = ResourceType::Fury;
                                cd.hpPerLevel = 7.0f; cd.mpPerLevel = 2.0f;
                                cd.strPerLevel = 0.25f; cd.vitPerLevel = 0.25f;
                                cd.intPerLevel = 0.0f; cd.dexPerLevel = 0.0f; cd.wisPerLevel = 0.0f;
                                break;
                            case ClassType::Mage:
                                cd.displayName = "Mage"; s.className = "Mage";
                                cd.baseMaxHP = 50; cd.baseMaxMP = 150;
                                cd.baseStrength = 4; cd.baseVitality = 6;
                                cd.baseIntelligence = 16; cd.baseDexterity = 6; cd.baseWisdom = 14;
                                cd.baseHitRate = 0.0f; cd.attackRange = 7.0f;
                                cd.primaryResource = ResourceType::Mana;
                                cd.hpPerLevel = 5.0f; cd.mpPerLevel = 10.0f;
                                cd.strPerLevel = 0.0f; cd.vitPerLevel = 0.25f;
                                cd.intPerLevel = 0.25f; cd.dexPerLevel = 0.0f; cd.wisPerLevel = 0.25f;
                                break;
                            case ClassType::Archer:
                                cd.displayName = "Archer"; s.className = "Archer";
                                cd.baseMaxHP = 50; cd.baseMaxMP = 40;
                                cd.baseStrength = 8; cd.baseVitality = 9;
                                cd.baseIntelligence = 7; cd.baseDexterity = 18; cd.baseWisdom = 8;
                                cd.baseHitRate = 4.0f; cd.attackRange = 7.0f;
                                cd.primaryResource = ResourceType::Fury;
                                cd.hpPerLevel = 5.0f; cd.mpPerLevel = 2.0f;
                                cd.strPerLevel = 0.0f; cd.vitPerLevel = 0.25f;
                                cd.intPerLevel = 0.0f; cd.dexPerLevel = 0.25f; cd.wisPerLevel = 0.5f;
                                break;
                        }
                        s.recalculateStats();
                        s.recalculateXPRequirement();
                        s.currentHP = s.maxHP;
                        s.currentMP = s.maxMP;
                        s.currentFury = 0.0f;
                        auto afterClassCombo = PrefabLibrary::entityToJson(selectedEntity_);
                        if (afterClassCombo != beforeClassCombo) {
                            auto cmd = std::make_unique<PropertyCommand>();
                            cmd->entityHandle = selectedEntity_->handle();
                            cmd->oldState = std::move(beforeClassCombo);
                            cmd->newState = std::move(afterClassCombo);
                            cmd->desc = "Inspector combo change";
                            UndoSystem::instance().push(std::move(cmd));
                        }
                    }
                }

                ImGui::DragInt("Level##cs", &s.level, 0.1f, 1, 70);
                if (ImGui::IsItemDeactivatedAfterEdit()) { s.recalculateStats(); s.recalculateXPRequirement(); s.currentHP = s.maxHP; s.currentMP = s.maxMP; }
                captureInspectorUndo();
                ImGui::DragInt("Current HP##cs", &s.currentHP, 1.0f, 0, 999999);
                captureInspectorUndo();
                ImGui::DragInt("Max HP##cs", &s.maxHP, 1.0f, 1, 999999);
                captureInspectorUndo();
                ImGui::DragInt("Current MP##cs", &s.currentMP, 1.0f, 0, 999999);
                captureInspectorUndo();
                ImGui::DragInt("Max MP##cs", &s.maxMP, 1.0f, 1, 999999);
                captureInspectorUndo();
                if (s.classDef.usesFury()) {
                    ImGui::DragFloat("Fury##cs", &s.currentFury, 0.1f, 0.0f, 20.0f);
                    captureInspectorUndo();
                    ImGui::DragInt("Max Fury##cs", &s.maxFury, 0.1f, 1, 20);
                    captureInspectorUndo();
                }
                ImGui::DragInt("Honor##cs", &s.honor, 1.0f, 0, 1000000);
                captureInspectorUndo();
                ImGui::Checkbox("Is Dead##cs", &s.isDead);
                captureInspectorUndo();
                {
                    const char* pkNames[] = {"White","Purple","Red","Black"};
                    int pkIdx = (int)s.pkStatus;
                    auto beforePkCombo = PrefabLibrary::entityToJson(selectedEntity_);
                    if (ImGui::Combo("PK Status##cs", &pkIdx, pkNames, 4)) {
                        s.pkStatus = (PKStatus)pkIdx;
                        auto afterPkCombo = PrefabLibrary::entityToJson(selectedEntity_);
                        if (afterPkCombo != beforePkCombo) {
                            auto cmd = std::make_unique<PropertyCommand>();
                            cmd->entityHandle = selectedEntity_->handle();
                            cmd->oldState = std::move(beforePkCombo);
                            cmd->newState = std::move(afterPkCombo);
                            cmd->desc = "Inspector combo change";
                            UndoSystem::instance().push(std::move(cmd));
                        }
                    }
                }
                ImGui::Separator();
                ImGui::Text("Class: %s  Resource: %s", s.classDef.displayName.c_str(), s.classDef.usesFury() ? "Fury" : "Mana");
                ImGui::Text("Base: HP:%d MP:%d STR:%d VIT:%d INT:%d DEX:%d WIS:%d",
                    s.classDef.baseMaxHP, s.classDef.baseMaxMP, s.classDef.baseStrength,
                    s.classDef.baseVitality, s.classDef.baseIntelligence, s.classDef.baseDexterity, s.classDef.baseWisdom);
                ImGui::Text("Computed: STR:%d VIT:%d INT:%d DEX:%d WIS:%d", s.getStrength(), s.getVitality(), s.getIntelligence(), s.getDexterity(), s.getWisdom());
                ImGui::Text("Armor:%d MR:%d HR:%.1f Crit:%.0f%% Spd:%.2f", s.getArmor(), s.getMagicResist(), s.getHitRate(), s.getCritRate()*100, s.getSpeed());
                ImGui::Text("Atk Range: %.0f tiles  DmgMult: %.2f", s.classDef.attackRange, s.getDamageMultiplier());
                if (ImGui::Button("Recalc Stats##cs")) { s.recalculateStats(); s.recalculateXPRequirement(); }
                ImGui::SameLine();
                if (ImGui::Button("Full Heal##cs")) { s.currentHP = s.maxHP; s.currentMP = s.maxMP; s.isDead = false; s.lifeState = LifeState::Alive; }
            }
        }

        // Enemy Stats
        if (auto* es = selectedEntity_->getComponent<EnemyStatsComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Enemy Stats");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmEnemyStats")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<EnemyStatsComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<EnemyStatsComponent>()) {
                auto& s = es->stats;
                char eName[64]; strncpy(eName, s.enemyName.c_str(), sizeof(eName)-1); eName[sizeof(eName)-1]=0;
                if (ImGui::InputText("Name##es", eName, sizeof(eName))) s.enemyName = eName;
                captureInspectorUndo();
                char eType[32]; strncpy(eType, s.monsterType.c_str(), sizeof(eType)-1); eType[sizeof(eType)-1]=0;
                if (ImGui::InputText("Type##es", eType, sizeof(eType))) s.monsterType = eType;
                captureInspectorUndo();
                ImGui::DragInt("Level##es", &s.level, 0.1f, 1, 70);
                captureInspectorUndo();
                ImGui::DragInt("HP##es", &s.currentHP, 1.0f, 0, 999999);
                captureInspectorUndo();
                ImGui::DragInt("Max HP##es", &s.maxHP, 1.0f, 1, 999999);
                captureInspectorUndo();
                ImGui::DragInt("Base Damage##es", &s.baseDamage, 1.0f, 0, 99999);
                captureInspectorUndo();
                ImGui::DragInt("Armor##es", &s.armor, 1.0f, 0, 9999);
                captureInspectorUndo();
                ImGui::DragInt("Magic Resist##es", &s.magicResist, 1.0f, 0, 9999);
                captureInspectorUndo();
                ImGui::DragInt("Hit Rate##es", &s.mobHitRate, 0.5f, 0, 100);
                captureInspectorUndo();
                ImGui::DragFloat("Crit Rate##es", &s.critRate, 0.01f, 0.0f, 1.0f, "%.2f");
                captureInspectorUndo();
                ImGui::DragFloat("Attack Speed##es", &s.attackSpeed, 0.1f, 0.1f, 10.0f);
                captureInspectorUndo();
                ImGui::DragFloat("Move Speed##es", &s.moveSpeed, 0.1f, 0.1f, 10.0f);
                captureInspectorUndo();
                ImGui::DragInt("XP Reward##es", &s.xpReward, 1.0f, 0, 99999);
                captureInspectorUndo();
                ImGui::DragInt("Honor##es", &s.honorReward, 1.0f, 0, 1000);
                captureInspectorUndo();
                ImGui::Checkbox("Aggressive##es", &s.isAggressive);
                captureInspectorUndo();
                ImGui::SameLine();
                ImGui::Checkbox("Magic Dmg##es", &s.dealsMagicDamage);
                captureInspectorUndo();
                ImGui::Checkbox("Alive##es", &s.isAlive);
                captureInspectorUndo();
                ImGui::Text("Threat entries: %zu", s.damageByAttacker.size());
                if (!s.damageByAttacker.empty() && ImGui::Button("Clear Threat##es")) s.clearThreatTable();
            }
        }

        // Mob AI
        if (auto* ai = selectedEntity_->getComponent<MobAIComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Mob AI");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmMobAI")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<MobAIComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<MobAIComponent>()) {
                auto& a = ai->ai;
                const char* mN[] = {"Idle","Roam","Chase","ChaseMem","Attack","ReturnHome"};
                const char* dN[] = {"None","Up","Down","Left","Right"};
                ImGui::Text("Mode: %s  Facing: %s", mN[(int)a.getMode()], dN[(int)a.getFacingDirection()]);
                ImGui::DragFloat("Aggro Radius##ai", &a.acquireRadius, 1.0f, 0.0f, 1000.0f);
                captureInspectorUndo();
                ImGui::DragFloat("Leash Radius##ai", &a.contactRadius, 1.0f, 0.0f, 1000.0f);
                captureInspectorUndo();
                ImGui::DragFloat("Attack Range##ai", &a.attackRange, 1.0f, 0.0f, 500.0f);
                captureInspectorUndo();
                ImGui::DragFloat("Roam Radius##ai", &a.roamRadius, 1.0f, 0.0f, 500.0f);
                captureInspectorUndo();
                ImGui::DragFloat("Chase Speed##ai", &a.baseChaseSpeed, 1.0f, 0.0f, 500.0f);
                captureInspectorUndo();
                ImGui::DragFloat("Return Speed##ai", &a.baseReturnSpeed, 1.0f, 0.0f, 500.0f);
                captureInspectorUndo();
                ImGui::DragFloat("Roam Speed##ai", &a.baseRoamSpeed, 1.0f, 0.0f, 500.0f);
                captureInspectorUndo();
                ImGui::DragFloat("Attack CD##ai", &a.attackCooldown, 0.05f, 0.1f, 10.0f, "%.2fs");
                captureInspectorUndo();
                ImGui::DragFloat("Think Interval##ai", &a.serverTickInterval, 0.01f, 0.05f, 1.0f, "%.2fs");
                captureInspectorUndo();
                ImGui::Checkbox("Passive##ai", &a.isPassive);
                captureInspectorUndo();
                ImGui::Checkbox("Can Roam##ai", &a.canRoam);
                captureInspectorUndo();
                ImGui::SameLine();
                ImGui::Checkbox("Can Chase##ai", &a.canChase);
                captureInspectorUndo();
                ImGui::Checkbox("Roam While Idle##ai", &a.roamWhileIdle);
                captureInspectorUndo();
                ImGui::Checkbox("Show Aggro Radius##ai", &a.showAggroRadius);
                captureInspectorUndo();
            }
        }

        // Combat Controller
        if (auto* cc = selectedEntity_->getComponent<CombatControllerComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Combat Controller");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmCombat")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<CombatControllerComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<CombatControllerComponent>()) {
                ImGui::DragFloat("Base Cooldown##cc", &cc->baseAttackCooldown, 0.05f, 0.1f, 5.0f, "%.2fs");
                captureInspectorUndo();
                ImGui::Text("CD Remaining: %.2f", cc->attackCooldownRemaining);
                ImGui::DragFloat("Disengage Range##cc", &cc->disengageRange, 0.1f, 0.5f, 20.0f, "%.1f tiles");
                captureInspectorUndo();
                ImGui::Checkbox("Show Disengage Range##cc", &cc->showDisengageRange);
                captureInspectorUndo();

                // --- Combat Text Config (game-wide) ---
                ImGui::Separator();
                auto& ctc = fate::CombatTextConfig::instance();
                if (ImGui::TreeNode("Combat Text Styles")) {
                    inspectCombatTextStyle("Damage", ctc.damage);
                    inspectCombatTextStyle("Critical", ctc.crit);
                    inspectCombatTextStyle("Miss", ctc.miss);
                    inspectCombatTextStyle("Resist", ctc.resist);
                    inspectCombatTextStyle("Heal", ctc.heal);
                    inspectCombatTextStyle("Block", ctc.block);
                    inspectCombatTextStyle("XP Gain", ctc.xp);
                    inspectCombatTextStyle("Level Up", ctc.levelUp);

                    ImGui::Separator();
                    if (ImGui::Button("Save to JSON##ctc")) {
                        ctc.save(fate::CombatTextConfig::kDefaultPath);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reset Defaults##ctc")) {
                        ctc.loadDefaults();
                    }
                    ImGui::TreePop();
                }
            }
        }

        // Inventory
        if (auto* inv = selectedEntity_->getComponent<InventoryComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Inventory");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmInv")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<InventoryComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<InventoryComponent>()) {
                int64_t gold = inv->inventory.getGold();
                ImGui::Text("Gold: %lld  Slots: %d / %d", (long long)gold, inv->inventory.usedSlots(), inv->inventory.totalSlots());
            }
        }

        // Skill Manager
        if (auto* sk = selectedEntity_->getComponent<SkillManagerComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Skill Manager");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmSkill")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<SkillManagerComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<SkillManagerComponent>()) {
                ImGui::Text("Points: %d avail, %d earned, %d spent", sk->skills.availablePoints(), sk->skills.earnedPoints(), sk->skills.spentPoints());
            }
        }

        // Status Effects
        if (auto* se = selectedEntity_->getComponent<StatusEffectComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Status Effects");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmSE")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<StatusEffectComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<StatusEffectComponent>()) {
                ImGui::Text("Invuln:%s StunImm:%s Shield:%.0f", se->effects.isInvulnerable()?"Y":"N", se->effects.isStunImmune()?"Y":"N", se->effects.currentShield());
                ImGui::Text("DmgMult:%.2f Reduc:%.2f Spd:%.2f", se->effects.getDamageMultiplier(), se->effects.getDamageReduction(), se->effects.getSpeedModifier());
                if (ImGui::Button("Clear All Effects##se")) se->effects.removeAllEffects();
            }
        }

        // Crowd Control
        if (auto* ccC = selectedEntity_->getComponent<CrowdControlComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Crowd Control");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmCC")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<CrowdControlComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<CrowdControlComponent>()) {
                const char* ccN[] = {"None","Stunned","Frozen","Rooted","Taunted"};
                ImGui::Text("CC: %s  Time: %.1f", ccN[(int)ccC->cc.getCurrentCC()], ccC->cc.getRemainingTime());
                ImGui::Text("CanMove: %s  CanAct: %s", ccC->cc.canMove()?"Y":"N", ccC->cc.canAct()?"Y":"N");
                if (ccC->cc.getCurrentCC() != CCType::None && ImGui::Button("Clear CC##cc")) ccC->cc.endCC();
            }
        }

        // Nameplate
        if (auto* np = selectedEntity_->getComponent<NameplateComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Nameplate");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmNP")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<NameplateComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<NameplateComponent>()) {
                char npName[64]; strncpy(npName, np->displayName.c_str(), sizeof(npName)-1); npName[sizeof(npName)-1]=0;
                if (ImGui::InputText("Name##np", npName, sizeof(npName))) np->displayName = npName;
                captureInspectorUndo();
                ImGui::DragInt("Level##np", &np->displayLevel, 0.1f, 1, 70);
                captureInspectorUndo();
                ImGui::Checkbox("Show Level##np", &np->showLevel);
                captureInspectorUndo();
                ImGui::DragFloat("Font Size##np", &np->fontSize, 0.02f, 0.3f, 2.0f, "%.2f");
                captureInspectorUndo();
                ImGui::DragFloat("Y Offset##np", &np->yOffset, 0.5f, -50.0f, 100.0f, "%.1f");
                captureInspectorUndo();
                // --- Text Style ---
                ImGui::Separator();
                ImGui::Text("Text Style");
                if (inspectTextStyle("npText", np->fontName, np->textStyle, np->textEffects))
                    captureInspectorUndo();
                ImGui::Checkbox("Visible##np", &np->visible);
                captureInspectorUndo();

                // --- Guild Tag ---
                ImGui::Separator();
                ImGui::Checkbox("Show Guild##np", &np->showGuild);
                captureInspectorUndo();
                if (np->showGuild) {
                    char gName[64]; strncpy(gName, np->guildName.c_str(), sizeof(gName)-1); gName[sizeof(gName)-1]=0;
                    if (ImGui::InputText("Guild Name##np", gName, sizeof(gName))) np->guildName = gName;
                    captureInspectorUndo();
                    ImGui::ColorEdit4("Guild Color##np", &np->guildColor.r);
                    captureInspectorUndo();
                    ImGui::DragFloat("Guild Font Size##np", &np->guildFontSize, 0.02f, 0.3f, 2.0f, "%.2f");
                    captureInspectorUndo();
                    ImGui::DragFloat("Guild Y Offset##np", &np->guildYOffset, 0.5f, -50.0f, 100.0f, "%.1f");
                    captureInspectorUndo();
                    // Guild text style
                    ImGui::Separator();
                    ImGui::Text("Guild Text Style");
                    if (inspectTextStyle("npGuild", np->guildFontName, np->guildTextStyle, np->guildTextEffects))
                        captureInspectorUndo();
                    char gIcon[128]; strncpy(gIcon, np->guildIconPath.c_str(), sizeof(gIcon)-1); gIcon[sizeof(gIcon)-1]=0;
                    if (ImGui::InputText("Guild Icon##np", gIcon, sizeof(gIcon))) {
                        np->guildIconPath = gIcon;
                        np->guildIconTex = nullptr; // force re-resolve
                    }
                    captureInspectorUndo();
                }
            }
        }

        // Mob Nameplate
        if (auto* mnp = selectedEntity_->getComponent<MobNameplateComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Mob Nameplate");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmMNP")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<MobNameplateComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<MobNameplateComponent>()) {
                char mnName[64]; strncpy(mnName, mnp->displayName.c_str(), sizeof(mnName)-1); mnName[sizeof(mnName)-1]=0;
                if (ImGui::InputText("Name##mnp", mnName, sizeof(mnName))) mnp->displayName = mnName;
                captureInspectorUndo();
                ImGui::DragInt("Level##mnp", &mnp->level, 0.1f, 1, 70);
                captureInspectorUndo();
                ImGui::Checkbox("Boss##mnp", &mnp->isBoss);
                captureInspectorUndo();
                ImGui::SameLine();
                ImGui::Checkbox("Elite##mnp", &mnp->isElite);
                captureInspectorUndo();
                ImGui::Checkbox("Show Level##mnp", &mnp->showLevel);
                captureInspectorUndo();
                ImGui::DragFloat("Font Size##mnp", &mnp->fontSize, 0.02f, 0.3f, 2.0f, "%.2f");
                captureInspectorUndo();
                ImGui::DragFloat("Y Offset##mnp", &mnp->yOffset, 0.5f, -50.0f, 100.0f, "%.1f");
                captureInspectorUndo();
                // --- Text Style ---
                ImGui::Separator();
                ImGui::Text("Text Style");
                if (inspectTextStyle("mnpText", mnp->fontName, mnp->textStyle, mnp->textEffects))
                    captureInspectorUndo();
                ImGui::Checkbox("Visible##mnp", &mnp->visible);
                captureInspectorUndo();

                // --- HP Bar ---
                ImGui::Separator();
                ImGui::Checkbox("Show HP Bar##mnp", &mnp->showHpBar);
                captureInspectorUndo();
                if (mnp->showHpBar) {
                    ImGui::DragFloat("HP Bar Width##mnp", &mnp->hpBarWidth, 1.0f, 8.0f, 128.0f, "%.0f");
                    captureInspectorUndo();
                    ImGui::DragFloat("HP Bar Height##mnp", &mnp->hpBarHeight, 0.5f, 1.0f, 32.0f, "%.1f");
                    captureInspectorUndo();
                    ImGui::DragFloat("HP Bar Y Offset##mnp", &mnp->hpBarYOffset, 0.5f, -50.0f, 100.0f, "%.1f");
                    captureInspectorUndo();
                    ImGui::ColorEdit4("HP Border##mnp", &mnp->hpBarBorderColor.r);
                    captureInspectorUndo();
                    ImGui::ColorEdit4("HP Background##mnp", &mnp->hpBarBgColor.r);
                    captureInspectorUndo();
                    ImGui::ColorEdit4("HP High##mnp", &mnp->hpBarHighColor.r);
                    captureInspectorUndo();
                    ImGui::ColorEdit4("HP Low##mnp", &mnp->hpBarLowColor.r);
                    captureInspectorUndo();
                }
            }
        }

        // Targeting
        if (auto* tgt = selectedEntity_->getComponent<TargetingComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Targeting");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmTgt")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<TargetingComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<TargetingComponent>()) {
                // Runtime state
                ImGui::Text("Target: %u", tgt->selectedTargetId);
                if (tgt->hasTarget() && ImGui::Button("Clear Target##tgt")) tgt->clearTarget();
                ImGui::Separator();

                // Behavior
                ImGui::DragFloat("Max Range##tgt", &tgt->maxTargetRange, 0.5f, 1.0f, 50.0f);
                captureInspectorUndo();
                ImGui::Checkbox("Can Target Self##tgt", &tgt->canTargetSelf);
                captureInspectorUndo();

                // Global defaults
                if (ImGui::TreeNode("Global Defaults##tgt")) {
                    ImGui::DragFloat("Radius Scale##tgtg", &tgt->radiusScale, 0.01f, 0.1f, 1.0f);
                    captureInspectorUndo();
                    ImGui::DragFloat("Y Scale (Slant)##tgtg", &tgt->yScale, 0.01f, 0.1f, 1.0f);
                    captureInspectorUndo();
                    ImGui::DragFloat("Ring Thickness##tgtg", &tgt->ringThickness, 0.1f, 0.5f, 5.0f);
                    captureInspectorUndo();
                    ImGui::DragFloat("Depth Offset##tgtg", &tgt->depthOffset, 0.05f, 0.0f, 5.0f);
                    captureInspectorUndo();
                    int segs = tgt->segments;
                    if (ImGui::DragInt("Segments##tgtg", &segs, 1, 6, 64)) tgt->segments = segs;
                    captureInspectorUndo();
                    ImGui::DragFloat("Pulse Speed##tgtg", &tgt->pulseSpeed, 0.1f, 0.0f, 20.0f);
                    captureInspectorUndo();
                    ImGui::DragFloat("Pulse Min##tgtg", &tgt->pulseMin, 0.01f, 0.0f, 1.0f);
                    captureInspectorUndo();
                    ImGui::DragFloat("Pulse Max##tgtg", &tgt->pulseMax, 0.01f, 0.0f, 1.0f);
                    captureInspectorUndo();
                    ImGui::ColorEdit4("Ring Color##tgtg", &tgt->ringColor.r);
                    captureInspectorUndo();
                    ImGui::ColorEdit4("Fill Color##tgtg", &tgt->fillColor.r);
                    captureInspectorUndo();
                    ImGui::ColorEdit4("Glow Color##tgtg", &tgt->glowColor.r);
                    captureInspectorUndo();
                    ImGui::DragFloat("Inner Ring Scale##tgtg", &tgt->innerRingScale, 0.01f, 0.0f, 0.99f);
                    captureInspectorUndo();
                    ImGui::DragFloat("Glow Scale##tgtg", &tgt->glowScale, 0.01f, 0.0f, 3.0f);
                    captureInspectorUndo();
                    ImGui::TreePop();
                }

                // Per-target-type configs
                if (ImGui::TreeNode("Per-Type Overrides##tgt")) {
                    ImGui::TextDisabled("(0 = use global default)");
                    for (int i = 0; i < static_cast<int>(TargetCategory::Count); ++i) {
                        auto& pt = tgt->perType[i];
                        const char* catName = targetCategoryName(static_cast<TargetCategory>(i));
                        char label[64];
                        snprintf(label, sizeof(label), "%s##tpt%d", catName, i);
                        if (ImGui::TreeNode(label)) {
                            snprintf(label, sizeof(label), "Offset##tpto%d", i);
                            float off[2] = {pt.offset.x, pt.offset.y};
                            if (ImGui::DragFloat2(label, off, 0.5f, -200.0f, 200.0f)) {
                                pt.offset = {off[0], off[1]};
                            }
                            captureInspectorUndo();
                            snprintf(label, sizeof(label), "Radius Scale##tptr%d", i);
                            ImGui::DragFloat(label, &pt.radiusScale, 0.01f, 0.0f, 2.0f);
                            captureInspectorUndo();
                            snprintf(label, sizeof(label), "Y Scale##tpty%d", i);
                            ImGui::DragFloat(label, &pt.yScale, 0.01f, 0.0f, 1.0f);
                            captureInspectorUndo();
                            snprintf(label, sizeof(label), "Ring Color##tptrc%d", i);
                            ImGui::ColorEdit4(label, &pt.ringColor.r);
                            captureInspectorUndo();
                            snprintf(label, sizeof(label), "Fill Color##tptfc%d", i);
                            ImGui::ColorEdit4(label, &pt.fillColor.r);
                            captureInspectorUndo();
                            snprintf(label, sizeof(label), "Glow Color##tptgc%d", i);
                            ImGui::ColorEdit4(label, &pt.glowColor.r);
                            captureInspectorUndo();
                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }
            }
        }

        // Marker components with remove support
        if (selectedEntity_->hasComponent<DamageableComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Damageable");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmDmg")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<DamageableComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open) ImGui::Text("(marker - entity can take damage)");
        }

        // Social components (removable)
        if (selectedEntity_->hasComponent<PartyComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            ImGui::CollapsingHeader("Party Manager");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmParty")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<PartyComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
        }
        if (selectedEntity_->hasComponent<GuildComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            ImGui::CollapsingHeader("Guild Manager");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmGuild")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<GuildComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
        }
        if (selectedEntity_->hasComponent<ChatComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            ImGui::CollapsingHeader("Chat Manager");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmChat")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<ChatComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
        }
        if (selectedEntity_->hasComponent<FriendsComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            ImGui::CollapsingHeader("Friends Manager");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmFriends")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<FriendsComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
        }
        if (selectedEntity_->hasComponent<TradeComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            ImGui::CollapsingHeader("Trade Manager");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmTrade")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<TradeComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
        }
        if (selectedEntity_->hasComponent<MarketComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            ImGui::CollapsingHeader("Market Manager");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmMarket")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<MarketComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
        }

        // Spawn Zone
        if (auto* szComp = selectedEntity_->getComponent<SpawnZoneComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Spawn Zone");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmSpawnZone")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<SpawnZoneComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<SpawnZoneComponent>()) {
                auto& cfg = szComp->config;

                char znBuf[64]; strncpy(znBuf, cfg.zoneName.c_str(), sizeof(znBuf)-1); znBuf[sizeof(znBuf)-1]=0;
                if (ImGui::InputText("Zone Name##sz", znBuf, sizeof(znBuf))) cfg.zoneName = znBuf;
                captureInspectorUndo();

                ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1), "(Move zone via Transform position)");
                ImGui::DragFloat2("Zone Size##sz", &cfg.size.x, 4.0f, 32.0f, 5000.0f);
                captureInspectorUndo();
                ImGui::DragFloat("Min Spawn Dist##sz", &cfg.minSpawnDistance, 0.5f, 0.0f, 500.0f);
                captureInspectorUndo();
                ImGui::DragFloat("Tick Interval##sz", &cfg.serverTickInterval, 0.01f, 0.05f, 5.0f, "%.2fs");
                captureInspectorUndo();
                ImGui::Checkbox("Show Bounds##sz", &szComp->showBounds);
                captureInspectorUndo();

                ImGui::Text("Tracked mobs: %zu  Rules: %zu", szComp->trackedMobs.size(), cfg.rules.size());
                ImGui::Separator();

                int removeIdx = -1;
                for (int ri = 0; ri < (int)cfg.rules.size(); ++ri) {
                    auto& rule = cfg.rules[ri];
                    ImGui::PushID(ri);

                    char ruleLabel[64]; std::snprintf(ruleLabel, sizeof(ruleLabel), "Rule %d: %s", ri, rule.enemyId.c_str());
                    if (ImGui::TreeNode(ruleLabel)) {
                        char eidBuf[64]; strncpy(eidBuf, rule.enemyId.c_str(), sizeof(eidBuf)-1); eidBuf[sizeof(eidBuf)-1]=0;
                        if (ImGui::InputText("Enemy ID##r", eidBuf, sizeof(eidBuf))) rule.enemyId = eidBuf;
                        captureInspectorUndo();

                        ImGui::DragInt("Target Count##r", &rule.targetCount, 0.1f, 0, 100);
                        captureInspectorUndo();
                        ImGui::DragInt("Min Level##r", &rule.minLevel, 0.1f, 1, 70);
                        captureInspectorUndo();
                        ImGui::DragInt("Max Level##r", &rule.maxLevel, 0.1f, 1, 70);
                        captureInspectorUndo();
                        ImGui::DragInt("Base HP##r", &rule.baseHP, 1.0f, 1, 999999);
                        captureInspectorUndo();
                        ImGui::DragInt("Base Damage##r", &rule.baseDamage, 1.0f, 0, 99999);
                        captureInspectorUndo();
                        ImGui::DragFloat("Respawn Time##r", &rule.respawnSeconds, 0.5f, 1.0f, 600.0f, "%.1fs");
                        captureInspectorUndo();
                        ImGui::Checkbox("Aggressive##r", &rule.isAggressive);
                        captureInspectorUndo();
                        ImGui::SameLine();
                        ImGui::Checkbox("Boss##r", &rule.isBoss);
                        captureInspectorUndo();

                        if (ImGui::Button("Remove Rule##r")) removeIdx = ri;

                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                }

                if (removeIdx >= 0 && removeIdx < (int)cfg.rules.size()) {
                    cfg.rules.erase(cfg.rules.begin() + removeIdx);
                }

                if (ImGui::Button("+ Add Rule##sz")) {
                    cfg.rules.push_back(MobSpawnRule{});
                }
            }
        }

        // Faction
        if (auto* fc = selectedEntity_->getComponent<FactionComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Faction");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmFaction")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<FactionComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<FactionComponent>()) {
                static const char* factionNames[] = { "None", "Xyros", "Fenor", "Zethos", "Solis" };
                int current = static_cast<int>(fc->faction);
                auto beforeFactionCombo = PrefabLibrary::entityToJson(selectedEntity_);
                if (ImGui::Combo("Faction##fc", &current, factionNames, 5)) {
                    fc->faction = static_cast<Faction>(current);
                    auto afterFactionCombo = PrefabLibrary::entityToJson(selectedEntity_);
                    if (afterFactionCombo != beforeFactionCombo) {
                        auto cmd = std::make_unique<PropertyCommand>();
                        cmd->entityHandle = selectedEntity_->handle();
                        cmd->oldState = std::move(beforeFactionCombo);
                        cmd->newState = std::move(afterFactionCombo);
                        cmd->desc = "Inspector combo change";
                        UndoSystem::instance().push(std::move(cmd));
                    }
                }
            }
        }

        // Pet
        if (auto* pc = selectedEntity_->getComponent<PetComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Pet");
            if (fontHeading_) ImGui::PopFont();
            if (ImGui::BeginPopupContextItem("##rmPet")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<PetComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<PetComponent>()) {
                ImGui::Text("Has Pet: %s", pc->hasPet() ? "Yes" : "No");
                if (pc->hasPet()) {
                    ImGui::Text("Name: %s", pc->equippedPet.petName.c_str());
                    ImGui::Text("Definition: %s", pc->equippedPet.petDefinitionId.c_str());
                    ImGui::Text("Level: %d  XP: %lld/%lld", pc->equippedPet.level, (long long)pc->equippedPet.currentXP, (long long)pc->equippedPet.xpToNextLevel);
                    ImGui::Checkbox("Auto-Loot##pet", &pc->equippedPet.autoLootEnabled);
                    captureInspectorUndo();
                }
                ImGui::DragFloat("Auto-Loot Radius##pet", &pc->autoLootRadius, 1.0f, 0.0f, 512.0f);
                captureInspectorUndo();
            }
        }

        // MarketplaceNPC
        if (auto* mktNpc = selectedEntity_->getComponent<MarketplaceNPCComponent>()) {
            bool open = ImGui::CollapsingHeader("Marketplace NPC");
            if (ImGui::BeginPopupContextItem("##rmMarketplaceNPC")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<MarketplaceNPCComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open) {
                ImGui::TextDisabled("Neutral marketplace NPC (Veylan)");
            }
        }

        // LeaderboardNPC
        if (auto* lbNpc = selectedEntity_->getComponent<LeaderboardNPCComponent>()) {
            bool open = ImGui::CollapsingHeader("Leaderboard NPC");
            if (ImGui::BeginPopupContextItem("##rmLeaderboardNPC")) {
                if (ImGui::MenuItem("Remove Component")) { selectedEntity_->removeComponent<LeaderboardNPCComponent>(); ImGui::EndPopup(); goto endInspectorComponents; }
                ImGui::EndPopup();
            }
            if (open && selectedEntity_->hasComponent<LeaderboardNPCComponent>()) {
                char loreBuf[512];
                strncpy(loreBuf, lbNpc->loreSnippet.c_str(), sizeof(loreBuf) - 1);
                loreBuf[sizeof(loreBuf) - 1] = '\0';
                if (ImGui::InputTextMultiline("Lore Snippet##lb", loreBuf, sizeof(loreBuf))) {
                    lbNpc->loreSnippet = loreBuf;
                }
            }
        }

        // AppearanceComponent
        if (auto* a = selectedEntity_->getComponent<AppearanceComponent>()) {
            if (fontHeading_) ImGui::PushFont(fontHeading_);
            bool open = ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen);
            if (fontHeading_) ImGui::PopFont();
            if (open) {
                auto& catalog = PaperDollCatalog::instance();
                if (ImGui::BeginTable("##AppearanceProps", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    INSPECTOR_ROW("Gender");
                    const char* genderNames[] = {"Male", "Female"};
                    int genderInt = a->gender;
                    if (ImGui::Combo("##gender", &genderInt, genderNames, 2)) {
                        a->gender = static_cast<uint8_t>(genderInt);
                        // Reset hairstyle if it exceeds available count for new gender
                        const char* g = a->gender == 0 ? "Male" : "Female";
                        if (a->hairstyle >= catalog.getHairstyleCount(g))
                            a->hairstyle = 0;
                        a->dirty = true;
                    }

                    INSPECTOR_ROW("Hairstyle");
                    if (catalog.isLoaded()) {
                        const char* genderStr = a->gender == 0 ? "Male" : "Female";
                        auto hairNames = catalog.getHairstyleNames(genderStr);
                        if (!hairNames.empty()) {
                            int hairInt = a->hairstyle;
                            if (hairInt >= static_cast<int>(hairNames.size())) hairInt = 0;
                            if (ImGui::BeginCombo("##hairstyle", hairNames[hairInt].c_str())) {
                                for (int i = 0; i < static_cast<int>(hairNames.size()); ++i) {
                                    bool selected = (i == hairInt);
                                    if (ImGui::Selectable(hairNames[i].c_str(), selected)) {
                                        a->hairstyle = static_cast<uint8_t>(i);
                                        a->dirty = true;
                                    }
                                }
                                ImGui::EndCombo();
                            }
                        } else {
                            ImGui::Text("(no hairstyles)");
                        }
                    } else {
                        int hairInt = a->hairstyle;
                        if (ImGui::SliderInt("##hairstyle", &hairInt, 0, 2)) {
                            a->hairstyle = static_cast<uint8_t>(hairInt);
                            a->dirty = true;
                        }
                    }

                    INSPECTOR_ROW("Armor");
                    ImGui::Text("%s", a->armorStyle.empty() ? "(none)" : a->armorStyle.c_str());

                    INSPECTOR_ROW("Hat");
                    ImGui::Text("%s", a->hatStyle.empty() ? "(none)" : a->hatStyle.c_str());

                    INSPECTOR_ROW("Weapon");
                    ImGui::Text("%s", a->weaponStyle.empty() ? "(none)" : a->weaponStyle.c_str());

                    ImGui::EndTable();
                }
            }
        }

        // Generic fallback: render any reflected components not handled above
        {
            static const std::unordered_set<CompId> manuallyInspected = {
                componentId<Transform>(),
                componentId<SpriteComponent>(),
                componentId<BoxCollider>(),
                componentId<PolygonCollider>(),
                componentId<PlayerController>(),
                componentId<Animator>(),
                componentId<ZoneComponent>(),
                componentId<PortalComponent>(),
                componentId<CharacterStatsComponent>(),
                componentId<EnemyStatsComponent>(),
                componentId<MobAIComponent>(),
                componentId<CombatControllerComponent>(),
                componentId<InventoryComponent>(),
                componentId<SkillManagerComponent>(),
                componentId<StatusEffectComponent>(),
                componentId<CrowdControlComponent>(),
                componentId<NameplateComponent>(),
                componentId<MobNameplateComponent>(),
                componentId<TargetingComponent>(),
                componentId<DamageableComponent>(),
                componentId<PartyComponent>(),
                componentId<GuildComponent>(),
                componentId<ChatComponent>(),
                componentId<FriendsComponent>(),
                componentId<TradeComponent>(),
                componentId<MarketComponent>(),
                componentId<SpawnZoneComponent>(),
                componentId<FactionComponent>(),
                componentId<PetComponent>(),
                componentId<AppearanceComponent>(),
            };

            selectedEntity_->forEachComponent([&](void* data, CompId id) {
                if (manuallyInspected.count(id)) return;
                auto* meta = ComponentMetaRegistry::instance().findById(id);
                if (!meta || meta->fields.empty()) return;

                if (fontHeading_) ImGui::PushFont(fontHeading_);
                bool reflOpen = ImGui::CollapsingHeader(meta->name);
                if (fontHeading_) ImGui::PopFont();
                if (reflOpen) {
                    drawReflectedComponent(*meta, data);
                }
            });
        }

        endInspectorComponents:;

        // Add Component
        ImGui::Spacing();
        ImGui::Spacing();
        {
            float availW = ImGui::GetContentRegionAvail().x;
            ImVec2 btnPos = ImGui::GetCursorScreenPos();
            ImVec2 btnSize(availW, ImGui::GetFrameHeight());
            ImGui::GetWindowDrawList()->AddRect(
                btnPos, ImVec2(btnPos.x + btnSize.x, btnPos.y + btnSize.y),
                IM_COL32(128, 128, 128, 80), 3.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.06f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.10f));
            if (ImGui::Button("+ Add Component", btnSize)) ImGui::OpenPopup("AddComponent");
            ImGui::PopStyleColor(3);
        }
        if (ImGui::BeginPopup("AddComponent")) {
            if (!selectedEntity_->hasComponent<Transform>() && ImGui::MenuItem("Transform"))
                selectedEntity_->addComponent<Transform>();
            if (!selectedEntity_->hasComponent<SpriteComponent>() && ImGui::MenuItem("Sprite"))
                selectedEntity_->addComponent<SpriteComponent>();
            if (!selectedEntity_->hasComponent<BoxCollider>() && ImGui::MenuItem("Box Collider"))
                selectedEntity_->addComponent<BoxCollider>();
            if (!selectedEntity_->hasComponent<PolygonCollider>() && ImGui::MenuItem("Polygon Collider")) {
                auto* pc = selectedEntity_->addComponent<PolygonCollider>();
                *pc = PolygonCollider::makeBox(32, 32);
            }
            if (!selectedEntity_->hasComponent<PlayerController>() && ImGui::MenuItem("Player Controller"))
                selectedEntity_->addComponent<PlayerController>();
            if (!selectedEntity_->hasComponent<Animator>() && ImGui::MenuItem("Animator"))
                selectedEntity_->addComponent<Animator>();

            ImGui::Separator();
            if (!selectedEntity_->hasComponent<ZoneComponent>() && ImGui::MenuItem("Zone")) {
                auto* z = selectedEntity_->addComponent<ZoneComponent>();
                z->zoneName = "NewZone";
                z->displayName = "New Zone";
            }
            if (!selectedEntity_->hasComponent<PortalComponent>() && ImGui::MenuItem("Portal")) {
                selectedEntity_->addComponent<PortalComponent>();
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f,0.8f,1.0f,1.0f), "-- Game Systems --");
            if (!selectedEntity_->hasComponent<CharacterStatsComponent>() && ImGui::MenuItem("Character Stats"))
                selectedEntity_->addComponent<CharacterStatsComponent>();
            if (!selectedEntity_->hasComponent<EnemyStatsComponent>() && ImGui::MenuItem("Enemy Stats"))
                selectedEntity_->addComponent<EnemyStatsComponent>();
            if (!selectedEntity_->hasComponent<MobAIComponent>() && ImGui::MenuItem("Mob AI")) {
                auto* a = selectedEntity_->addComponent<MobAIComponent>();
                auto* t = selectedEntity_->getComponent<Transform>();
                if (t) a->ai.initialize(t->position);
            }
            if (!selectedEntity_->hasComponent<CombatControllerComponent>() && ImGui::MenuItem("Combat Controller"))
                selectedEntity_->addComponent<CombatControllerComponent>();
            if (!selectedEntity_->hasComponent<DamageableComponent>() && ImGui::MenuItem("Damageable"))
                selectedEntity_->addComponent<DamageableComponent>();
            if (!selectedEntity_->hasComponent<InventoryComponent>() && ImGui::MenuItem("Inventory"))
                selectedEntity_->addComponent<InventoryComponent>();
            if (!selectedEntity_->hasComponent<SkillManagerComponent>() && ImGui::MenuItem("Skill Manager"))
                selectedEntity_->addComponent<SkillManagerComponent>();
            if (!selectedEntity_->hasComponent<StatusEffectComponent>() && ImGui::MenuItem("Status Effects"))
                selectedEntity_->addComponent<StatusEffectComponent>();
            if (!selectedEntity_->hasComponent<CrowdControlComponent>() && ImGui::MenuItem("Crowd Control"))
                selectedEntity_->addComponent<CrowdControlComponent>();
            if (!selectedEntity_->hasComponent<TargetingComponent>() && ImGui::MenuItem("Targeting"))
                selectedEntity_->addComponent<TargetingComponent>();
            if (!selectedEntity_->hasComponent<NameplateComponent>() && ImGui::MenuItem("Nameplate"))
                selectedEntity_->addComponent<NameplateComponent>();
            if (!selectedEntity_->hasComponent<MobNameplateComponent>() && ImGui::MenuItem("Mob Nameplate"))
                selectedEntity_->addComponent<MobNameplateComponent>();
            if (!selectedEntity_->hasComponent<SpawnZoneComponent>() && ImGui::MenuItem("Spawn Zone"))
                selectedEntity_->addComponent<SpawnZoneComponent>();

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f,0.8f,1.0f,1.0f), "-- Social --");
            if (!selectedEntity_->hasComponent<ChatComponent>() && ImGui::MenuItem("Chat Manager"))
                selectedEntity_->addComponent<ChatComponent>();
            if (!selectedEntity_->hasComponent<PartyComponent>() && ImGui::MenuItem("Party Manager"))
                selectedEntity_->addComponent<PartyComponent>();
            if (!selectedEntity_->hasComponent<GuildComponent>() && ImGui::MenuItem("Guild Manager"))
                selectedEntity_->addComponent<GuildComponent>();
            if (!selectedEntity_->hasComponent<FriendsComponent>() && ImGui::MenuItem("Friends Manager"))
                selectedEntity_->addComponent<FriendsComponent>();
            if (!selectedEntity_->hasComponent<TradeComponent>() && ImGui::MenuItem("Trade Manager"))
                selectedEntity_->addComponent<TradeComponent>();
            if (!selectedEntity_->hasComponent<MarketComponent>() && ImGui::MenuItem("Market Manager"))
                selectedEntity_->addComponent<MarketComponent>();

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "-- NPC --");
            if (!selectedEntity_->hasComponent<NPCComponent>() && ImGui::MenuItem("NPC"))
                selectedEntity_->addComponent<NPCComponent>();
            if (!selectedEntity_->hasComponent<QuestGiverComponent>() && ImGui::MenuItem("Quest Giver"))
                selectedEntity_->addComponent<QuestGiverComponent>();
            if (!selectedEntity_->hasComponent<QuestMarkerComponent>() && ImGui::MenuItem("Quest Marker"))
                selectedEntity_->addComponent<QuestMarkerComponent>();
            if (!selectedEntity_->hasComponent<ShopComponent>() && ImGui::MenuItem("Shop"))
                selectedEntity_->addComponent<ShopComponent>();
            if (!selectedEntity_->hasComponent<BankerComponent>() && ImGui::MenuItem("Banker"))
                selectedEntity_->addComponent<BankerComponent>();
            if (!selectedEntity_->hasComponent<GuildNPCComponent>() && ImGui::MenuItem("Guild NPC"))
                selectedEntity_->addComponent<GuildNPCComponent>();
            if (!selectedEntity_->hasComponent<TeleporterComponent>() && ImGui::MenuItem("Teleporter"))
                selectedEntity_->addComponent<TeleporterComponent>();
            if (!selectedEntity_->hasComponent<StoryNPCComponent>() && ImGui::MenuItem("Story NPC"))
                selectedEntity_->addComponent<StoryNPCComponent>();
            if (!selectedEntity_->hasComponent<DungeonNPCComponent>() && ImGui::MenuItem("Dungeon NPC"))
                selectedEntity_->addComponent<DungeonNPCComponent>();
            if (!selectedEntity_->hasComponent<ArenaNPCComponent>() && ImGui::MenuItem("Arena NPC"))
                selectedEntity_->addComponent<ArenaNPCComponent>();
            if (!selectedEntity_->hasComponent<BattlefieldNPCComponent>() && ImGui::MenuItem("Battlefield NPC"))
                selectedEntity_->addComponent<BattlefieldNPCComponent>();
            if (!selectedEntity_->hasComponent<MarketplaceNPCComponent>() && ImGui::MenuItem("Marketplace NPC"))
                selectedEntity_->addComponent<MarketplaceNPCComponent>();
            if (!selectedEntity_->hasComponent<LeaderboardNPCComponent>() && ImGui::MenuItem("Leaderboard NPC"))
                selectedEntity_->addComponent<LeaderboardNPCComponent>();

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "-- Player Quest/Bank --");
            if (!selectedEntity_->hasComponent<QuestComponent>() && ImGui::MenuItem("Quest Manager"))
                selectedEntity_->addComponent<QuestComponent>();
            if (!selectedEntity_->hasComponent<BankStorageComponent>() && ImGui::MenuItem("Bank Storage"))
                selectedEntity_->addComponent<BankStorageComponent>();
            if (!selectedEntity_->hasComponent<FactionComponent>() && ImGui::MenuItem("Faction"))
                selectedEntity_->addComponent<FactionComponent>();
            if (!selectedEntity_->hasComponent<PetComponent>() && ImGui::MenuItem("Pet"))
                selectedEntity_->addComponent<PetComponent>();

            ImGui::EndPopup();
        }
#endif // FATE_HAS_GAME
    }
    ImGui::End();

    #undef INSPECTOR_ROW
}

} // namespace fate
