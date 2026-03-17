#if defined(ENGINE_MEMORY_DEBUG)

#include "engine/editor/memory_panel.h"
#include "engine/memory/allocator_registry.h"
#include "engine/memory/arena.h"
#include <imgui.h>
#include <implot.h>
#include <cstdio>
#include <algorithm>

namespace fate {

// Ring buffer for frame arena timeline
static constexpr int kTimelineSamples = 300;
static float s_timelineBuf[kTimelineSamples] = {};
static int s_timelineHead = 0;
static float s_highWaterMark = 0.0f;

static const char* formatBytes(size_t bytes, char* buf, size_t bufSize) {
    if (bytes >= 1024 * 1024)
        snprintf(buf, bufSize, "%.1f MB", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        snprintf(buf, bufSize, "%.1f KB", bytes / 1024.0);
    else
        snprintf(buf, bufSize, "%zu B", bytes);
    return buf;
}

static void drawArenaTab() {
    const auto& allocators = AllocatorRegistry::instance().all();
    for (const auto& a : allocators) {
        if (a.type == AllocatorType::Pool) continue;
        if (!a.getUsed || !a.getReserved) continue;

        size_t used = a.getUsed();
        size_t reserved = a.getReserved();
        size_t committed = a.getCommitted ? a.getCommitted() : 0;
        float ratio = reserved > 0 ? static_cast<float>(used) / reserved : 0.0f;

        // Color-coded progress bar
        ImVec4 color;
        if (ratio < 0.7f)      color = ImVec4(0.2f, 0.8f, 0.3f, 1.0f); // green
        else if (ratio < 0.9f) color = ImVec4(0.9f, 0.8f, 0.1f, 1.0f); // yellow
        else                   color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f); // red

        char usedBuf[32], reservedBuf[32];
        formatBytes(used, usedBuf, sizeof(usedBuf));
        formatBytes(reserved, reservedBuf, sizeof(reservedBuf));

        char overlay[128];
        snprintf(overlay, sizeof(overlay), "%s: %s / %s (%.1f%%)",
                 a.name, usedBuf, reservedBuf, ratio * 100.0f);

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
        ImGui::ProgressBar(ratio, ImVec2(-1, 0), overlay);
        ImGui::PopStyleColor();

        // Committed bar (dimmer)
        if (committed > 0 && reserved > 0) {
            float commitRatio = static_cast<float>(committed) / reserved;
            char commitBuf[32];
            formatBytes(committed, commitBuf, sizeof(commitBuf));
            char commitOverlay[128];
            snprintf(commitOverlay, sizeof(commitOverlay), "  Committed: %s", commitBuf);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.4f, 0.4f, 0.5f, 0.6f));
            ImGui::ProgressBar(commitRatio, ImVec2(-1, 0), commitOverlay);
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
    }
}

static void drawPoolTab() {
    const auto& allocators = AllocatorRegistry::instance().all();
    for (const auto& a : allocators) {
        if (a.type != AllocatorType::Pool) continue;
        if (!a.getActiveBlocks || !a.getTotalBlocks) continue;

        size_t active = a.getActiveBlocks();
        size_t total = a.getTotalBlocks();

        char summary[128];
        snprintf(summary, sizeof(summary), "%s: %zu/%zu active (%.1f%%)",
                 a.name, active, total,
                 total > 0 ? (active * 100.0 / total) : 0.0);
        ImGui::Text("%s", summary);

        // Heat map grid
        if (a.getOccupancyBitmap) {
            const uint8_t* bitmap = a.getOccupancyBitmap();
            if (bitmap) {
                ImDrawList* draw = ImGui::GetWindowDrawList();
                ImVec2 cursor = ImGui::GetCursorScreenPos();
                float cellSize = total > 1024 ? 3.0f : 6.0f;
                int cols = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (cellSize + 1)));

                for (size_t i = 0; i < total; ++i) {
                    int col = static_cast<int>(i % cols);
                    int row = static_cast<int>(i / cols);
                    ImVec2 p0(cursor.x + col * (cellSize + 1), cursor.y + row * (cellSize + 1));
                    ImVec2 p1(p0.x + cellSize, p0.y + cellSize);

                    bool occupied = (bitmap[i / 8] & (1 << (i % 8))) != 0;
                    ImU32 c = occupied ? IM_COL32(220, 50, 50, 255) : IM_COL32(80, 80, 80, 255);
                    draw->AddRectFilled(p0, p1, c);

                    if (ImGui::IsMouseHoveringRect(p0, p1)) {
                        ImGui::SetTooltip("Block %zu: %s", i, occupied ? "occupied" : "free");
                    }
                }

                int rows = static_cast<int>((total + cols - 1) / cols);
                ImGui::Dummy(ImVec2(0, rows * (cellSize + 1) + 4));
            }
        }

        ImGui::Separator();
    }
}

static void drawTimelineTab(FrameArena* frameArena) {
    if (!frameArena) {
        ImGui::Text("No FrameArena available");
        return;
    }

    // Sample current frame's allocation
    float used = static_cast<float>(frameArena->current().position());
    s_timelineBuf[s_timelineHead] = used;
    s_timelineHead = (s_timelineHead + 1) % kTimelineSamples;
    s_highWaterMark = std::max(s_highWaterMark, used);

    if (ImPlot::BeginPlot("Frame Arena Usage", ImVec2(-1, 200))) {
        ImPlot::SetupAxes("Frame", "Bytes", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

        // Build ordered array from ring buffer
        float ordered[kTimelineSamples];
        for (int i = 0; i < kTimelineSamples; ++i) {
            ordered[i] = s_timelineBuf[(s_timelineHead + i) % kTimelineSamples];
        }

        ImPlot::PlotLine("Usage", ordered, kTimelineSamples);

        // High water mark line
        float hwm[kTimelineSamples];
        for (int i = 0; i < kTimelineSamples; ++i) hwm[i] = s_highWaterMark;
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1, 0.3f, 0.3f, 0.7f));
        ImPlot::PlotLine("High Water", hwm, kTimelineSamples);
        ImPlot::PopStyleColor();

        ImPlot::EndPlot();
    }

    char hwmBuf[32];
    formatBytes(static_cast<size_t>(s_highWaterMark), hwmBuf, sizeof(hwmBuf));
    ImGui::Text("High Water Mark: %s", hwmBuf);

    if (ImGui::Button("Reset High Water Mark")) {
        s_highWaterMark = 0.0f;
    }
}

void drawMemoryPanel(bool* open, FrameArena* frameArena) {
    if (!*open) return;

    if (ImGui::Begin("Memory", open)) {
        if (ImGui::BeginTabBar("MemoryTabs")) {
            if (ImGui::BeginTabItem("Arenas")) {
                drawArenaTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Pools")) {
                drawPoolTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Frame Timeline")) {
                drawTimelineTab(frameArena);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace fate

#endif // ENGINE_MEMORY_DEBUG
