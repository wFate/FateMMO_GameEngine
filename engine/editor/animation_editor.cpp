#include "engine/editor/animation_editor.h"
#include <imgui.h>

namespace fate {

void AnimationEditor::init() {}

void AnimationEditor::shutdown() {}

void AnimationEditor::draw() {
    if (!open_) return;

    if (ImGui::Begin("Animation Editor", &open_, ImGuiWindowFlags_MenuBar)) {
        ImGui::TextUnformatted("Animation Editor \xe2\x80\x94 under construction");
    }
    ImGui::End();
}

void AnimationEditor::openFile(const std::string& /*path*/) {
    open_ = true;
}

// Draw sub-panels (stubs)
void AnimationEditor::drawTopBar() {}
void AnimationEditor::drawFrameWorkspace() {}
void AnimationEditor::drawStateList() {}
void AnimationEditor::drawFrameStrip() {}
void AnimationEditor::drawStateProperties() {}
void AnimationEditor::drawPreview() {}
void AnimationEditor::drawMenuBar() {}

// File I/O (stubs)
void AnimationEditor::newTemplate(const std::string& /*entityType*/) {}
void AnimationEditor::loadTemplate(const std::string& /*path*/) {}
void AnimationEditor::saveTemplate() {}
void AnimationEditor::loadFrameSet(const std::string& /*path*/) {}
void AnimationEditor::saveFrameSet(const std::string& /*layerVariantKey*/) {}
void AnimationEditor::packFrameSet(const std::string& /*layerVariantKey*/) {}

// Helpers
std::string AnimationEditor::currentLayerVariantKey() const {
    return selectedLayer_ + ":" + selectedVariant_;
}

std::vector<std::string>& AnimationEditor::currentFrameList() {
    auto key = currentLayerVariantKey();
    auto& fs = frameSets_[key];
    if (fs.templateName.empty()) {
        fs.templateName = template_.name;
        fs.layer = selectedLayer_;
        fs.variant = selectedVariant_;
    }
    if (selectedStateIdx_ < 0 || selectedStateIdx_ >= (int)template_.states.size()) {
        static std::vector<std::string> fallback;
        fallback.clear();
        return fallback;
    }
    auto& state = template_.states[selectedStateIdx_];
    return fs.frames[state.name][directionName(selectedDirection_)];
}

const char* AnimationEditor::directionName(int idx) const {
    switch (idx) {
        case 0: return "down";
        case 1: return "up";
        case 2: return "side";
        default: return "down";
    }
}

unsigned int AnimationEditor::loadFrameTexture(const std::string& /*path*/) {
    return 0;
}

} // namespace fate
