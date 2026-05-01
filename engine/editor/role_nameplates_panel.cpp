#include "engine/editor/editor.h"
#ifdef FATE_HAS_GAME
#include "game/systems/role_nameplate_config.h"
#endif // FATE_HAS_GAME
#include <imgui.h>
#include <cstdio>
#include <cstring>

namespace fate {

#ifdef FATE_HAS_GAME
void Editor::drawRoleNameplatesPanel() {
    if (!ImGui::Begin("Role Nameplates", &showRoleNameplatesPanel_)) {
        ImGui::End();
        return;
    }

    auto& cfg = RoleNameplateConfig::instance();
    bool dirty = false;

    auto renderRole = [&](const char* label, RoleTier tier) {
        if (!ImGui::CollapsingHeader(label)) return;
        auto& e = cfg.getMut(tier);
        ImGui::PushID(label);

        dirty |= ImGui::Checkbox("Visible", &e.visible);

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s", e.tagText.c_str());
        if (ImGui::InputText("Tag Text", buf, sizeof(buf))) {
            e.tagText = buf;
            dirty = true;
        }

        dirty |= ImGui::ColorEdit4("Tag Color",       &e.tagColor.r);
        dirty |= ImGui::DragFloat ("Font Size",       &e.fontSize, 0.5f, 6.0f, 64.0f);
        dirty |= ImGui::DragFloat ("Gap (px)",        &e.gapPx, 0.5f, 0.0f, 64.0f);
        dirty |= ImGui::Checkbox  ("Bold",            &e.bold);
        dirty |= ImGui::Checkbox  ("Italic",          &e.italic);
        dirty |= ImGui::ColorEdit4("Outline Color",   &e.outlineColor.r);
        dirty |= ImGui::DragFloat ("Outline Thick",   &e.outlineThickness, 0.1f, 0.0f, 8.0f);
        dirty |= ImGui::Checkbox  ("Pill Enabled",    &e.pillEnabled);
        ImGui::SameLine();
        ImGui::TextDisabled("(or set Pill Color alpha to 0)");
        dirty |= ImGui::ColorEdit4("Pill Color",      &e.pillColor.r);
        dirty |= ImGui::DragFloat ("Pill Padding X",  &e.pillPaddingX, 0.5f, 0.0f, 32.0f);
        dirty |= ImGui::DragFloat ("Pill Padding Y",  &e.pillPaddingY, 0.5f, 0.0f, 32.0f);
        dirty |= ImGui::DragFloat ("Pill Radius",     &e.pillRadius,   0.5f, 0.0f, 32.0f);
        dirty |= ImGui::DragFloat ("Offset X",        &e.offsetX, 0.5f, -200.0f, 200.0f);
        dirty |= ImGui::DragFloat ("Offset Y",        &e.offsetY, 0.5f, -200.0f, 200.0f);

        ImGui::PopID();
    };

    renderRole("Player", RoleTier::Player);
    renderRole("GM",     RoleTier::GM);
    renderRole("Admin",  RoleTier::Admin);

    ImGui::Separator();
    // Debounce disk writes: drag-style widgets (DragFloat, ColorEdit) return
    // dirty every frame the slider is held, which would spam the log + IO.
    // Defer the save until the user releases all widgets (no item active).
    static bool pendingSave = false;

    if (ImGui::Button("Save now")) {
        cfg.save(RoleNameplateConfig::kDefaultPath);
        pendingSave = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload from disk")) {
        cfg.load(RoleNameplateConfig::kDefaultPath);
        pendingSave = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) {
        cfg.loadDefaults();
        dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(pendingSave ? "(unsaved)" : "(saved)");
    ImGui::TextDisabled("Auto-saves on release of any slider.");

    if (dirty) pendingSave = true;
    if (pendingSave && !ImGui::IsAnyItemActive()) {
        cfg.save(RoleNameplateConfig::kDefaultPath);
        pendingSave = false;
    }

    ImGui::End();
}
#else // !FATE_HAS_GAME
// Demo build: game/ sources are absent, so the role-nameplate config singleton
// isn't linked. Provide an empty stub so editor.cpp's unconditional dispatch
// at editor.cpp:~745 still resolves.
void Editor::drawRoleNameplatesPanel() {}
#endif // FATE_HAS_GAME

} // namespace fate
