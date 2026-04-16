#include "engine/editor/content_browser_panel.h"
#include "engine/net/net_client.h"
#include "engine/net/admin_messages.h"
#include "engine/net/packet.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <set>
#include <unordered_map>

namespace fate {

// ============================================================================
// Case-insensitive substring match helper
// ============================================================================
static bool containsCI(const std::string& haystack, const char* needle) {
    if (!needle || needle[0] == '\0') return true;
    std::string h = haystack;
    std::string n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::tolower);
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    return h.find(n) != std::string::npos;
}

// Helper: get string from json with fallback
static std::string jstr(const nlohmann::json& obj, const char* key, const std::string& fallback = "") {
    if (obj.contains(key) && obj[key].is_string()) return obj[key].get<std::string>();
    return fallback;
}

// ============================================================================
// Network helpers
// ============================================================================

void ContentBrowserPanel::requestContentList(uint8_t contentType) {
    if (!netClient_) return;
    netClient_->sendAdminRequestContentList(contentType);
}

void ContentBrowserPanel::saveContent(uint8_t contentType, bool isNew, const nlohmann::json& data) {
    if (!netClient_) return;
    netClient_->sendAdminSaveContent(contentType, isNew, data.dump());
}

void ContentBrowserPanel::deleteContent(uint8_t contentType, const std::string& id) {
    if (!netClient_) return;
    netClient_->sendAdminDeleteContent(contentType, id);
}

void ContentBrowserPanel::saveSpawnZone(const nlohmann::json& zone) {
    saveContent(AdminContentType::SpawnZone, false, zone);
}

void ContentBrowserPanel::ensureSpawnListLoaded() {
    if (spawnListDirty_ && netClient_) {
        requestContentList(AdminContentType::SpawnZone);
        spawnListDirty_ = false;
    }
}

// ============================================================================
// Server response handlers
// ============================================================================

void ContentBrowserPanel::onContentListReceived(uint8_t contentType, uint16_t pageIndex,
                                                  uint16_t totalPages, const std::string& json) {
    auto parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) return;

    // Select the target list
    nlohmann::json* target = nullptr;
    bool* dirtyFlag = nullptr;
    switch (contentType) {
        case AdminContentType::Mob:       target = &mobList_;   dirtyFlag = &mobListDirty_; break;
        case AdminContentType::Item:      target = &itemList_;  dirtyFlag = &itemListDirty_; break;
        case AdminContentType::LootDrop:  target = &lootList_;  dirtyFlag = &lootListDirty_; break;
        case AdminContentType::SpawnZone: target = &spawnList_; dirtyFlag = &spawnListDirty_; break;
        default: return;
    }

    // First page clears existing data; subsequent pages append
    if (pageIndex == 0) {
        *target = std::move(parsed);
    } else {
        for (auto& entry : parsed) {
            target->push_back(std::move(entry));
        }
    }

    // Mark complete only after last page, then sort
    if (pageIndex + 1 >= totalPages) {
        *dirtyFlag = false;
        // Sort alphabetically by display name for better usability
        if (contentType == AdminContentType::Item) {
            // Consumable sub-group key for sorting
            auto consumableSortKey = [](const nlohmann::json& item) -> std::string {
                std::string sub = jstr(item, "subtype"), name = jstr(item, "name");
                std::string subL = sub, nameL = name;
                std::transform(subL.begin(), subL.end(), subL.begin(), ::tolower);
                std::transform(nameL.begin(), nameL.end(), nameL.begin(), ::tolower);
                if (subL.find("skill_book") != std::string::npos || subL.find("skillbook") != std::string::npos
                    || nameL.find("skill book") != std::string::npos || subL == "tome") return "1";
                if (subL.find("enhance") != std::string::npos || nameL.find("enhancement stone") != std::string::npos
                    || subL.find("stone") != std::string::npos) return "2";
                if (subL.find("potion") != std::string::npos || subL == "hp" || subL == "mp"
                    || nameL.find("potion") != std::string::npos || nameL.find("elixir") != std::string::npos) return "3";
                if (subL.find("scroll") != std::string::npos || nameL.find("scroll") != std::string::npos) return "4";
                if (subL.find("exp_boost") != std::string::npos || subL.find("stat_reset") != std::string::npos) return "5";
                return "6";
            };
            // Sort items by: type, then within equipment: class_req -> level -> name
            //                      within consumable: sub-group -> level -> name
            auto consumableSortKeyLocal = consumableSortKey;
            std::sort(target->begin(), target->end(),
                [&consumableSortKeyLocal](const nlohmann::json& a, const nlohmann::json& b) {
                    std::string ta = jstr(a, "type"), tb = jstr(b, "type");
                    // Normalize type case for sorting
                    std::string taL = ta, tbL = tb;
                    std::transform(taL.begin(), taL.end(), taL.begin(), ::tolower);
                    std::transform(tbL.begin(), tbL.end(), tbL.begin(), ::tolower);
                    if (taL != tbL) return taL < tbL;

                    if (ta == "Consumable") {
                        // Sub-group first (Skill Books, Enhancement, Potions, etc.)
                        std::string ga = consumableSortKeyLocal(a), gb = consumableSortKeyLocal(b);
                        if (ga != gb) return ga < gb;
                        // Then level
                        int la = 0, lb = 0;
                        if (a.contains("level_req") && a["level_req"].is_number()) la = a["level_req"].get<int>();
                        if (b.contains("level_req") && b["level_req"].is_number()) lb = b["level_req"].get<int>();
                        if (la != lb) return la < lb;
                    } else {
                        // Equipment: class then level
                        std::string ca = jstr(a, "class_req", "All"), cb = jstr(b, "class_req", "All");
                        if (ca != cb) return ca < cb;
                        int la = 0, lb = 0;
                        if (a.contains("level_req") && a["level_req"].is_number()) la = a["level_req"].get<int>();
                        if (b.contains("level_req") && b["level_req"].is_number()) lb = b["level_req"].get<int>();
                        if (la != lb) return la < lb;
                    }
                    std::string na = jstr(a, "name"), nb = jstr(b, "name");
                    std::transform(na.begin(), na.end(), na.begin(), ::tolower);
                    std::transform(nb.begin(), nb.end(), nb.begin(), ::tolower);
                    return na < nb;
                });
        } else if (contentType == AdminContentType::Mob) {
            // Sort mobs by min_spawn_level, then display_name
            std::sort(target->begin(), target->end(),
                [](const nlohmann::json& a, const nlohmann::json& b) {
                    int la = 0, lb = 0;
                    if (a.contains("min_spawn_level") && a["min_spawn_level"].is_number()) la = a["min_spawn_level"].get<int>();
                    if (b.contains("min_spawn_level") && b["min_spawn_level"].is_number()) lb = b["min_spawn_level"].get<int>();
                    if (la != lb) return la < lb;
                    std::string na = jstr(a, "display_name"), nb = jstr(b, "display_name");
                    std::transform(na.begin(), na.end(), na.begin(), ::tolower);
                    std::transform(nb.begin(), nb.end(), nb.begin(), ::tolower);
                    return na < nb;
                });
        } else {
            // Default alpha sort for spawns, loot
            auto getSort = [&](const nlohmann::json& e) -> std::string {
                if (contentType == AdminContentType::SpawnZone)
                    return jstr(e, "scene_id") + "/" + jstr(e, "mob_def_id");
                if (contentType == AdminContentType::LootDrop)
                    return jstr(e, "loot_table_id") + "/" + jstr(e, "item_id");
                return "";
            };
            std::sort(target->begin(), target->end(),
                [&](const nlohmann::json& a, const nlohmann::json& b) {
                    std::string sa = getSort(a), sb = getSort(b);
                    std::transform(sa.begin(), sa.end(), sa.begin(), ::tolower);
                    std::transform(sb.begin(), sb.end(), sb.begin(), ::tolower);
                    return sa < sb;
                });
        }
    }
}

void ContentBrowserPanel::onAdminResult(uint8_t requestType, bool success, const std::string& message) {
    (void)requestType;
    toastMessage_ = message;
    toastTimer_ = 4.0f;
    toastSuccess_ = success;

    // Refresh all lists on success
    if (success) {
        mobListDirty_ = true;
        itemListDirty_ = true;
        lootListDirty_ = true;
        spawnListDirty_ = true;
    }
}

void ContentBrowserPanel::onValidationReport(const std::vector<std::pair<uint8_t, std::string>>& issues) {
    // Accumulate chunks — first chunk clears (detected by having previously finished or being empty)
    // We use a simple heuristic: if the panel just sent a validate request, clear on first response
    if (validationPendingClear_) {
        validationIssues_.clear();
        validationPendingClear_ = false;
    }
    validationIssues_.insert(validationIssues_.end(), issues.begin(), issues.end());
}

// ============================================================================
// Toast
// ============================================================================

void ContentBrowserPanel::drawToast() {
    if (toastTimer_ <= 0.0f) return;
    float dt = ImGui::GetIO().DeltaTime;
    toastTimer_ -= dt;

    ImVec4 color = toastSuccess_ ? ImVec4(0.2f, 0.9f, 0.3f, 1.0f) : ImVec4(0.95f, 0.3f, 0.3f, 1.0f);
    ImGui::TextColored(color, "%s", toastMessage_.c_str());
}

// ============================================================================
// Field editor helpers
// ============================================================================

bool ContentBrowserPanel::drawStringField(const char* label, nlohmann::json& obj, const char* key, bool readOnly) {
    std::string val = jstr(obj, key);
    char buf[256];
    std::strncpy(buf, val.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    ImGui::Text("%s", label);
    ImGui::SameLine(140.0f);
    ImGui::SetNextItemWidth(-1.0f);
    ImGuiInputTextFlags flags = 0;
    if (readOnly) flags |= ImGuiInputTextFlags_ReadOnly;

    std::string id = std::string(label) + "##" + key;
    if (ImGui::InputText(id.c_str(), buf, sizeof(buf), flags)) {
        obj[key] = std::string(buf);
        return true;
    }
    return false;
}

bool ContentBrowserPanel::drawIntField(const char* label, nlohmann::json& obj, const char* key, int min, int max) {
    int val = 0;
    if (obj.contains(key)) {
        if (obj[key].is_number_integer()) val = obj[key].get<int>();
        else if (obj[key].is_number_float()) val = static_cast<int>(obj[key].get<float>());
    }

    ImGui::Text("%s", label);
    ImGui::SameLine(140.0f);
    ImGui::SetNextItemWidth(-1.0f);
    std::string id = std::string(label) + "##" + key;
    if (ImGui::DragInt(id.c_str(), &val, 1.0f, min, max)) {
        obj[key] = val;
        return true;
    }
    return false;
}

bool ContentBrowserPanel::drawFloatField(const char* label, nlohmann::json& obj, const char* key, float min, float max) {
    float val = 0.0f;
    if (obj.contains(key)) {
        if (obj[key].is_number_float()) val = obj[key].get<float>();
        else if (obj[key].is_number_integer()) val = static_cast<float>(obj[key].get<int>());
    }

    ImGui::Text("%s", label);
    ImGui::SameLine(140.0f);
    ImGui::SetNextItemWidth(-1.0f);
    std::string id = std::string(label) + "##" + key;
    if (ImGui::DragFloat(id.c_str(), &val, 0.01f, min, max, "%.3f")) {
        obj[key] = val;
        return true;
    }
    return false;
}

bool ContentBrowserPanel::drawBoolField(const char* label, nlohmann::json& obj, const char* key) {
    bool val = false;
    if (obj.contains(key) && obj[key].is_boolean()) val = obj[key].get<bool>();

    std::string id = std::string(label) + "##" + key;
    if (ImGui::Checkbox(id.c_str(), &val)) {
        obj[key] = val;
        return true;
    }
    return false;
}

bool ContentBrowserPanel::drawComboField(const char* label, nlohmann::json& obj, const char* key, const std::vector<std::string>& options) {
    std::string current = jstr(obj, key);
    int currentIdx = 0;
    for (int i = 0; i < (int)options.size(); ++i) {
        if (options[i] == current) { currentIdx = i; break; }
    }

    ImGui::Text("%s", label);
    ImGui::SameLine(140.0f);
    std::string id = std::string(label) + "##" + key;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(id.c_str(), currentIdx < (int)options.size() ? options[currentIdx].c_str() : "")) {
        for (int i = 0; i < (int)options.size(); ++i) {
            bool selected = (i == currentIdx);
            if (ImGui::Selectable(options[i].c_str(), selected)) {
                obj[key] = options[i];
                ImGui::EndCombo();
                return true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return false;
}

// ============================================================================
// Main draw
// ============================================================================

void ContentBrowserPanel::draw() {
    if (!open_) return;

    ImGui::SetNextWindowSize({900, 600}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Content Browser", &open_)) {
        ImGui::End();
        return;
    }

    drawToast();

    if (ImGui::BeginTabBar("##ContentBrowserTabs")) {
        if (ImGui::BeginTabItem("Mobs")) {
            if (mobListDirty_) { requestContentList(AdminContentType::Mob); mobListDirty_ = false; }
            drawMobsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Items")) {
            if (itemListDirty_) { requestContentList(AdminContentType::Item); itemListDirty_ = false; }
            drawItemsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Loot Tables")) {
            if (lootListDirty_) { requestContentList(AdminContentType::LootDrop); lootListDirty_ = false; }
            drawLootTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Spawn Zones")) {
            if (spawnListDirty_) { requestContentList(AdminContentType::SpawnZone); spawnListDirty_ = false; }
            drawSpawnsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Validation")) {
            drawValidationTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ============================================================================
// Mobs Tab
// ============================================================================

// Zone color palette for consistent scene coloring
static ImVec4 sceneColor(const std::string& scene) {
    // Deterministic color from scene name hash
    uint32_t h = 0;
    for (char c : scene) h = h * 31 + (uint8_t)c;
    float hue = (h % 360) / 360.0f;
    float sat = 0.5f + (h % 100) / 200.0f;
    ImVec4 col;
    ImGui::ColorConvertHSVtoRGB(hue, sat, 0.9f, col.x, col.y, col.z);
    col.w = 1.0f;
    return col;
}

void ContentBrowserPanel::drawMobsTab() {
    // Build mob -> scene map from spawn zones
    std::unordered_map<std::string, std::string> mobToScene;
    for (const auto& zone : spawnList_) {
        std::string mobId = jstr(zone, "mob_def_id");
        std::string scene = jstr(zone, "scene_id");
        if (!mobId.empty() && !scene.empty()) {
            mobToScene[mobId] = scene; // last scene wins if multi-zone
        }
    }

    // Collect unique scenes for the tab filter
    std::vector<std::string> scenes = {"All", "Unspawned"};
    {
        std::set<std::string> seen;
        for (const auto& [mid, sid] : mobToScene) {
            if (seen.insert(sid).second) scenes.push_back(sid);
        }
    }

    // Toolbar
    if (ImGui::Button("New Mob")) {
        selectedMobIndex_ = -1;
        editingMob_ = {
            {"_isNew", true},
            {"mob_def_id", ""}, {"display_name", "New Mob"}, {"mob_name", "new_mob"},
            {"base_hp", 100}, {"base_damage", 10}, {"base_armor", 5},
            {"crit_rate", 0.05f}, {"attack_speed", 1.0f}, {"move_speed", 64.0f},
            {"magic_resist", 0}, {"deals_magic_damage", false}, {"mob_hit_rate", 80},
            {"hp_per_level", 20.0f}, {"damage_per_level", 2.0f}, {"armor_per_level", 1.0f},
            {"base_xp_reward", 50}, {"xp_per_level", 10}, {"min_gold_drop", 1}, {"max_gold_drop", 10},
            {"gold_drop_chance", 0.5f}, {"honor_reward", 0}, {"loot_table_id", ""},
            {"aggro_range", 160.0f}, {"attack_range", 32.0f}, {"leash_radius", 320.0f},
            {"is_aggressive", true}, {"attack_style", "Melee"}, {"monster_type", "Normal"},
            {"is_boss", false}, {"is_elite", false},
            {"respawn_seconds", 60}, {"min_spawn_level", 1}, {"max_spawn_level", 5}, {"spawn_weight", 10}
        };
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##mobFilter", "Filter...", mobFilterBuf_, sizeof(mobFilterBuf_));
    ImGui::SameLine();
    ImGui::Text("(%d mobs)", (int)mobList_.size());

    // Zone filter tabs
    static int mobZoneFilter = 0; // index into scenes
    if (ImGui::BeginTabBar("##MobZoneTabs")) {
        for (int t = 0; t < (int)scenes.size(); ++t) {
            if (t > 1) ImGui::PushStyleColor(ImGuiCol_Text, sceneColor(scenes[t]));
            if (ImGui::BeginTabItem(scenes[t].c_str())) {
                mobZoneFilter = t;
                ImGui::EndTabItem();
            }
            if (t > 1) ImGui::PopStyleColor();
        }
        ImGui::EndTabBar();
    }

    // Left-right split
    float leftW = 260.0f;
    if (ImGui::BeginChild("##MobList", ImVec2(leftW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
        for (int i = 0; i < (int)mobList_.size(); ++i) {
            auto& mob = mobList_[i];
            std::string name = jstr(mob, "display_name", jstr(mob, "mob_name", "???"));
            std::string id = jstr(mob, "mob_def_id");

            if (!containsCI(name, mobFilterBuf_) && !containsCI(id, mobFilterBuf_)) continue;

            // Zone filter
            auto it = mobToScene.find(id);
            std::string mobScene = (it != mobToScene.end()) ? it->second : "";
            if (mobZoneFilter == 1 && !mobScene.empty()) continue;        // "Unspawned" tab
            if (mobZoneFilter >= 2 && mobScene != scenes[mobZoneFilter]) continue; // specific scene

            // Color by zone
            ImVec4 color = mobScene.empty() ? ImVec4{0.5f,0.5f,0.5f,1.0f} : sceneColor(mobScene);
            bool isBoss = mob.contains("is_boss") && mob["is_boss"].is_boolean() && mob["is_boss"].get<bool>();
            bool isElite = mob.contains("is_elite") && mob["is_elite"].is_boolean() && mob["is_elite"].get<bool>();

            int minLvl = 0, maxLvl = 0;
            if (mob.contains("min_spawn_level") && mob["min_spawn_level"].is_number()) minLvl = mob["min_spawn_level"].get<int>();
            if (mob.contains("max_spawn_level") && mob["max_spawn_level"].is_number()) maxLvl = mob["max_spawn_level"].get<int>();

            if (isBoss || isElite) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 0.85f, 0.2f, 1.0f});
            else ImGui::PushStyleColor(ImGuiCol_Text, color);

            bool selected = (i == selectedMobIndex_);
            std::string prefix = isBoss ? "[B] " : (isElite ? "[E] " : "");
            std::string lvlStr = (minLvl == maxLvl) ?
                "Lv" + std::to_string(minLvl) :
                "Lv" + std::to_string(minLvl) + "-" + std::to_string(maxLvl);
            std::string label = prefix + lvlStr + " " + name + "##mob" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), selected)) {
                selectedMobIndex_ = i;
                editingMob_ = mob;
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                int hp = 0;
                if (mob.contains("base_hp") && mob["base_hp"].is_number()) hp = mob["base_hp"].get<int>();
                ImGui::SetTooltip("%s\nHP: %d | %s\nZone: %s", id.c_str(), hp,
                    jstr(mob, "attack_style", "Melee").c_str(),
                    mobScene.empty() ? "Not spawned" : mobScene.c_str());
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right pane: editor
    if (ImGui::BeginChild("##MobEditor", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        if (editingMob_.is_null() || editingMob_.empty()) {
            ImGui::TextDisabled("Select or create a mob to edit");
        } else {
            bool isNewMob = editingMob_.contains("_isNew");

            if (ImGui::CollapsingHeader("Identity", ImGuiTreeNodeFlags_DefaultOpen)) {
                drawStringField("Def ID", editingMob_, "mob_def_id", !isNewMob);
                drawStringField("Display Name", editingMob_, "display_name");
                drawStringField("Mob Name", editingMob_, "mob_name");
            }

            if (ImGui::CollapsingHeader("Combat Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
                drawIntField("Base HP", editingMob_, "base_hp", 1, 9999999);
                drawIntField("Base Damage", editingMob_, "base_damage", 0, 999999);
                drawIntField("Base Armor", editingMob_, "base_armor", 0, 999999);
                drawFloatField("Crit Rate", editingMob_, "crit_rate", 0.0f, 1.0f);
                drawFloatField("Attack Speed", editingMob_, "attack_speed", 0.1f, 10.0f);
                drawFloatField("Move Speed", editingMob_, "move_speed", 0.0f, 999.0f);
                drawIntField("Magic Resist", editingMob_, "magic_resist", 0, 999999);
                drawBoolField("Deals Magic Damage", editingMob_, "deals_magic_damage");
                drawIntField("Mob Hit Rate", editingMob_, "mob_hit_rate", 0, 100);
            }

            if (ImGui::CollapsingHeader("Scaling")) {
                drawFloatField("HP / Level", editingMob_, "hp_per_level", 0.0f, 999.0f);
                drawFloatField("Damage / Level", editingMob_, "damage_per_level", 0.0f, 999.0f);
                drawFloatField("Armor / Level", editingMob_, "armor_per_level", 0.0f, 999.0f);
            }

            if (ImGui::CollapsingHeader("Rewards")) {
                drawIntField("Base XP Reward", editingMob_, "base_xp_reward");
                drawIntField("XP / Level", editingMob_, "xp_per_level");
                drawIntField("Min Gold Drop", editingMob_, "min_gold_drop");
                drawIntField("Max Gold Drop", editingMob_, "max_gold_drop");
                drawFloatField("Gold Drop Chance", editingMob_, "gold_drop_chance", 0.0f, 1.0f);
                drawIntField("Honor Reward", editingMob_, "honor_reward");
                drawStringField("Loot Table ID", editingMob_, "loot_table_id");
            }

            if (ImGui::CollapsingHeader("AI & Behavior")) {
                drawFloatField("Aggro Range", editingMob_, "aggro_range", 0.0f, 999.0f);
                drawFloatField("Attack Range", editingMob_, "attack_range", 0.0f, 999.0f);
                drawFloatField("Leash Radius", editingMob_, "leash_radius", 0.0f, 999.0f);
                drawBoolField("Is Aggressive", editingMob_, "is_aggressive");
                drawComboField("Attack Style", editingMob_, "attack_style", {"Melee", "Ranged", "Magic"});
                drawComboField("Monster Type", editingMob_, "monster_type", {"Normal", "Boss", "Elite", "Miniboss"});
                drawBoolField("Is Boss", editingMob_, "is_boss");
                drawBoolField("Is Elite", editingMob_, "is_elite");
            }

            if (ImGui::CollapsingHeader("Spawning")) {
                drawIntField("Respawn Seconds", editingMob_, "respawn_seconds", 0, 86400);
                drawIntField("Min Spawn Level", editingMob_, "min_spawn_level", 1, 200);
                drawIntField("Max Spawn Level", editingMob_, "max_spawn_level", 1, 200);
                drawIntField("Spawn Weight", editingMob_, "spawn_weight", 0, 1000);
            }

            // Show linked spawn zones (read-only summary — edit in Spawn Zones tab)
            if (ImGui::CollapsingHeader("Spawn Zones (read-only)")) {
                std::string mobId = jstr(editingMob_, "mob_def_id");
                int zoneCount = 0;
                for (int zi = 0; zi < (int)spawnList_.size(); ++zi) {
                    const auto& zone = spawnList_[zi];
                    if (jstr(zone, "mob_def_id") != mobId) continue;
                    zoneCount++;

                    std::string scene = jstr(zone, "scene_id", "???");
                    std::string zname = jstr(zone, "zone_name", "");
                    float cx = zone.value("center_x", 0.0f);
                    float cy = zone.value("center_y", 0.0f);
                    float rad = zone.value("radius", 0.0f);
                    int count = zone.value("target_count", 1);
                    std::string shape = jstr(zone, "zone_shape", "circle");

                    ImGui::BulletText("%s — %s", scene.c_str(), zname.c_str());
                    ImGui::Text("   Pos: (%.0f, %.0f)  Radius: %.0f  %s  x%d",
                                cx, cy, rad, shape.c_str(), count);
                }
                if (zoneCount == 0) {
                    ImGui::TextDisabled("No spawn zones for this mob");
                } else {
                    ImGui::TextDisabled("Edit zones in the Spawn Zones tab");
                }
            }

            ImGui::Separator();

            // Action buttons
            if (ImGui::Button("Save")) {
                nlohmann::json toSend = editingMob_;
                toSend.erase("_isNew");
                saveContent(AdminContentType::Mob, isNewMob, toSend);
            }
            ImGui::SameLine();
            if (!isNewMob && ImGui::Button("Delete")) {
                std::string id = jstr(editingMob_, "mob_def_id");
                if (!id.empty()) deleteContent(AdminContentType::Mob, id);
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert")) {
                if (isNewMob) {
                    editingMob_ = nlohmann::json();
                } else if (selectedMobIndex_ >= 0 && selectedMobIndex_ < (int)mobList_.size()) {
                    editingMob_ = mobList_[selectedMobIndex_];
                }
            }
        }
    }
    ImGui::EndChild();
}

// ============================================================================
// Items Tab
// ============================================================================

static ImVec4 rarityColor(const std::string& rarity) {
    if (rarity == "Uncommon")  return {0.2f,0.8f,0.2f,1};
    if (rarity == "Rare")      return {0.3f,0.5f,1.0f,1};
    if (rarity == "Epic")      return {0.7f,0.3f,0.9f,1};
    if (rarity == "Legendary") return {1.0f,0.6f,0.1f,1};
    return {0.85f,0.85f,0.85f,1}; // Common
}

// Item tab filter: matches by type string, or special logic for Accessory/Equipment
struct ItemTabDef {
    const char* label;
    // Returns true if item matches this tab
    bool (*matches)(const nlohmann::json& item);
};

static bool isAccessory(const nlohmann::json& item) {
    std::string sub = jstr(item, "subtype");
    return sub == "Necklace" || sub == "Ring" || sub == "Cloak" || sub == "Belt";
}

static const ItemTabDef itemTabs[] = {
    {"All",        [](const nlohmann::json&) { return true; }},
    {"Weapon",     [](const nlohmann::json& i) { return jstr(i,"type") == "Weapon"; }},
    {"Armor",      [](const nlohmann::json& i) { return jstr(i,"type") == "Armor" && !isAccessory(i); }},
    {"Accessory",  [](const nlohmann::json& i) { return isAccessory(i); }},
    {"Consumable", [](const nlohmann::json& i) { return jstr(i,"type") == "Consumable"; }},
    {"Material",   [](const nlohmann::json& i) {
        std::string t = jstr(i,"type");
        return t == "Material" || t == "material"; // DB has both cases
    }},
    {"Currency",   [](const nlohmann::json& i) { return jstr(i,"type") == "Currency"; }},
    {"Enhancement",[](const nlohmann::json& i) { return jstr(i,"type") == "Enhancement"; }},
};
static constexpr int NUM_ITEM_TABS = sizeof(itemTabs) / sizeof(itemTabs[0]);

void ContentBrowserPanel::drawItemsTab() {
    static int itemTypeFilter = 0;

    if (ImGui::Button("New Item")) {
        selectedItemIndex_ = -1;
        editingItem_ = {
            {"_isNew", true},
            {"item_id", ""}, {"name", "New Item"}, {"type", "Material"}, {"subtype", ""},
            {"rarity", "Common"}, {"class_req", "All"}, {"level_req", 1},
            {"damage_min", 0}, {"damage_max", 0}, {"armor", 0},
            {"gold_value", 1}, {"max_stack", 1},
            {"icon_path", ""}, {"visual_style", ""},
            {"is_socketable", false}, {"is_soulbound", false}, {"max_enchant", 0},
            {"description", ""}
        };
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##itemFilter", "Filter...", itemFilterBuf_, sizeof(itemFilterBuf_));
    ImGui::SameLine();
    ImGui::Text("(%d items)", (int)itemList_.size());

    // Type filter tabs
    if (ImGui::BeginTabBar("##ItemTypeTabs")) {
        for (int t = 0; t < NUM_ITEM_TABS; ++t) {
            if (ImGui::BeginTabItem(itemTabs[t].label)) {
                itemTypeFilter = t;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    // Consumable sub-category detection
    auto consumableCat = [](const nlohmann::json& item) -> int {
        std::string id = jstr(item, "item_id");
        std::string sub = jstr(item, "subtype"), name = jstr(item, "name");
        std::string subL = sub; std::transform(subL.begin(), subL.end(), subL.begin(), ::tolower);
        std::string nameL = name; std::transform(nameL.begin(), nameL.end(), nameL.begin(), ::tolower);
        if (id.starts_with("skillbook_") || subL.find("skill_book") != std::string::npos || subL == "tome") return 0; // Skill Books
        if (id.starts_with("mat_enhance_stone_") || id.starts_with("mat_protect_")) return 1; // Enhancement
        if (subL == "hp" || subL == "mp" || subL.find("potion") != std::string::npos
            || nameL.find("potion") != std::string::npos || nameL.find("elixir") != std::string::npos) return 2; // Potions
        if (subL == "scroll" || nameL.find("scroll:") != std::string::npos) return 3; // Scrolls
        return 4; // Other
    };

    bool isConsumableTab = (itemTypeFilter == 4);
    bool isEquipTab = (itemTypeFilter >= 1 && itemTypeFilter <= 3);

    // Consumable sub-tabs
    static int consumableSubFilter = -1; // -1 = All
    if (isConsumableTab) {
        static const char* conSubTabs[] = {"All", "Skill Books", "Enhancement", "Potions", "Scrolls", "Other"};
        if (ImGui::BeginTabBar("##ConSubTabs")) {
            for (int t = 0; t < 6; ++t) {
                if (ImGui::BeginTabItem(conSubTabs[t])) {
                    consumableSubFilter = t - 1; // -1=All, 0=SkillBooks, 1=Enhancement, etc.
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }

    float leftW = 280.0f;
    if (ImGui::BeginChild("##ItemList", ImVec2(leftW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
        std::string lastGroup;

        for (int i = 0; i < (int)itemList_.size(); ++i) {
            auto& item = itemList_[i];
            std::string name = jstr(item, "name", "???");
            std::string id = jstr(item, "item_id");
            std::string classReq = jstr(item, "class_req", "All");
            int levelReq = 0;
            if (item.contains("level_req") && item["level_req"].is_number())
                levelReq = item["level_req"].get<int>();

            if (!containsCI(name, itemFilterBuf_) && !containsCI(id, itemFilterBuf_)) continue;
            if (!itemTabs[itemTypeFilter].matches(item)) continue;

            // Consumable sub-filter
            if (isConsumableTab && consumableSubFilter >= 0) {
                if (consumableCat(item) != consumableSubFilter) continue;
            }

            // Group headers by class for equipment tabs (skip "All" — it provides no grouping value)
            if (isEquipTab && classReq != "All" && classReq != lastGroup) {
                if (!lastGroup.empty()) ImGui::Spacing();
                ImGui::TextColored({0.6f,0.8f,1.0f,1.0f}, "-- %s --", classReq.c_str());
                ImGui::Separator();
                lastGroup = classReq;
            }

            std::string rarity = jstr(item, "rarity", "Common");
            ImVec4 color = rarityColor(rarity);

            bool selected = (i == selectedItemIndex_);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            std::string label;
            if ((isEquipTab || isConsumableTab) && levelReq > 0)
                label = "Lv" + std::to_string(levelReq) + " " + name + "##item" + std::to_string(i);
            else
                label = name + "##item" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), selected)) {
                selectedItemIndex_ = i;
                editingItem_ = item;
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                std::string sub = jstr(item, "subtype");
                ImGui::SetTooltip("[%s] %s%s | %s | Lv%d\n%s",
                    rarity.c_str(), jstr(item, "type").c_str(),
                    sub.empty() ? "" : (" / " + sub).c_str(),
                    classReq.c_str(), levelReq, id.c_str());
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    if (ImGui::BeginChild("##ItemEditor", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        if (editingItem_.is_null() || editingItem_.empty()) {
            ImGui::TextDisabled("Select or create an item to edit");
        } else {
            bool isNewItem = editingItem_.contains("_isNew");

            if (ImGui::CollapsingHeader("Identity", ImGuiTreeNodeFlags_DefaultOpen)) {
                drawStringField("Item ID", editingItem_, "item_id", !isNewItem);
                drawStringField("Name", editingItem_, "name");
                drawComboField("Type", editingItem_, "type",
                    {"Weapon", "Armor", "Accessory", "Consumable", "Material", "QuestItem", "Scroll", "Bag", "Pet"});
                drawStringField("Subtype", editingItem_, "subtype");
                drawComboField("Rarity", editingItem_, "rarity",
                    {"Common", "Uncommon", "Rare", "Epic", "Legendary"});
            }

            if (ImGui::CollapsingHeader("Requirements")) {
                drawComboField("Class Req", editingItem_, "class_req",
                    {"All", "Warrior", "Mage", "Archer", "Healer"});
                drawIntField("Level Req", editingItem_, "level_req", 1, 200);
            }

            if (ImGui::CollapsingHeader("Stats")) {
                drawIntField("Damage Min", editingItem_, "damage_min");
                drawIntField("Damage Max", editingItem_, "damage_max");
                drawIntField("Armor", editingItem_, "armor");
            }

            if (ImGui::CollapsingHeader("Economy")) {
                drawIntField("Gold Value", editingItem_, "gold_value");
                drawIntField("Max Stack", editingItem_, "max_stack", 1, 9999);
            }

            if (ImGui::CollapsingHeader("Visual")) {
                drawStringField("Icon Path", editingItem_, "icon_path");
                drawStringField("Visual Style", editingItem_, "visual_style");
            }

            if (ImGui::CollapsingHeader("Flags")) {
                drawBoolField("Is Socketable", editingItem_, "is_socketable");
                drawBoolField("Is Soulbound", editingItem_, "is_soulbound");
                drawIntField("Max Enchant", editingItem_, "max_enchant", 0, 15);
            }

            if (ImGui::CollapsingHeader("Description")) {
                std::string desc = jstr(editingItem_, "description");
                char descBuf[1024];
                std::strncpy(descBuf, desc.c_str(), sizeof(descBuf) - 1);
                descBuf[sizeof(descBuf) - 1] = '\0';
                if (ImGui::InputTextMultiline("##desc", descBuf, sizeof(descBuf), ImVec2(-1, 100))) {
                    editingItem_["description"] = std::string(descBuf);
                }
            }

            ImGui::Separator();

            if (ImGui::Button("Save")) {
                nlohmann::json toSend = editingItem_;
                toSend.erase("_isNew");
                saveContent(AdminContentType::Item, isNewItem, toSend);
            }
            ImGui::SameLine();
            if (!isNewItem && ImGui::Button("Delete")) {
                std::string id = jstr(editingItem_, "item_id");
                if (!id.empty()) deleteContent(AdminContentType::Item, id);
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert")) {
                if (isNewItem) {
                    editingItem_ = nlohmann::json();
                } else if (selectedItemIndex_ >= 0 && selectedItemIndex_ < (int)itemList_.size()) {
                    editingItem_ = itemList_[selectedItemIndex_];
                }
            }
        }
    }
    ImGui::EndChild();
}

// ============================================================================
// Loot Tables Tab
// ============================================================================

void ContentBrowserPanel::drawLootTab() {
    // Collect unique loot_table_id values
    std::vector<std::string> tableIds;
    for (auto& entry : lootList_) {
        std::string tid = jstr(entry, "loot_table_id");
        if (!tid.empty() && std::find(tableIds.begin(), tableIds.end(), tid) == tableIds.end()) {
            tableIds.push_back(tid);
        }
    }
    std::sort(tableIds.begin(), tableIds.end());

    // Build loot_table_id -> mob display_name map
    std::unordered_map<std::string, std::string> tableToMob;
    std::unordered_map<std::string, std::string> tableToMobScene;
    for (const auto& mob : mobList_) {
        std::string lt = jstr(mob, "loot_table_id");
        if (!lt.empty()) {
            tableToMob[lt] = jstr(mob, "display_name", jstr(mob, "mob_def_id"));
            // Cross-ref with spawn zones for scene
            std::string mobId = jstr(mob, "mob_def_id");
            for (const auto& zone : spawnList_) {
                if (jstr(zone, "mob_def_id") == mobId) {
                    tableToMobScene[lt] = jstr(zone, "scene_id");
                    break;
                }
            }
        }
    }

    // Count entries per table
    std::unordered_map<std::string, int> tableEntryCount;
    for (const auto& entry : lootList_) {
        tableEntryCount[jstr(entry, "loot_table_id")]++;
    }

    ImGui::Text("%d loot tables, %d total entries", (int)tableIds.size(), (int)lootList_.size());
    ImGui::Separator();

    // Left pane: table list
    float leftW = 280.0f;
    if (ImGui::BeginChild("##LootTableList", ImVec2(leftW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
        for (auto& tid : tableIds) {
            auto mobIt = tableToMob.find(tid);
            auto sceneIt = tableToMobScene.find(tid);
            std::string mobName = (mobIt != tableToMob.end()) ? mobIt->second : "";
            std::string scene = (sceneIt != tableToMobScene.end()) ? sceneIt->second : "";
            int count = tableEntryCount[tid];

            // Color by scene (matches mob tab coloring)
            ImVec4 color = scene.empty() ? ImVec4{0.6f,0.6f,0.6f,1.0f} : sceneColor(scene);
            ImGui::PushStyleColor(ImGuiCol_Text, color);

            bool selected = (tid == selectedLootTable_);
            std::string label = tid + " (" + std::to_string(count) + ")##loot_" + tid;
            if (ImGui::Selectable(label.c_str(), selected)) {
                selectedLootTable_ = tid;
            }
            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered()) {
                if (!mobName.empty())
                    ImGui::SetTooltip("Mob: %s\nScene: %s\n%d drop entries", mobName.c_str(),
                        scene.empty() ? "Not spawned" : scene.c_str(), count);
                else
                    ImGui::SetTooltip("No mob assigned\n%d drop entries", count);
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right pane: entries for selected table
    if (ImGui::BeginChild("##LootEntries", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        if (selectedLootTable_.empty()) {
            ImGui::TextDisabled("Select a loot table");
        } else {
            ImGui::Text("Table: %s", selectedLootTable_.c_str());
            ImGui::Separator();

            // Add Entry button
            if (ImGui::Button("Add Entry")) {
                nlohmann::json newEntry = {
                    {"loot_table_id", selectedLootTable_},
                    {"item_id", ""},
                    {"drop_chance", 0.1f},
                    {"min_quantity", 1},
                    {"max_quantity", 1}
                };
                lootList_.push_back(newEntry);
            }

            ImGui::Separator();

            int removeIdx = -1;
            int entryNum = 0;
            for (int i = 0; i < (int)lootList_.size(); ++i) {
                auto& entry = lootList_[i];
                if (jstr(entry, "loot_table_id") != selectedLootTable_) continue;

                ImGui::PushID(i);
                entryNum++;

                ImGui::Text("Entry %d", entryNum);
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);

                if (ImGui::Button("Save##loot")) {
                    saveContent(AdminContentType::LootDrop, false, entry);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) {
                    removeIdx = i;
                }

                // Item ID
                std::string itemId = jstr(entry, "item_id");
                char itemBuf[128];
                std::strncpy(itemBuf, itemId.c_str(), sizeof(itemBuf) - 1);
                itemBuf[sizeof(itemBuf) - 1] = '\0';
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::InputText("Item ID", itemBuf, sizeof(itemBuf))) {
                    entry["item_id"] = std::string(itemBuf);
                }

                // Drop chance slider
                float chance = 0.1f;
                if (entry.contains("drop_chance") && entry["drop_chance"].is_number())
                    chance = entry["drop_chance"].get<float>();
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::SliderFloat("Drop Chance", &chance, 0.0f, 1.0f, "%.3f")) {
                    entry["drop_chance"] = chance;
                }

                // Min/max quantity
                int minQ = 1, maxQ = 1;
                if (entry.contains("min_quantity") && entry["min_quantity"].is_number_integer())
                    minQ = entry["min_quantity"].get<int>();
                if (entry.contains("max_quantity") && entry["max_quantity"].is_number_integer())
                    maxQ = entry["max_quantity"].get<int>();
                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::DragInt("Min Qty", &minQ, 1.0f, 1, 999)) entry["min_quantity"] = minQ;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::DragInt("Max Qty", &maxQ, 1.0f, 1, 999)) entry["max_quantity"] = maxQ;

                ImGui::Separator();
                ImGui::PopID();
            }

            // Handle deletion — server expects "table_id:item_id" composite format
            if (removeIdx >= 0) {
                auto& entry = lootList_[removeIdx];
                std::string tableId = entry.value("loot_table_id", "");
                std::string itemId = entry.value("item_id", "");
                if (!tableId.empty() && !itemId.empty()) {
                    deleteContent(AdminContentType::LootDrop, tableId + ":" + itemId);
                }
                lootList_.erase(lootList_.begin() + removeIdx);
            }
        }
    }
    ImGui::EndChild();
}

// ============================================================================
// Spawn Zones Tab
// ============================================================================

void ContentBrowserPanel::drawSpawnsTab() {
    if (ImGui::Button("New Spawn Zone")) {
        selectedSpawnIndex_ = -1;
        editingSpawn_ = {
            {"_isNew", true},
            {"scene_id", ""}, {"zone_name", "New Zone"}, {"mob_def_id", ""},
            {"center_x", 0.0f}, {"center_y", 0.0f}, {"radius", 128.0f},
            {"zone_shape", "circle"}, {"target_count", 5},
            {"respawn_override_seconds", -1}
        };
    }
    ImGui::SameLine();
    ImGui::Text("(%d zones)", (int)spawnList_.size());

    float leftW = 280.0f;
    if (ImGui::BeginChild("##SpawnList", ImVec2(leftW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
        // Group by scene
        std::string lastScene;
        for (int i = 0; i < (int)spawnList_.size(); ++i) {
            auto& zone = spawnList_[i];
            std::string scene = jstr(zone, "scene_id", "???");
            std::string mobId = jstr(zone, "mob_def_id");
            std::string zoneName = jstr(zone, "zone_name");
            int count = 0;
            if (zone.contains("target_count") && zone["target_count"].is_number())
                count = zone["target_count"].get<int>();

            // Scene group header (clickable — selects all zones for this scene)
            if (scene != lastScene) {
                if (!lastScene.empty()) ImGui::Spacing();
                bool sceneSelected = (selectedSpawnScene_ == scene && selectedSpawnIndex_ < 0);
                if (sceneSelected) ImGui::PushStyleColor(ImGuiCol_Text, {1.0f, 0.9f, 0.2f, 1.0f});
                else               ImGui::PushStyleColor(ImGuiCol_Text, {0.6f, 0.8f, 1.0f, 1.0f});
                std::string sceneLabel = scene + "##scene_hdr_" + scene;
                if (ImGui::Selectable(sceneLabel.c_str(), sceneSelected)) {
                    selectedSpawnScene_ = scene;
                    selectedSpawnIndex_ = -1;
                    editingSpawn_ = nlohmann::json();
                }
                ImGui::PopStyleColor();
                ImGui::Separator();
                lastScene = scene;
            }

            // Look up mob level range
            const nlohmann::json* listMob = nullptr;
            for (const auto& m : mobList_) {
                if (jstr(m, "mob_def_id") == mobId) { listMob = &m; break; }
            }
            int minLvl = listMob ? listMob->value("min_spawn_level", 0) : 0;
            int maxLvl = listMob ? listMob->value("max_spawn_level", 0) : 0;
            std::string lvl = (minLvl == maxLvl)
                ? "Lv" + std::to_string(minLvl)
                : "Lv" + std::to_string(minLvl) + "-" + std::to_string(maxLvl);

            bool selected = (i == selectedSpawnIndex_);
            std::string label = "  " + lvl + " " + mobId + " x" + std::to_string(count);
            if (!zoneName.empty()) label += "  [" + zoneName + "]";
            label += "##spawn" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), selected)) {
                selectedSpawnIndex_ = i;
                selectedSpawnScene_.clear();
                editingSpawn_ = zone;
            }
            if (ImGui::IsItemHovered()) {
                float cx = zone.value("center_x", 0.0f);
                float cy = zone.value("center_y", 0.0f);
                float rad = zone.value("radius", 0.0f);
                std::string dispName = listMob ? jstr(*listMob, "display_name", mobId) : mobId;
                ImGui::SetTooltip("%s — %s\nPos: (%.0f, %.0f)  Radius: %.0f", dispName.c_str(), zoneName.c_str(), cx, cy, rad);
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    if (ImGui::BeginChild("##SpawnEditor", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        if (editingSpawn_.is_null() || editingSpawn_.empty()) {
            ImGui::TextDisabled("Select or create a spawn zone to edit");
        } else {
            bool isNewSpawn = editingSpawn_.contains("_isNew");

            // Look up mob definition for display info
            std::string spawnMobId = jstr(editingSpawn_, "mob_def_id");
            const nlohmann::json* linkedMob = nullptr;
            for (const auto& mob : mobList_) {
                if (jstr(mob, "mob_def_id") == spawnMobId) { linkedMob = &mob; break; }
            }

            // Mob info header (read-only context from mob definition)
            if (linkedMob) {
                std::string dispName = jstr(*linkedMob, "display_name", spawnMobId);
                int minLvl = linkedMob->value("min_spawn_level", 0);
                int maxLvl = linkedMob->value("max_spawn_level", 0);
                int hp = linkedMob->value("base_hp", 0);
                std::string atkStyle = jstr(*linkedMob, "attack_style", "Melee");
                bool isBoss = linkedMob->value("is_boss", false);
                bool isElite = linkedMob->value("is_elite", false);

                std::string tag = isBoss ? " [BOSS]" : (isElite ? " [ELITE]" : "");
                std::string lvlStr = (minLvl == maxLvl)
                    ? "Lv " + std::to_string(minLvl)
                    : "Lv " + std::to_string(minLvl) + "-" + std::to_string(maxLvl);

                ImGui::TextColored({1.0f, 0.9f, 0.4f, 1.0f}, "%s%s", dispName.c_str(), tag.c_str());
                ImGui::Text("%s  |  HP: %d  |  %s", lvlStr.c_str(), hp, atkStyle.c_str());
                ImGui::Separator();
            }

            drawStringField("Scene ID", editingSpawn_, "scene_id");
            drawStringField("Zone Name", editingSpawn_, "zone_name");
            drawStringField("Mob Def ID", editingSpawn_, "mob_def_id");
            drawFloatField("Center X", editingSpawn_, "center_x", -999999.0f, 999999.0f);
            drawFloatField("Center Y", editingSpawn_, "center_y", -999999.0f, 999999.0f);
            drawFloatField("Width (radius)", editingSpawn_, "radius", 0.0f, 999999.0f);
            drawFloatField("Height", editingSpawn_, "height", 0.0f, 999999.0f);
            ImGui::SameLine();
            ImGui::TextDisabled("(0 = same as width)");
            drawComboField("Zone Shape", editingSpawn_, "zone_shape", {"circle", "rectangle"});
            drawIntField("Target Count", editingSpawn_, "target_count", 0, 1000);
            drawIntField("Respawn Override (sec)", editingSpawn_, "respawn_override_seconds", -1, 86400);

            ImGui::Separator();

            if (ImGui::Button("Save")) {
                nlohmann::json toSend = editingSpawn_;
                toSend.erase("_isNew");
                saveContent(AdminContentType::SpawnZone, isNewSpawn, toSend);
            }
            ImGui::SameLine();
            if (!isNewSpawn && ImGui::Button("Delete")) {
                std::string zoneId;
                if (editingSpawn_.contains("zone_id")) {
                    if (editingSpawn_["zone_id"].is_number())
                        zoneId = std::to_string(editingSpawn_["zone_id"].get<int>());
                    else if (editingSpawn_["zone_id"].is_string())
                        zoneId = editingSpawn_["zone_id"].get<std::string>();
                }
                if (!zoneId.empty()) deleteContent(AdminContentType::SpawnZone, zoneId);
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert")) {
                if (isNewSpawn) {
                    editingSpawn_ = nlohmann::json();
                } else if (selectedSpawnIndex_ >= 0 && selectedSpawnIndex_ < (int)spawnList_.size()) {
                    editingSpawn_ = spawnList_[selectedSpawnIndex_];
                }
            }
        }
    }
    ImGui::EndChild();
}

// ============================================================================
// Validation Tab
// ============================================================================

void ContentBrowserPanel::drawValidationTab() {
    if (ImGui::Button("Run Validation")) {
        if (netClient_) {
            validationPendingClear_ = true;
            netClient_->sendAdminValidate();
        }
    }

    ImGui::SameLine();
    ImGui::Checkbox("Errors", &showErrors_);
    ImGui::SameLine();
    ImGui::Checkbox("Warnings", &showWarnings_);
    ImGui::SameLine();
    ImGui::Checkbox("Info", &showInfo_);

    ImGui::Separator();

    if (validationIssues_.empty()) {
        ImGui::TextDisabled("No validation results. Click 'Run Validation' to check content.");
        return;
    }

    int errorCount = 0, warnCount = 0, infoCount = 0;
    for (auto& [sev, msg] : validationIssues_) {
        if (sev == 0) errorCount++;
        else if (sev == 1) warnCount++;
        else infoCount++;
    }
    ImGui::Text("Results: %d errors, %d warnings, %d info", errorCount, warnCount, infoCount);
    ImGui::Separator();

    if (ImGui::BeginChild("##ValidationResults", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        for (auto& [severity, message] : validationIssues_) {
            if (severity == 0 && !showErrors_) continue;
            if (severity == 1 && !showWarnings_) continue;
            if (severity == 2 && !showInfo_) continue;

            ImVec4 color;
            const char* prefix;
            if (severity == 0)      { color = {0.95f, 0.3f, 0.3f, 1.0f}; prefix = "[ERROR] "; }
            else if (severity == 1) { color = {0.95f, 0.85f, 0.2f, 1.0f}; prefix = "[WARN]  "; }
            else                    { color = {0.3f, 0.6f, 0.95f, 1.0f}; prefix = "[INFO]  "; }

            ImGui::TextColored(color, "%s%s", prefix, message.c_str());
        }
    }
    ImGui::EndChild();
}

} // namespace fate
