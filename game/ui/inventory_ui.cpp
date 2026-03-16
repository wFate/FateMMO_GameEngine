#include "game/ui/inventory_ui.h"
#include "engine/core/logger.h"
#include "game/components/transform.h"
#include "game/components/player_controller.h"
#include "game/components/game_components.h"
#include <cstdio>
#include <cmath>

namespace fate {

// ============================================================================
// Main Draw
// ============================================================================

void InventoryUI::draw(World* world) {
    if (!open_ || !world) return;

    Entity* player = findPlayer(world);
    if (!player) return;

    Inventory* inv = findPlayerInventory(world);

    ImGuiIO& io = ImGui::GetIO();
    float panelW = 380.0f;
    float panelH = 420.0f;
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - panelW) * 0.5f,
                                    (io.DisplaySize.y - panelH) * 0.5f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 0.95f));

    if (!ImGui::Begin("##InventoryPanel", &open_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        return;
    }

    // Tab bar
    if (ImGui::BeginTabBar("InventoryTabs")) {
        if (ImGui::BeginTabItem("Inventory")) {
            activeTab_ = Tab::Inventory;
            if (inv) drawInventoryTab(inv);
            else ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "No inventory component");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Stats")) {
            activeTab_ = Tab::Stats;
            drawStatsTab(world, player);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Skills")) {
            activeTab_ = Tab::Skills;
            drawSkillsTab(world, player);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Community")) {
            activeTab_ = Tab::Community;
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Community features coming soon");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings")) {
            activeTab_ = Tab::Settings;
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Settings coming soon");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ============================================================================
// Find Player
// ============================================================================

Entity* InventoryUI::findPlayer(World* world) {
    Entity* result = nullptr;
    world->forEach<Transform, PlayerController>(
        [&](Entity* e, Transform*, PlayerController* ctrl) {
            if (ctrl->isLocalPlayer) result = e;
        }
    );
    return result;
}

Inventory* InventoryUI::findPlayerInventory(World* world) {
    Inventory* result = nullptr;
    world->forEach<PlayerController, InventoryComponent>(
        [&](Entity*, PlayerController* ctrl, InventoryComponent* ic) {
            if (ctrl->isLocalPlayer) result = &ic->inventory;
        }
    );
    return result;
}

// ============================================================================
// Inventory Tab
// ============================================================================

void InventoryUI::drawInventoryTab(Inventory* inv) {
    float slotSize = 40.0f;
    int cols = 5;

    // Left side: inventory grid (15 slots, 5x3)
    ImGui::BeginChild("InvGrid", ImVec2(cols * (slotSize + 4) + 8, 0), true);
    ImGui::Text("Backpack (%d/%d)", inv->usedSlots(), inv->totalSlots());
    ImGui::Separator();

    for (int i = 0; i < inv->totalSlots(); i++) {
        if (i % cols != 0) ImGui::SameLine(0, 4);
        drawItemSlot(inv, i, slotSize);
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Gold: %s", formatGold(inv->getGold()).c_str());

    ImGui::EndChild();

    ImGui::SameLine();

    // Right side: equipment slots
    ImGui::BeginChild("EquipPanel", ImVec2(0, 0), true);
    ImGui::Text("Equipment");
    ImGui::Separator();

    float equipSize = 36.0f;
    drawEquipmentSlot(inv, EquipmentSlot::Hat,       "Hat",    equipSize);
    drawEquipmentSlot(inv, EquipmentSlot::Armor,     "Armor",  equipSize);
    drawEquipmentSlot(inv, EquipmentSlot::Gloves,    "Gloves", equipSize);
    drawEquipmentSlot(inv, EquipmentSlot::Shoes,     "Shoes",  equipSize);
    drawEquipmentSlot(inv, EquipmentSlot::Belt,      "Belt",   equipSize);
    drawEquipmentSlot(inv, EquipmentSlot::Cloak,     "Cloak",  equipSize);
    ImGui::Separator();
    drawEquipmentSlot(inv, EquipmentSlot::Weapon,    "Weapon", equipSize);
    drawEquipmentSlot(inv, EquipmentSlot::SubWeapon, "Shield", equipSize);
    ImGui::Separator();
    drawEquipmentSlot(inv, EquipmentSlot::Ring,      "Ring",   equipSize);
    drawEquipmentSlot(inv, EquipmentSlot::Necklace,  "Neck",   equipSize);

    ImGui::EndChild();
}

// ============================================================================
// Item Slot (inventory grid)
// ============================================================================

void InventoryUI::drawItemSlot(Inventory* inv, int slotIndex, float size) {
    ItemInstance item = inv->getSlot(slotIndex);
    bool hasItem = item.isValid();
    bool isLocked = inv->isSlotLocked(slotIndex);

    ImGui::PushID(slotIndex);

    // Slot background color
    ImVec4 bgColor = hasItem ? ImVec4(0.15f, 0.15f, 0.2f, 1.0f) : ImVec4(0.1f, 0.1f, 0.13f, 1.0f);
    if (isLocked) bgColor = ImVec4(0.3f, 0.1f, 0.1f, 1.0f);

    // Rarity border
    if (hasItem) {
        ImVec4 rarityCol = getRarityColor(ItemRarity::Common /* TODO: from item def cache */);
        ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
        ImGui::PushStyleColor(ImGuiCol_Border, rarityCol);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    }

    // Draw the slot button
    char label[32];
    if (hasItem) {
        if (item.enchantLevel > 0)
            snprintf(label, sizeof(label), "+%d", item.enchantLevel);
        else if (item.quantity > 1)
            snprintf(label, sizeof(label), "x%d", item.quantity);
        else
            snprintf(label, sizeof(label), "##slot%d", slotIndex);
    } else {
        snprintf(label, sizeof(label), "##slot%d", slotIndex);
    }

    ImGui::Button(label, ImVec2(size, size));

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    // Drag source
    if (hasItem && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        dragSourceSlot_ = slotIndex;
        dragFromEquipment_ = false;
        ImGui::SetDragDropPayload("ITEM_SLOT", &slotIndex, sizeof(int));
        ImGui::Text("%s", item.itemId.c_str());
        ImGui::EndDragDropSource();
    }

    // Drop target
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ITEM_SLOT")) {
            int sourceSlot = *(const int*)payload->Data;
            if (!dragFromEquipment_) {
                inv->moveItem(sourceSlot, slotIndex);
                LOG_DEBUG("InvUI", "Moved item from slot %d to %d", sourceSlot, slotIndex);
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("EQUIP_SLOT")) {
            EquipmentSlot eqSlot = *(const EquipmentSlot*)payload->Data;
            inv->unequipItem(eqSlot);
            LOG_DEBUG("InvUI", "Unequipped from %s", getEquipSlotName(eqSlot));
        }
        ImGui::EndDragDropTarget();
    }

    // Tooltip
    if (hasItem && ImGui::IsItemHovered()) {
        drawItemTooltip(item);
    }

    // Double-click to equip
    if (hasItem && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        // Try to auto-equip based on item type
        // For now, just log — proper equip logic needs item definition lookup
        LOG_INFO("InvUI", "Double-click equip: %s", item.itemId.c_str());
    }

    ImGui::PopID();
}

// ============================================================================
// Equipment Slot
// ============================================================================

void InventoryUI::drawEquipmentSlot(Inventory* inv, EquipmentSlot slot, const char* label, float size) {
    ItemInstance item = inv->getEquipment(slot);
    bool hasItem = item.isValid();

    ImGui::PushID((int)slot + 1000);

    ImVec4 bgColor = hasItem ? ImVec4(0.15f, 0.18f, 0.25f, 1.0f) : ImVec4(0.08f, 0.08f, 0.1f, 1.0f);

    if (hasItem) {
        ImVec4 rarityCol = getRarityColor(ItemRarity::Common /* TODO: from item def cache */);
        ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
        ImGui::PushStyleColor(ImGuiCol_Border, rarityCol);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.2f, 0.3f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    }

    char btnLabel[32];
    snprintf(btnLabel, sizeof(btnLabel), "%s##eq%d", hasItem ? "" : label, (int)slot);
    ImGui::Button(btnLabel, ImVec2(size, size));

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    if (hasItem) {
        ImGui::Text("%s", label);
        if (item.enchantLevel > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "+%d", item.enchantLevel);
        }
    } else {
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "%s", label);
    }

    // Drag source (from equipment)
    if (hasItem && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        dragFromEquipment_ = true;
        dragEquipSlot_ = slot;
        ImGui::SetDragDropPayload("EQUIP_SLOT", &slot, sizeof(EquipmentSlot));
        ImGui::Text("%s", item.itemId.c_str());
        ImGui::EndDragDropSource();
    }

    // Drop target (equip from inventory)
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ITEM_SLOT")) {
            int sourceSlot = *(const int*)payload->Data;
            inv->equipItem(sourceSlot, slot);
            LOG_DEBUG("InvUI", "Equipped from slot %d to %s", sourceSlot, getEquipSlotName(slot));
        }
        ImGui::EndDragDropTarget();
    }

    // Tooltip
    if (hasItem && ImGui::IsItemHovered()) {
        drawItemTooltip(item);
    }

    // Double-click to unequip
    if (hasItem && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        inv->unequipItem(slot);
    }

    ImGui::PopID();
}

// ============================================================================
// Item Tooltip
// ============================================================================

void InventoryUI::drawItemTooltip(const ItemInstance& item) {
    ImGui::BeginTooltip();

    // Item name with rarity color
    ImVec4 rarityCol = getRarityColor(ItemRarity::Common /* TODO: from item def cache */);
    std::string name = item.itemId;
    if (item.enchantLevel > 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s +%d", item.itemId.c_str(), item.enchantLevel);
        name = buf;
    }
    ImGui::TextColored(rarityCol, "%s", name.c_str());
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "%s", getRarityName(ItemRarity::Common /* TODO: from item def cache */));

    ImGui::Separator();

    // Quantity
    if (item.quantity > 1) {
        ImGui::Text("Quantity: %d", item.quantity);
    }

    // Rolled stats
    if (!item.rolledStats.empty()) {
        for (auto& stat : item.rolledStats) {
            const char* statNames[] = {
                "STR", "VIT", "INT", "DEX", "WIS",
                "HP", "MP", "Hit Rate", "Crit", "Block",
                "Armor", "Evasion", "Lifesteal", "Move Speed"
            };
            int idx = (int)stat.statType;
            if (idx >= 0 && idx < 14) {
                ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "  +%d %s", stat.value, statNames[idx]);
            }
        }
    }

    // Socket
    if (!item.socket.isEmpty) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 1.0f, 1.0f), "  Socket: +%d", item.socket.value);
    }

    // Enchant level
    if (item.enchantLevel > 0) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "  Enhancement: +%d", item.enchantLevel);
    }

    // Flags
    if (item.isProtected) ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "  Protected");
    if (item.isSoulbound) ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "  Soulbound");

    ImGui::EndTooltip();
}

// ============================================================================
// Stats Tab
// ============================================================================

void InventoryUI::drawStatsTab(World* world, Entity* player) {
    auto* statsComp = player->getComponent<CharacterStatsComponent>();
    if (!statsComp) {
        ImGui::Text("No character stats");
        return;
    }

    auto& s = statsComp->stats;

    ImGui::Text("%s — Level %d %s", s.characterName.c_str(), s.level, s.className.c_str());
    ImGui::Separator();

    // HP / MP
    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "HP: %d / %d", s.currentHP, s.maxHP);
    float hpRatio = s.maxHP > 0 ? (float)s.currentHP / s.maxHP : 0;
    ImGui::ProgressBar(hpRatio, ImVec2(-1, 14), "");

    ImGui::TextColored(ImVec4(0.3f, 0.5f, 1, 1), "MP: %d / %d", s.currentMP, s.maxMP);
    float mpRatio = s.maxMP > 0 ? (float)s.currentMP / s.maxMP : 0;
    ImGui::ProgressBar(mpRatio, ImVec2(-1, 14), "");

    // XP
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1), "XP: %lld / %lld",
                       (long long)s.currentXP, (long long)s.xpToNextLevel);
    float xpRatio = s.xpToNextLevel > 0 ? (float)((double)s.currentXP / s.xpToNextLevel) : 0;
    ImGui::ProgressBar(xpRatio, ImVec2(-1, 14), "");

    ImGui::Separator();

    // Primary stats
    ImGui::Columns(2);
    ImGui::Text("STR: %d", s.getStrength());
    ImGui::Text("VIT: %d", s.getVitality());
    ImGui::Text("INT: %d", s.getIntelligence());
    ImGui::NextColumn();
    ImGui::Text("DEX: %d", s.getDexterity());
    ImGui::Text("WIS: %d", s.getWisdom());
    ImGui::Columns(1);

    ImGui::Separator();

    // Derived stats
    ImGui::Text("Armor: %d", s.getArmor());
    ImGui::Text("Magic Resist: %d", s.getMagicResist());
    ImGui::Text("Hit Rate: %.0f", s.getHitRate());
    ImGui::Text("Evasion: %.0f", s.getEvasion());
    ImGui::Text("Crit: %.1f%%", s.getCritRate() * 100.0f);
    ImGui::Text("Speed: %.1f", s.getSpeed());

    // Fury / Honor
    if (s.classDef.usesFury()) {
        ImGui::Separator();
        int maxFury = s.classDef.getMaxFuryForLevel(s.level);
        ImGui::Text("Fury: %.0f / %d", s.currentFury, maxFury);
    }

    ImGui::Separator();
    ImGui::Text("Honor: %d", s.honor);
    ImGui::Text("PvP: %d kills / %d deaths", s.pvpKills, s.pvpDeaths);
}

// ============================================================================
// Skills Tab (placeholder)
// ============================================================================

void InventoryUI::drawSkillsTab(World* world, Entity* player) {
    auto* skillComp = player->getComponent<SkillManagerComponent>();
    if (!skillComp) {
        ImGui::Text("No skill manager");
        return;
    }

    auto& skills = skillComp->skills;

    // Skill points summary
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Skill Points: %d available",
                       skills.availablePoints());
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(%d earned, %d spent)",
                       skills.earnedPoints(), skills.spentPoints());
    ImGui::Separator();

    // Learned skills list
    auto& learned = skills.getLearnedSkills();
    if (learned.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "No skills learned yet.");
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1), "Use skillbooks to learn skills.");
        return;
    }

    ImGui::Text("Learned Skills:");
    ImGui::Spacing();

    for (auto& ls : learned) {
        ImGui::PushID(ls.skillId.c_str());

        // Skill row: name, rank, activate button
        bool onCD = skills.isOnCooldown(ls.skillId);
        ImVec4 nameColor = onCD ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f) : ImVec4(0.8f, 0.8f, 1.0f, 1.0f);

        ImGui::TextColored(nameColor, "%s", ls.skillId.c_str());
        ImGui::SameLine(180);

        // Rank display with color
        ImVec4 rankColor = (ls.activatedRank > 0) ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                                                   : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        ImGui::TextColored(rankColor, "%d/%d", ls.activatedRank, ls.unlockedRank);

        // Activate rank button (spend skill point)
        if (ls.activatedRank < ls.unlockedRank && skills.availablePoints() > 0) {
            ImGui::SameLine(240);
            char btnLabel[32];
            snprintf(btnLabel, sizeof(btnLabel), "+##act_%s", ls.skillId.c_str());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.4f, 0.15f, 1.0f));
            if (ImGui::SmallButton(btnLabel)) {
                skills.activateSkillRank(ls.skillId);
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Spend 1 skill point to activate rank %d", ls.activatedRank + 1);
            }
        }

        // Cooldown indicator
        if (onCD) {
            ImGui::SameLine(270);
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "CD:%.1fs",
                              skills.getRemainingCooldown(ls.skillId));
        }

        // Drag source: drag this skill to assign to skill bar
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("SKILL_ASSIGN", ls.skillId.c_str(), ls.skillId.size() + 1);
            ImGui::Text("Assign: %s", ls.skillId.c_str());
            ImGui::EndDragDropSource();
        }

        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1), "Drag skills to the Skill Bar ->");
}

// ============================================================================
// Helpers
// ============================================================================

ImVec4 InventoryUI::getRarityColor(ItemRarity rarity) {
    switch (rarity) {
        case ItemRarity::Common:    return {0.8f, 0.8f, 0.8f, 1.0f};
        case ItemRarity::Uncommon:  return {0.2f, 0.9f, 0.2f, 1.0f};
        case ItemRarity::Rare:      return {0.3f, 0.5f, 1.0f, 1.0f};
        case ItemRarity::Epic:      return {0.7f, 0.3f, 1.0f, 1.0f};
        case ItemRarity::Legendary: return {1.0f, 0.6f, 0.1f, 1.0f};
        case ItemRarity::Unique:    return {1.0f, 0.2f, 0.2f, 1.0f};
        default: return {0.5f, 0.5f, 0.5f, 1.0f};
    }
}

const char* InventoryUI::getRarityName(ItemRarity rarity) {
    switch (rarity) {
        case ItemRarity::Common:    return "Common";
        case ItemRarity::Uncommon:  return "Uncommon";
        case ItemRarity::Rare:      return "Rare";
        case ItemRarity::Epic:      return "Epic";
        case ItemRarity::Legendary: return "Legendary";
        case ItemRarity::Unique:    return "Unique";
        default: return "Unknown";
    }
}

const char* InventoryUI::getEquipSlotName(EquipmentSlot slot) {
    switch (slot) {
        case EquipmentSlot::Hat:       return "Hat";
        case EquipmentSlot::Armor:     return "Armor";
        case EquipmentSlot::Gloves:    return "Gloves";
        case EquipmentSlot::Shoes:     return "Shoes";
        case EquipmentSlot::Belt:      return "Belt";
        case EquipmentSlot::Cloak:     return "Cloak";
        case EquipmentSlot::Weapon:    return "Weapon";
        case EquipmentSlot::SubWeapon: return "Shield";
        case EquipmentSlot::Ring:      return "Ring";
        case EquipmentSlot::Necklace:  return "Necklace";
        default: return "None";
    }
}

std::string InventoryUI::formatGold(int64_t gold) {
    if (gold >= 1000000000LL) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fB", gold / 1000000000.0);
        return buf;
    }
    if (gold >= 1000000LL) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fM", gold / 1000000.0);
        return buf;
    }
    if (gold >= 10000LL) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fK", gold / 1000.0);
        return buf;
    }
    return std::to_string(gold);
}

} // namespace fate
