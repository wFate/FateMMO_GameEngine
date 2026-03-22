#include "engine/editor/log_viewer.h"
#include "imgui.h"
#include <algorithm>

namespace fate {

void LogViewer::draw() {
    ImGui::SetNextWindowSize(ImVec2(500, 200), ImGuiCond_FirstUseEver);

    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Log", nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }

    // Filter bar
    if (ImGui::Button("Clear")) clear();
    ImGui::SameLine();
    ImGui::Checkbox("DBG", &showDebug_);
    ImGui::SameLine();
    ImGui::Checkbox("INF", &showInfo_);
    ImGui::SameLine();
    ImGui::Checkbox("WRN", &showWarn_);
    ImGui::SameLine();
    ImGui::Checkbox("ERR", &showError_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::InputText("Filter", filterBuf_, sizeof(filterBuf_));

    ImGui::Separator();

    // Scrollable log area
    ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    std::string filter(filterBuf_);
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    // Lock mutex before iterating entries_ to prevent data races with
    // concurrent writes from other threads (e.g. logging from network thread).
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : entries_) {
        // Level filter
        if (entry.level == 0 && !showDebug_) continue;
        if (entry.level == 1 && !showInfo_) continue;
        if (entry.level == 2 && !showWarn_) continue;
        if (entry.level >= 3 && !showError_) continue;

        // Text filter
        if (!filter.empty()) {
            std::string lower = entry.message;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find(filter) == std::string::npos) continue;
        }

        // Color by level
        ImVec4 color;
        switch (entry.level) {
            case 0: color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); break; // debug gray
            case 1: color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); break; // info white
            case 2: color = ImVec4(1.0f, 0.9f, 0.3f, 1.0f); break; // warn yellow
            case 3: color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); break; // error red
            default: color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); break; // fatal
        }

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(entry.message.c_str());
        ImGui::PopStyleColor();
    }

    if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    autoScroll_ = false;

    ImGui::EndChild();
    ImGui::End();
}

} // namespace fate
