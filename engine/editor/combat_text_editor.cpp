#include "engine/editor/combat_text_editor.h"
#ifdef FATE_HAS_GAME
#include "game/systems/combat_text_config.h"
#endif // FATE_HAS_GAME
#include "imgui.h"
#include <cstring>

namespace fate {

#ifdef FATE_HAS_GAME
static void drawStyleEditor(const char* label, CombatTextStyle& s) {
    if (!ImGui::CollapsingHeader(label)) return;
    ImGui::PushID(label);
    ImGui::Indent(8.0f);

    // Core
    char textBuf[128];
    std::strncpy(textBuf, s.text.c_str(), sizeof(textBuf) - 1);
    textBuf[sizeof(textBuf) - 1] = '\0';
    if (ImGui::InputText("Text", textBuf, sizeof(textBuf))) {
        s.text = textBuf;
    }
    ImGui::ColorEdit4("Color", &s.color.r);
    ImGui::ColorEdit4("Outline Color", &s.outlineColor.r);
    ImGui::DragFloat("Font Size", &s.fontSize, 0.5f, 4.0f, 60.0f);
    ImGui::DragFloat("Scale", &s.scale, 0.05f, 0.1f, 5.0f);

    ImGui::Separator();

    // Motion
    ImGui::DragFloat("Lifetime (s)", &s.lifetime, 0.05f, 0.1f, 10.0f);
    ImGui::DragFloat("Float Speed (px/s)", &s.floatSpeed, 1.0f, 0.0f, 200.0f);
    ImGui::DragFloat("Float Angle (deg)", &s.floatAngle, 1.0f, 0.0f, 360.0f);
    ImGui::DragFloat("Start Offset Y (px)", &s.startOffsetY, 1.0f, -100.0f, 100.0f);
    ImGui::DragFloat("Random Spread X (px)", &s.randomSpreadX, 1.0f, 0.0f, 100.0f);

    ImGui::Separator();

    // Fade & Pop
    ImGui::DragFloat("Fade Delay (s)", &s.fadeDelay, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Pop Scale", &s.popScale, 0.05f, 0.5f, 5.0f);
    ImGui::DragFloat("Pop Duration (s)", &s.popDuration, 0.01f, 0.0f, 2.0f);

    ImGui::Unindent(8.0f);
    ImGui::PopID();
}

void drawCombatTextEditorWindow(bool* open) {
    if (!ImGui::Begin("Combat Text Editor", open)) {
        ImGui::End();
        return;
    }

    auto& cfg = CombatTextConfig::instance();

    if (ImGui::Button("Save")) {
        cfg.save(CombatTextConfig::kDefaultPath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to Defaults")) {
        cfg.loadDefaults();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload from File")) {
        cfg.loadDefaults();
        cfg.load(CombatTextConfig::kDefaultPath);
    }

    ImGui::Separator();

    drawStyleEditor("Damage",   cfg.damage);
    drawStyleEditor("Crit",     cfg.crit);
    drawStyleEditor("Miss",     cfg.miss);
    drawStyleEditor("Resist",   cfg.resist);
    drawStyleEditor("XP",       cfg.xp);
    drawStyleEditor("Level Up", cfg.levelUp);
    drawStyleEditor("Heal",     cfg.heal);
    drawStyleEditor("Block",    cfg.block);

    ImGui::End();
}
#else
void drawCombatTextEditorWindow(bool* open) {
    if (!ImGui::Begin("Combat Text Editor", open)) {
        ImGui::End();
        return;
    }
    ImGui::Text("Combat Text Editor requires game code");
    ImGui::End();
}
#endif // FATE_HAS_GAME

} // namespace fate
