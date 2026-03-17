#include "game/ui/bank_storage_ui.h"
#include "engine/ecs/world.h"
#include "game/components/game_components.h"
#include "engine/core/logger.h"
#include "imgui.h"
#include <cstdio>
#include <cmath>

namespace fate {

// ============================================================================
// Open / Close
// ============================================================================

void BankStorageUI::open(Entity* npc, Entity* /*player*/) {
    if (!npc) return;
    bankerNPC_ = npc;
    isOpen = true;
    goldInputAmount_ = 0;
    LOG_INFO("BankStorageUI", "Opened bank storage");
}

void BankStorageUI::close() {
    isOpen = false;
    bankerNPC_ = nullptr;
    goldInputAmount_ = 0;
}

// ============================================================================
// Render
// ============================================================================

void BankStorageUI::render(Entity* player) {
    if (!isOpen || !player || !bankerNPC_) return;

    auto* bankerComp = bankerNPC_->getComponent<BankerComponent>();
    auto* bankComp = player->getComponent<BankStorageComponent>();
    auto* invComp = player->getComponent<InventoryComponent>();
    if (!bankerComp || !bankComp || !invComp) return;

    auto& bank = bankComp->storage;
    auto& inv = invComp->inventory;
    float feePercent = bankerComp->depositFeePercent;

    // Window setup
    ImGuiIO& io = ImGui::GetIO();
    float panelW = 420.0f;
    float panelH = 480.0f;
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - panelW) * 0.5f,
                                    (io.DisplaySize.y - panelH) * 0.5f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 0.95f));

    bool open = isOpen;
    if (!ImGui::Begin("Bank Storage###BankStorage", &open, ImGuiWindowFlags_NoCollapse)) {
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

    // Tab bar: Items / Gold
    if (ImGui::BeginTabBar("BankTabs")) {
        // ----------------------------------------------------------------
        // Items Tab
        // ----------------------------------------------------------------
        if (ImGui::BeginTabItem("Items")) {
            // Bank items grid
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Bank Storage:");
            ImGui::BeginChild("BankItems", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.5f - 20), true);

            auto& bankItems = bank.getItems();
            if (bankItems.empty()) {
                ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "Bank is empty.");
            }

            for (int i = 0; i < (int)bankItems.size(); ++i) {
                auto& item = bankItems[i];
                ImGui::PushID(i);

                char label[128];
                snprintf(label, sizeof(label), "%s  x%d", item.itemId.c_str(), item.count);
                ImGui::Text("%s", label);

                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70);
                if (ImGui::SmallButton("Withdraw")) {
                    if (bank.withdrawItem(item.itemId, 1)) {
                        // Placeholder: add to player inventory
                        LOG_INFO("BankUI", "Withdrew 1x %s", item.itemId.c_str());
                    }
                }

                ImGui::PopID();
            }

            ImGui::EndChild();

            // Player inventory (for depositing)
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Your Inventory:");
            ImGui::BeginChild("PlayerItems", ImVec2(0, 0), true);

            for (int i = 0; i < inv.totalSlots(); ++i) {
                ItemInstance item = inv.getSlot(i);
                if (!item.isValid()) continue;

                ImGui::PushID(i + 5000);

                char label[128];
                snprintf(label, sizeof(label), "[%d] %s  x%d",
                         i, item.itemId.c_str(), item.quantity);
                ImGui::Text("%s", label);

                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
                if (ImGui::SmallButton("Deposit")) {
                    if (bank.depositItem(item.itemId, 1)) {
                        // Placeholder: remove from player inventory
                        LOG_INFO("BankUI", "Deposited 1x %s", item.itemId.c_str());
                    }
                }

                ImGui::PopID();
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // ----------------------------------------------------------------
        // Gold Tab
        // ----------------------------------------------------------------
        if (ImGui::BeginTabItem("Gold")) {
            ImGui::Spacing();

            // Current balances
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Your Gold: %s",
                               formatGold(inv.getGold()).c_str());
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Bank Gold: %s",
                               formatGold(bank.getStoredGold()).c_str());

            if (feePercent > 0.0f) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                                   "Deposit fee: %.1f%%", feePercent * 100.0f);
            }

            ImGui::Separator();
            ImGui::Spacing();

            // Gold amount input
            ImGui::SetNextItemWidth(200);
            ImGui::InputInt("Amount", &goldInputAmount_, 100, 1000);
            if (goldInputAmount_ < 0) goldInputAmount_ = 0;

            ImGui::Spacing();

            // Deposit button with fee preview
            float buttonW = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;

            {
                int64_t fee = static_cast<int64_t>(std::floor(goldInputAmount_ * feePercent));
                int64_t deposited = goldInputAmount_ - fee;
                bool canDeposit = goldInputAmount_ > 0 && inv.getGold() >= goldInputAmount_;

                if (!canDeposit) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.4f, 0.15f, 1.0f));
                }

                char depositLabel[64];
                if (fee > 0) {
                    snprintf(depositLabel, sizeof(depositLabel), "Deposit (%s net)",
                             formatGold(deposited).c_str());
                } else {
                    snprintf(depositLabel, sizeof(depositLabel), "Deposit");
                }

                if (ImGui::Button(depositLabel, ImVec2(buttonW, 0)) && canDeposit) {
                    if (inv.removeGold(goldInputAmount_)) {
                        bank.depositGold(goldInputAmount_, feePercent);
                        LOG_INFO("BankUI", "Deposited %d gold (fee: %lld)",
                                 goldInputAmount_, (long long)fee);
                    }
                }
                ImGui::PopStyleColor();
            }

            ImGui::SameLine(0, 8.0f);

            // Withdraw button
            {
                bool canWithdraw = goldInputAmount_ > 0 &&
                                   bank.getStoredGold() >= goldInputAmount_;

                if (!canWithdraw) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.3f, 0.15f, 1.0f));
                }

                if (ImGui::Button("Withdraw", ImVec2(buttonW, 0)) && canWithdraw) {
                    if (bank.withdrawGold(goldInputAmount_)) {
                        inv.addGold(goldInputAmount_);
                        LOG_INFO("BankUI", "Withdrew %d gold", goldInputAmount_);
                    }
                }
                ImGui::PopStyleColor();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ============================================================================
// Helpers
// ============================================================================

std::string BankStorageUI::formatGold(int64_t gold) {
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
