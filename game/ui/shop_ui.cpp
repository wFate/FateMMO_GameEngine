#include "game/ui/shop_ui.h"
#include "game/ui/game_viewport.h"
#include "game/components/game_components.h"
#include "engine/core/logger.h"
#include "imgui.h"
#include <cstdio>

namespace fate {

// ============================================================================
// Open / Close
// ============================================================================

void ShopUI::open(Entity* npc) {
    if (!npc) return;
    shopNPC = npc;
    isOpen = true;
    selectedBuyIndex_ = -1;
    selectedSellSlot_ = -1;
    LOG_INFO("ShopUI", "Opened shop for NPC");
}

void ShopUI::close() {
    isOpen = false;
    shopNPC = nullptr;
    selectedBuyIndex_ = -1;
    selectedSellSlot_ = -1;
}

// ============================================================================
// Render
// ============================================================================

void ShopUI::render(Entity* player) {
    if (!isOpen || !shopNPC || !player) return;

    auto* shopComp = shopNPC->getComponent<ShopComponent>();
    auto* invComp = player->getComponent<InventoryComponent>();
    if (!shopComp || !invComp) return;

    Inventory& inv = invComp->inventory;

    // Window setup — centered, styled to match existing UI
    float panelW = 420.0f;
    float panelH = 480.0f;
    ImGui::SetNextWindowPos(ImVec2(GameViewport::centerX() - panelW * 0.5f,
                                    GameViewport::centerY() - panelH * 0.5f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 0.95f));

    char title[128];
    snprintf(title, sizeof(title), "%s###ShopUI", shopComp->shopName.c_str());

    bool open = isOpen;
    if (!ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (!open) close();
        return;
    }

    if (!open) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        close();
        return;
    }

    // Tab bar: Buy / Sell
    if (ImGui::BeginTabBar("ShopTabs")) {
        // ----------------------------------------------------------------
        // Buy Tab
        // ----------------------------------------------------------------
        if (ImGui::BeginTabItem("Buy")) {
            ImGui::BeginChild("BuyList", ImVec2(0, -40), true);

            auto& items = shopComp->inventory;
            for (int i = 0; i < (int)items.size(); ++i) {
                auto& shopItem = items[i];
                ImGui::PushID(i);

                bool selected = (selectedBuyIndex_ == i);
                char label[256];
                if (shopItem.stock > 0) {
                    snprintf(label, sizeof(label), "%-20s  %s gold  [%d in stock]",
                             shopItem.itemName.c_str(),
                             formatGold(shopItem.buyPrice).c_str(),
                             shopItem.stock);
                } else {
                    snprintf(label, sizeof(label), "%-20s  %s gold",
                             shopItem.itemName.c_str(),
                             formatGold(shopItem.buyPrice).c_str());
                }

                if (ImGui::Selectable(label, selected)) {
                    selectedBuyIndex_ = i;
                }

                // Buy button on same line
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50);
                bool canAfford = inv.getGold() >= shopItem.buyPrice;
                bool inStock = (shopItem.stock == 0 || shopItem.stock > 0);

                if (!canAfford) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
                }

                if (ImGui::SmallButton("Buy") && canAfford && inStock) {
                    if (inv.removeGold(shopItem.buyPrice)) {
                        // Placeholder: add item to player inventory
                        // TODO: inv.addItem(ItemInstance from shopItem.itemId)
                        if (shopItem.stock > 0) shopItem.stock--;
                        LOG_INFO("ShopUI", "Bought %s for %lld gold",
                                 shopItem.itemName.c_str(), (long long)shopItem.buyPrice);
                    }
                }

                if (!canAfford) {
                    ImGui::PopStyleColor(2);
                }

                ImGui::PopID();
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // ----------------------------------------------------------------
        // Sell Tab
        // ----------------------------------------------------------------
        if (ImGui::BeginTabItem("Sell")) {
            ImGui::BeginChild("SellList", ImVec2(0, -40), true);

            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Your Inventory:");
            ImGui::Separator();

            for (int i = 0; i < inv.totalSlots(); ++i) {
                ItemInstance item = inv.getSlot(i);
                if (!item.isValid()) continue;

                ImGui::PushID(i + 10000);

                bool selected = (selectedSellSlot_ == i);
                char label[256];
                // Placeholder sell price — in a real system, look up from item definition
                snprintf(label, sizeof(label), "[%d] %s  x%d",
                         i, item.itemId.c_str(), item.quantity);

                if (ImGui::Selectable(label, selected)) {
                    selectedSellSlot_ = i;
                }

                // Sell button
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50);
                if (ImGui::SmallButton("Sell")) {
                    // Placeholder: calculate sell price from item definition
                    // For now, just remove the item
                    LOG_INFO("ShopUI", "Sold item from slot %d: %s",
                             i, item.itemId.c_str());
                    inv.removeItem(i);
                }

                ImGui::PopID();
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // Gold display at bottom
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Gold: %s",
                       formatGold(inv.getGold()).c_str());

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ============================================================================
// Helpers
// ============================================================================

std::string ShopUI::formatGold(int64_t gold) {
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
