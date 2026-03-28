#pragma once

namespace fate {

/// Draws a standalone ImGui window for editing CombatTextConfig styles.
/// Pass a pointer to a bool to control window visibility (ImGui::Begin pattern).
void drawCombatTextEditorWindow(bool* open);

} // namespace fate
