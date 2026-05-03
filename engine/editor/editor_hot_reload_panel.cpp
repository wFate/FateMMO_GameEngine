// engine/editor/editor_hot_reload_panel.cpp
//
// "Hot Reload" Window menu panel. Displays the live FateGameRuntime module's
// build identity, last reload error, reload counters, and the registered
// behavior names. Provides a Reload Now trigger and a play-mode-reload
// override. Stays compiled in non-shipping builds — shipping strips editor.

#include "engine/editor/editor.h"
#include "engine/module/hot_reload_manager.h"
#include "engine/module/behavior_registry.h"

#include "imgui.h"

namespace fate {

void Editor::drawHotReloadPanel() {
    if (!showHotReloadPanel_) return;

    auto& mgr = HotReloadManager::instance();
    auto& reg = BehaviorRegistry::instance();

    ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Hot Reload", &showHotReloadPanel_)) {

        // Module identity
        if (mgr.isModuleLoaded()) {
            ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.4f, 1.0f), "Module: %s", mgr.moduleName().c_str());
            ImGui::Text("Build:  %s", mgr.moduleBuildId().c_str());
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.4f, 1.0f), "Module: (not loaded)");
            ImGui::TextWrapped("Watching: %s", mgr.sourcePath().c_str());
        }

        ImGui::Separator();

        // Counters
        ImGui::Text("Reloads:  %u", mgr.reloadCount());
        ImGui::SameLine(160);
        ImGui::Text("Failures: %u", mgr.failureCount());

        // Last error
        const auto& err = mgr.lastError();
        if (!err.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "Last error:");
            ImGui::TextWrapped("%s", err.c_str());
        }

        ImGui::Separator();

        // Manual reload trigger.
        if (ImGui::Button("Reload Now")) {
            mgr.requestManualReload("editor button");
        }

        // Play-mode reload status. READ-ONLY in this build — flipping the
        // toggle requires a compile-time opt-in (FATE_HOTRELOAD_EXPERIMENTAL_
        // PLAYMODE=1). Removed the user-facing checkbox to prevent the
        // foot-gun: combat, AOI, and network packet dispatch are not yet
        // quiesced at the reload safe-frame point during play mode.
        ImGui::SameLine();
        if (mgr.playModeReloadAllowed()) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f),
                "Play-mode reload: ENABLED (experimental build)");
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "Play-mode reload: deferred (recompile w/ FATE_HOTRELOAD_EXPERIMENTAL_PLAYMODE=1 to override)");
        }

        ImGui::Separator();

        // Source-side build status (only meaningful when source-watch is on).
        const std::string& cmd = mgr.buildCommand();
        if (!cmd.empty()) {
            ImGui::Text("Build cmd: %s", cmd.c_str());
            const auto status = mgr.buildStatus();
            switch (status) {
                case HotReloadManager::BuildStatus::Idle:
                    ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "Build status: idle");
                    break;
                case HotReloadManager::BuildStatus::Running:
                    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.4f, 1.0f), "Build status: RUNNING...");
                    break;
                case HotReloadManager::BuildStatus::Succeeded:
                    ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.4f, 1.0f), "Build status: succeeded (rc=%d)",
                                       mgr.buildExitCode());
                    break;
                case HotReloadManager::BuildStatus::Failed: {
                    ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "Build status: FAILED (rc=%d)",
                                       mgr.buildExitCode());
                    // Snapshot under mutex so the panel never reads a string
                    // the worker thread is mid-reallocating.
                    const std::string tail = mgr.buildLogTailSnapshot();
                    if (!tail.empty()) {
                        ImGui::BeginChild("##buildlog", ImVec2(0, 110), true);
                        ImGui::TextUnformatted(tail.c_str());
                        ImGui::EndChild();
                    }
                    break;
                }
            }
        } else {
            ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f),
                "Source watch: off (set FATE_HOTRELOAD_SOURCE_DIR + FATE_HOTRELOAD_BUILD_CMD).");
        }

        ImGui::Separator();

        // Registered behaviors
        ImGui::Text("Registered behaviors (gen %u):", reg.generation());
        ImGui::BeginChild("##behaviors", ImVec2(0, 140), true);
        bool any = false;
        reg.forEach([&](const std::string& name, const FateBehaviorVTable&) {
            ImGui::BulletText("%s", name.c_str());
            any = true;
        });
        if (!any) ImGui::TextDisabled("(none — module is not loaded or registered nothing)");
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace fate
