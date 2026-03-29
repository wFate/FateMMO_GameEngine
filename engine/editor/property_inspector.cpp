#include "engine/editor/property_inspector.h"
#include "engine/core/types.h"
#include <imgui.h>
#include <algorithm>
#include <string>
#include <cstring>
#include <vector>

namespace fate {

void drawPropertyInspector(void* instance,
                           std::span<const PropertyInfo> properties,
                           const std::function<void()>& undoCapture) {
    if (!instance || properties.empty()) return;

    auto* base = static_cast<uint8_t*>(instance);

    // Build sorted list with original indices for stable ordering
    struct Entry {
        const PropertyInfo* prop;
        int declOrder;
    };
    std::vector<Entry> entries;
    entries.reserve(properties.size());
    for (int i = 0; i < static_cast<int>(properties.size()); ++i) {
        if (properties[i].control == EditorControl::Hidden) continue;
        entries.push_back({&properties[i], i});
    }

    // Sort by category (nullptr first as "General"), then order, then declaration order
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        const char* catA = a.prop->category ? a.prop->category : "";
        const char* catB = b.prop->category ? b.prop->category : "";
        int cmp = std::strcmp(catA, catB);
        if (cmp != 0) return cmp < 0;
        if (a.prop->order != b.prop->order) return a.prop->order < b.prop->order;
        return a.declOrder < b.declOrder;
    });

    // Render grouped by category
    const char* currentCategory = nullptr;
    bool categoryOpen = true;

    for (const auto& entry : entries) {
        const auto& prop = *entry.prop;
        void* ptr = base + prop.offset;

        // Category header transition
        const char* cat = prop.category ? prop.category : "";
        bool newCategory = (currentCategory == nullptr || std::strcmp(currentCategory, cat) != 0);
        if (newCategory) {
            // Close previous category if it was a tree node
            if (currentCategory != nullptr && currentCategory[0] != '\0' && categoryOpen) {
                ImGui::TreePop();
            }
            currentCategory = cat;
            if (cat[0] != '\0') {
                categoryOpen = ImGui::TreeNodeEx(cat, ImGuiTreeNodeFlags_DefaultOpen);
            } else {
                categoryOpen = true; // ungrouped fields always shown
            }
        }

        if (!categoryOpen) continue;

        const char* label = prop.displayName ? prop.displayName : prop.name;

        // Determine effective control
        EditorControl ctrl = prop.control;
        if (ctrl == EditorControl::Auto) {
            switch (prop.type) {
                case FieldType::Bool:  ctrl = EditorControl::Checkbox; break;
                case FieldType::Color: ctrl = EditorControl::ColorPicker; break;
                case FieldType::Enum:  ctrl = prop.enumNames ? EditorControl::Dropdown : EditorControl::ReadOnly; break;
                default:
                    ctrl = (prop.min != 0.0f || prop.max != 0.0f)
                         ? EditorControl::Slider : EditorControl::Auto;
                    break;
            }
        }

        // Push unique ID to avoid ImGui label collisions across categories
        ImGui::PushID(entry.declOrder);

        switch (prop.type) {
            case FieldType::Float: {
                float speed = prop.step > 0.0f ? prop.step : 0.1f;
                if (ctrl == EditorControl::Slider || (prop.min != 0.0f || prop.max != 0.0f)) {
                    ImGui::DragFloat(label, static_cast<float*>(ptr), speed, prop.min, prop.max);
                } else {
                    ImGui::DragFloat(label, static_cast<float*>(ptr), speed);
                }
                undoCapture();
                break;
            }
            case FieldType::Int: {
                if (prop.min != 0.0f || prop.max != 0.0f) {
                    ImGui::DragInt(label, static_cast<int*>(ptr), 1.0f,
                                   static_cast<int>(prop.min), static_cast<int>(prop.max));
                } else {
                    ImGui::DragInt(label, static_cast<int*>(ptr));
                }
                undoCapture();
                break;
            }
            case FieldType::UInt: {
                ImGui::DragScalar(label, ImGuiDataType_U32, ptr);
                undoCapture();
                break;
            }
            case FieldType::Bool: {
                ImGui::Checkbox(label, static_cast<bool*>(ptr));
                undoCapture();
                break;
            }
            case FieldType::Vec2: {
                auto* v = static_cast<Vec2*>(ptr);
                float vals[2] = {v->x, v->y};
                float speed = prop.step > 0.0f ? prop.step : 0.5f;
                if (ImGui::DragFloat2(label, vals, speed)) {
                    v->x = vals[0]; v->y = vals[1];
                }
                undoCapture();
                break;
            }
            case FieldType::Vec3: {
                auto* v = static_cast<Vec3*>(ptr);
                float vals[3] = {v->x, v->y, v->z};
                if (ImGui::DragFloat3(label, vals, 0.5f)) {
                    v->x = vals[0]; v->y = vals[1]; v->z = vals[2];
                }
                undoCapture();
                break;
            }
            case FieldType::Vec4: {
                auto* v = static_cast<Vec4*>(ptr);
                float vals[4] = {v->x, v->y, v->z, v->w};
                if (ImGui::DragFloat4(label, vals, 0.5f)) {
                    v->x = vals[0]; v->y = vals[1]; v->z = vals[2]; v->w = vals[3];
                }
                undoCapture();
                break;
            }
            case FieldType::Color: {
                ImGui::ColorEdit4(label, &static_cast<Color*>(ptr)->r);
                undoCapture();
                break;
            }
            case FieldType::Rect: {
                auto* r = static_cast<Rect*>(ptr);
                float vals[4] = {r->x, r->y, r->w, r->h};
                if (ImGui::DragFloat4(label, vals, 0.5f)) {
                    r->x = vals[0]; r->y = vals[1]; r->w = vals[2]; r->h = vals[3];
                }
                undoCapture();
                break;
            }
            case FieldType::String: {
                auto* s = static_cast<std::string*>(ptr);
                char buf[512] = {};
                strncpy(buf, s->c_str(), sizeof(buf) - 1);
                if (ctrl == EditorControl::TextMultiline) {
                    if (ImGui::InputTextMultiline(label, buf, sizeof(buf))) {
                        *s = buf;
                    }
                } else {
                    if (ImGui::InputText(label, buf, sizeof(buf))) {
                        *s = buf;
                    }
                }
                undoCapture();
                break;
            }
            case FieldType::Enum: {
                if (ctrl == EditorControl::Dropdown && prop.enumNames && prop.enumCount > 0) {
                    int32_t val = 0;
                    std::memcpy(&val, ptr, prop.size <= sizeof(val) ? prop.size : sizeof(val));
                    if (val >= 0 && val < prop.enumCount) {
                        if (ImGui::Combo(label, &val, prop.enumNames, prop.enumCount)) {
                            std::memcpy(ptr, &val, prop.size <= sizeof(val) ? prop.size : sizeof(val));
                        }
                    }
                } else {
                    int32_t val = 0;
                    std::memcpy(&val, ptr, prop.size <= sizeof(val) ? prop.size : sizeof(val));
                    ImGui::TextDisabled("%s: %d", label, val);
                }
                undoCapture();
                break;
            }
            case FieldType::EntityHandle:
            case FieldType::Direction:
            case FieldType::Custom:
            default: {
                if (ctrl == EditorControl::ReadOnly) {
                    ImGui::TextDisabled("%s: [read-only]", label);
                } else {
                    ImGui::TextDisabled("%s: [unsupported type]", label);
                }
                break;
            }
        }

        // Tooltip
        if (prop.tooltip && ImGui::IsItemHovered()) {
            ImGui::SetItemTooltip("%s", prop.tooltip);
        }

        ImGui::PopID();
    }

    // Close last category tree node
    if (currentCategory != nullptr && currentCategory[0] != '\0' && categoryOpen) {
        ImGui::TreePop();
    }
}

} // namespace fate
