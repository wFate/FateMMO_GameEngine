// engine/editor/editor_hot_reload_panel.cpp
//
// "Hot Reload" Window menu panel. The primary save→build→reload loop is
// driven by source-watch + automatic post-build reload-request — this panel
// surfaces the state of that pipeline and exposes "Build Runtime Now" /
// "Reload Now" buttons as manual fallbacks. Stays compiled in non-shipping
// builds — shipping strips the editor entirely.

#include "engine/editor/editor.h"
#include "engine/module/hot_reload_manager.h"
#include "engine/module/behavior_registry.h"

#include "imgui.h"

namespace fate {

namespace {

// Color helpers — keep status legible against ImGui's default dark theme.
constexpr ImVec4 kGreen   = ImVec4(0.40f, 0.95f, 0.40f, 1.00f);
constexpr ImVec4 kAmber   = ImVec4(0.95f, 0.85f, 0.40f, 1.00f);
constexpr ImVec4 kRed     = ImVec4(0.95f, 0.40f, 0.40f, 1.00f);
constexpr ImVec4 kOrange  = ImVec4(0.95f, 0.55f, 0.40f, 1.00f);
constexpr ImVec4 kGrey    = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);

const char* buildStatusText(HotReloadManager::BuildStatus s) {
    switch (s) {
        case HotReloadManager::BuildStatus::Idle:      return "Idle";
        case HotReloadManager::BuildStatus::Running:   return "Running";
        case HotReloadManager::BuildStatus::Succeeded: return "Succeeded";
        case HotReloadManager::BuildStatus::Failed:    return "Failed";
    }
    return "?";
}

ImVec4 buildStatusColor(HotReloadManager::BuildStatus s) {
    switch (s) {
        case HotReloadManager::BuildStatus::Idle:      return kGrey;
        case HotReloadManager::BuildStatus::Running:   return kAmber;
        case HotReloadManager::BuildStatus::Succeeded: return kGreen;
        case HotReloadManager::BuildStatus::Failed:    return kRed;
    }
    return kGrey;
}

} // namespace

void Editor::drawHotReloadPanel() {
    if (!showHotReloadPanel_) return;

    auto& mgr = HotReloadManager::instance();
    auto& reg = BehaviorRegistry::instance();

    ImGui::SetNextWindowSize(ImVec2(480, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Hot Reload", &showHotReloadPanel_)) {

        // -- Pipeline status (the primary save→build→reload loop) ----------
        ImGui::TextUnformatted("Pipeline");
        ImGui::Separator();

        // Source watch.
        if (mgr.sourceWatchEnabled()) {
            ImGui::TextColored(kGreen, "Source watch: ON");
            ImGui::TextWrapped("Watching: %s", mgr.sourceWatchPath().c_str());
            ImGui::TextWrapped("Build cmd: %s", mgr.buildCommand().c_str());
        } else {
            ImGui::TextColored(kGrey, "Source watch: OFF");
            ImGui::TextWrapped(
                "Set FATE_HOTRELOAD_SOURCE_DIR + FATE_HOTRELOAD_BUILD_CMD, or "
                "rebuild from a checkout where game/runtime + "
                "scripts/check_shipping.ps1 exist.");
        }

        // Build status.
        const auto status = mgr.buildStatus();
        ImGui::TextColored(buildStatusColor(status),
            "Build status: %s (last rc=%d)", buildStatusText(status),
            mgr.buildExitCode());

        // Build log tail — surface on Failed always, on Running for live
        // visibility, hidden on Idle/Succeeded so the panel isn't dominated
        // by stale output.
        if (status == HotReloadManager::BuildStatus::Failed ||
            status == HotReloadManager::BuildStatus::Running) {
            const std::string tail = mgr.buildLogTailSnapshot();
            if (!tail.empty()) {
                ImGui::BeginChild("##buildlog", ImVec2(0, 110), true);
                ImGui::TextUnformatted(tail.c_str());
                ImGui::EndChild();
            }
        }

        // Reload-queue state. Distinct from build state — a successful
        // build queues reload, debounce + safe-frame gate decide when it
        // commits, play-mode defers it. The panel must say which.
        if (mgr.isReloadDeferredByPlayMode()) {
            ImGui::TextColored(kOrange,
                "Reload: QUEUED (waiting for play mode to exit)");
        } else if (mgr.isReloadPending()) {
            ImGui::TextColored(kAmber, "Reload: PENDING (debouncing)");
        } else {
            ImGui::TextColored(kGrey, "Reload: idle");
        }

        ImGui::Spacing();

        // Manual triggers — fallbacks. Primary path is save-the-source-
        // file automation, but a "Build Runtime Now" button is useful for
        // post-CMake-change rebuilds and for verifying a clean cycle.
        const bool buildBusy =
            (status == HotReloadManager::BuildStatus::Running);
        const bool canBuild = mgr.sourceWatchEnabled();
        ImGui::BeginDisabled(!canBuild || buildBusy);
        if (ImGui::Button("Build Runtime Now")) {
            mgr.requestBuildNow("editor button");
        }
        ImGui::EndDisabled();
        if (!canBuild) {
            ImGui::SameLine();
            ImGui::TextDisabled("(needs source watch)");
        } else if (buildBusy) {
            ImGui::SameLine();
            ImGui::TextDisabled("(running)");
        }

        ImGui::SameLine();
        if (ImGui::Button("Reload Now")) {
            mgr.requestManualReload("editor button");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(fallback)");

        ImGui::Separator();
        ImGui::Spacing();

        // -- Module identity ------------------------------------------------
        ImGui::TextUnformatted("Module");
        ImGui::Separator();
        if (mgr.isModuleLoaded()) {
            ImGui::TextColored(kGreen, "%s", mgr.moduleName().c_str());
            ImGui::Text("Build id: %s", mgr.moduleBuildId().c_str());
            ImGui::Text("Generation: %u (failures: %u)",
                        mgr.reloadCount(), mgr.failureCount());
        } else {
            ImGui::TextColored(kOrange, "(not loaded)");
            ImGui::TextWrapped("Watching artifact: %s", mgr.sourcePath().c_str());
            ImGui::Text("Generation: %u (failures: %u)",
                        mgr.reloadCount(), mgr.failureCount());
        }

        // Last error (any pipeline phase: copy, LoadLibrary, ABI check, init).
        const auto& err = mgr.lastError();
        if (!err.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(kRed, "Last error:");
            ImGui::TextWrapped("%s", err.c_str());
        }

        // Play-mode reload posture. Read-only — flipping it requires the
        // FATE_HOTRELOAD_EXPERIMENTAL_PLAYMODE compile flag because combat,
        // AOI, and network packet dispatch are not yet quiesced at the
        // reload safe-frame point.
        ImGui::Spacing();
        if (mgr.playModeReloadAllowed()) {
            ImGui::TextColored(kOrange,
                "Play-mode reload: ENABLED (experimental build)");
        } else {
            ImGui::TextColored(kGrey,
                "Play-mode reload: DISABLED (recompile w/ "
                "FATE_HOTRELOAD_EXPERIMENTAL_PLAYMODE=1 to override)");
        }

        // Module-degraded banner. Set when a module lifecycle callback
        // (Init/BeginReload/EndReload/Shutdown) faulted under SEH or threw
        // a C++ exception. Module is still live but its handshake plumbing
        // is unreliable — surface visibly.
        if (mgr.moduleDegraded()) {
            ImGui::Spacing();
            ImGui::TextColored(kOrange, "Module DEGRADED:");
            ImGui::TextWrapped("%s", mgr.moduleDegradedReason().c_str());
            if (ImGui::SmallButton("Clear degraded flag")) {
                mgr.clearModuleDegraded();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        // -- Faulted instances (per-behavior quarantine) --------------------
        const auto faulted = mgr.faultedBehaviors();
        if (!faulted.empty()) {
            ImGui::TextColored(kRed, "Faulted instances (%d):",
                               (int)faulted.size());
            ImGui::BeginChild("##faulted", ImVec2(0, 90), true);
            for (const auto& row : faulted) {
                ImGui::BulletText("entity=%u behavior='%s'",
                                  row.entityId, row.behaviorName.c_str());
                ImGui::TextWrapped("  %s", row.detail.c_str());
            }
            ImGui::EndChild();
            if (ImGui::Button("Re-arm all##rearm")) {
                mgr.clearAllFaults();
            }
            ImGui::Separator();
        }

        // -- Registered behaviors ------------------------------------------
        ImGui::Text("Registered behaviors (gen %u):", reg.generation());
        ImGui::BeginChild("##behaviors", ImVec2(0, 130), true);
        bool any = false;
        reg.forEach([&](const std::string& name, const FateBehaviorVTable&) {
            ImGui::BulletText("%s", name.c_str());
            any = true;
        });
        if (!any) {
            ImGui::TextDisabled(
                "(none — module is not loaded or registered nothing)");
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace fate
