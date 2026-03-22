#include "engine/editor/asset_browser.h"
#include "engine/core/logger.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <filesystem>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

// ============================================================================
// File-local helpers for type colors/icons (need ImVec4 from imgui.h)
// ============================================================================
namespace {

// Indices match AssetBrowser::AssetType enum:
// Sprite=0, Script=1, Scene=2, Shader=3, Audio=4, Font=5, Tile=6, Prefab=7, Animation=8, Other=9
static ImVec4 colorForType(int type) {
    switch (type) {
        case 0:  return {0.35f, 0.65f, 0.35f, 1.0f};   // Sprite - green
        case 1:  return {0.35f, 0.65f, 0.80f, 1.0f};   // Script - cyan
        case 2:  return {0.35f, 0.80f, 0.50f, 1.0f};   // Scene - mint
        case 3:  return {0.80f, 0.58f, 0.35f, 1.0f};   // Shader - orange
        case 4:  return {0.72f, 0.35f, 0.72f, 1.0f};   // Audio - purple
        case 5:  return {0.72f, 0.72f, 0.42f, 1.0f};   // Font - yellow
        case 6:  return {0.50f, 0.50f, 0.72f, 1.0f};   // Tile - blue
        case 7:  return {0.80f, 0.50f, 0.50f, 1.0f};   // Prefab - red
        case 8:  return {0.42f, 0.80f, 0.72f, 1.0f};   // Animation - teal
        default: return {0.50f, 0.50f, 0.50f, 1.0f};   // Other - gray
    }
}

static const char* iconForType(int type) {
    switch (type) {
        case 0:  return "IMG";
        case 1:  return "SRC";
        case 2:  return "SCN";
        case 3:  return "SHD";
        case 4:  return "SFX";
        case 5:  return "FNT";
        case 6:  return "TIL";
        case 7:  return "PRE";
        case 8:  return "ANM";
        default: return "???";
    }
}

static void drawCheckerboard(ImDrawList* dl, ImVec2 pos, float w, float h, int checkSize = 8) {
    ImU32 c1 = IM_COL32(40, 40, 40, 255);
    ImU32 c2 = IM_COL32(50, 50, 50, 255);
    for (int y = 0; y < (int)h; y += checkSize) {
        for (int x = 0; x < (int)w; x += checkSize) {
            ImU32 c = ((x / checkSize + y / checkSize) % 2 == 0) ? c1 : c2;
            float x1 = pos.x + (float)x, y1 = pos.y + (float)y;
            float x2 = x1 + fminf((float)checkSize, w - (float)x);
            float y2 = y1 + fminf((float)checkSize, h - (float)y);
            dl->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), c);
        }
    }
}

} // anonymous namespace

namespace fate {

// ============================================================================
// Init / Scan
// ============================================================================

void AssetBrowser::init(const std::string& assetRoot, const std::string& sourceDir) {
    assetRoot_ = assetRoot;
    sourceDir_ = sourceDir;
    currentDir_.clear();  // root
    scan();
}

void AssetBrowser::scan() {
    scanDirectory(currentDir_);
}

void AssetBrowser::scanDirectory(const std::string& relDir) {
    currentEntries_.clear();

    // Build absolute path: assetRoot / relDir
    fs::path absDir = fs::path(assetRoot_);
    if (!relDir.empty()) {
        absDir /= relDir;
    }

    if (!fs::exists(absDir) || !fs::is_directory(absDir)) {
        LOG_WARN("AssetBrowser", "Directory does not exist: %s", absDir.string().c_str());
        return;
    }

    for (auto& dirEntry : fs::directory_iterator(absDir)) {
        Entry e;
        e.name = dirEntry.path().filename().string();
        e.fullPath = dirEntry.path().string();
        std::replace(e.fullPath.begin(), e.fullPath.end(), '\\', '/');

        // Compute relative path from assetRoot
        auto relPath = fs::relative(dirEntry.path(), fs::path(assetRoot_));
        e.relativePath = relPath.string();
        std::replace(e.relativePath.begin(), e.relativePath.end(), '\\', '/');

        if (dirEntry.is_directory()) {
            e.isDirectory = true;
            e.type = AssetType::Other;
            currentEntries_.push_back(std::move(e));
        } else if (dirEntry.is_regular_file()) {
            e.extension = dirEntry.path().extension().string();
            std::transform(e.extension.begin(), e.extension.end(), e.extension.begin(), ::tolower);
            e.type = classifyExtension(e.extension);
            e.isDirectory = false;
            currentEntries_.push_back(std::move(e));
        }
    }

    // Sort: directories first, then by type, then by name
    std::sort(currentEntries_.begin(), currentEntries_.end(),
        [](const Entry& a, const Entry& b) {
            if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
            if (a.type != b.type) return (int)a.type < (int)b.type;
            return a.name < b.name;
        });
}

void AssetBrowser::navigateTo(const std::string& relDir) {
    currentDir_ = relDir;
    searchBuf_[0] = '\0';
    scanDirectory(currentDir_);
}

// ============================================================================
// Type Classification
// ============================================================================

AssetBrowser::AssetType AssetBrowser::classifyExtension(const std::string& ext) const {
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
        return AssetType::Sprite;
    if (ext == ".lua" || ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".c")
        return AssetType::Script;
    if (ext == ".json")
        return AssetType::Scene;
    if (ext == ".vert" || ext == ".frag" || ext == ".glsl")
        return AssetType::Shader;
    if (ext == ".ogg" || ext == ".wav" || ext == ".mp3" || ext == ".flac")
        return AssetType::Audio;
    if (ext == ".ttf" || ext == ".otf")
        return AssetType::Font;
    if (ext == ".anim" || ext == ".frameset")
        return AssetType::Animation;
    return AssetType::Other;
}

// ============================================================================
// Thumbnail Cache
// ============================================================================

std::shared_ptr<Texture> AssetBrowser::getThumbnail(const Entry& entry) {
    if (entry.type != AssetType::Sprite) return nullptr;

    auto it = thumbCache_.find(entry.fullPath);
    if (it != thumbCache_.end()) return it->second;

    auto tex = TextureCache::instance().load(entry.fullPath);
    thumbCache_[entry.fullPath] = tex;
    return tex;
}

// ============================================================================
// Draw
// ============================================================================

void AssetBrowser::draw(World* world, Camera* camera) {
    // Breadcrumb navigation
    drawBreadcrumb();

    // Search bar
    drawSearchBar();

    ImGui::Separator();

    // Placement indicator
    if (isDraggingAsset_) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "PLACING: %s (click scene | ESC cancel)",
            fs::path(draggedAssetPath_).stem().string().c_str());
        ImGui::Separator();
    }

    // Thumbnail size slider
    ImGui::SetNextItemWidth(100);
    ImGui::SliderInt("##ThumbSize", &thumbnailSize_, 32, 128, "%d px");
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        scanDirectory(currentDir_);
    }
    ImGui::SameLine();
    ImGui::Text("(%zu items)", currentEntries_.size());

    ImGui::Separator();

    // Grid content
    drawGrid(world, camera);
}

// ============================================================================
// Breadcrumb
// ============================================================================

void AssetBrowser::drawBreadcrumb() {
    if (fontSmall_) ImGui::PushFont(fontSmall_);

    // Root button
    if (ImGui::SmallButton("assets")) {
        navigateTo("");
    }

    if (!currentDir_.empty()) {
        // Split currentDir_ by '/'
        std::string accumulated;
        std::string remaining = currentDir_;

        size_t pos = 0;
        while (pos < remaining.size()) {
            size_t slash = remaining.find('/', pos);
            std::string segment;
            if (slash == std::string::npos) {
                segment = remaining.substr(pos);
                pos = remaining.size();
            } else {
                segment = remaining.substr(pos, slash - pos);
                pos = slash + 1;
            }

            if (segment.empty()) continue;

            if (!accumulated.empty()) accumulated += "/";
            accumulated += segment;

            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.502f, 0.502f, 0.533f, 1.0f), ">");
            ImGui::SameLine();

            // Make each segment clickable
            ImGui::PushID(accumulated.c_str());
            if (ImGui::SmallButton(segment.c_str())) {
                navigateTo(accumulated);
                ImGui::PopID();
                if (fontSmall_) ImGui::PopFont();
                return;  // early out since entries changed
            }
            ImGui::PopID();
        }
    }

    if (fontSmall_) ImGui::PopFont();
}

// ============================================================================
// Search Bar
// ============================================================================

void AssetBrowser::drawSearchBar() {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##AssetSearch", "Search...", searchBuf_, sizeof(searchBuf_));
}

// ============================================================================
// Grid View
// ============================================================================

void AssetBrowser::drawGrid(World* world, Camera* camera) {
    float thumbF = (float)thumbnailSize_;
    float padding = 8.0f;
    float cellSize = thumbF + padding;
    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columns = (int)(panelWidth / cellSize);
    if (columns < 1) columns = 1;

    // Filter entries by search text
    std::string searchStr(searchBuf_);
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

    ImGui::BeginChild("AssetGrid", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    int col = 0;
    for (auto& entry : currentEntries_) {
        // Apply search filter
        if (!searchStr.empty()) {
            std::string nameLower = entry.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (nameLower.find(searchStr) == std::string::npos) continue;
        }

        ImGui::PushID(entry.fullPath.c_str());
        ImGui::BeginGroup();

        float itemW = thumbF;
        float itemH = thumbF;

        int typeInt = (int)entry.type;

        if (entry.isDirectory) {
            // Directory: colored folder button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.50f, 1.0f));

            char btnLabel[128];
            snprintf(btnLabel, sizeof(btnLabel), "DIR\n%s##dir", entry.name.c_str());
            if (ImGui::Button(btnLabel, ImVec2(itemW, itemH))) {
                // Single click selects
            }
            ImGui::PopStyleColor(2);

            // Double-click navigates into directory
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                navigateTo(entry.relativePath);
                ImGui::EndGroup();
                ImGui::PopID();
                ImGui::EndChild();
                return;  // entries changed, bail out
            }
        } else if (entry.type == AssetType::Sprite) {
            // Sprite: thumbnail
            auto thumb = getThumbnail(entry);
            if (thumb) {
                ImTextureID texId = (ImTextureID)(intptr_t)thumb->id();
                bool selected = (draggedAssetPath_ == entry.relativePath);
                if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 1.0f, 0.7f));

                ImVec2 thumbPos = ImGui::GetCursorScreenPos();
                drawCheckerboard(ImGui::GetWindowDrawList(), thumbPos, itemW - 8, itemH - 8);

                char btnId[128];
                snprintf(btnId, sizeof(btnId), "##thumb_%s", entry.name.c_str());
                if (ImGui::ImageButton(btnId, texId, ImVec2(itemW - 8, itemH - 8),
                                       ImVec2(0, 1), ImVec2(1, 0))) {
                    isDraggingAsset_ = true;
                    draggedAssetPath_ = entry.relativePath;
                }
                if (selected) ImGui::PopStyleColor();

                if (draggedAssetPath_ == entry.relativePath && !entry.isDirectory) {
                    ImVec2 min = ImGui::GetItemRectMin();
                    ImVec2 max = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRect(min, max, IM_COL32(74, 138, 219, 255), 2.0f, 0, 2.0f);
                }
            } else {
                // Fallback if texture failed to load
                ImVec4 tc = colorForType(typeInt);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(tc.x * 0.3f, tc.y * 0.3f, tc.z * 0.3f, 1.0f));
                char btnLabel[128];
                snprintf(btnLabel, sizeof(btnLabel), "%s##file", iconForType(typeInt));
                if (ImGui::Button(btnLabel, ImVec2(itemW, itemH))) {
                    isDraggingAsset_ = true;
                    draggedAssetPath_ = entry.relativePath;
                }
                ImGui::PopStyleColor();
            }

            // Drag source for sprites
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                const char* path = entry.fullPath.c_str();
                ImGui::SetDragDropPayload("ASSET", path, strlen(path) + 1);
                ImGui::Text("%s", entry.name.c_str());
                ImGui::EndDragDropSource();
            }
        } else {
            // Other file types: colored placeholder button
            ImVec4 typeColor = colorForType(typeInt);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(typeColor.x * 0.3f, typeColor.y * 0.3f, typeColor.z * 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(typeColor.x * 0.5f, typeColor.y * 0.5f, typeColor.z * 0.5f, 1.0f));

            char btnLabel[128];
            snprintf(btnLabel, sizeof(btnLabel), "%s##file", iconForType(typeInt));
            if (ImGui::Button(btnLabel, ImVec2(itemW, itemH))) {
                LOG_INFO("AssetBrowser", "Selected: %s", entry.fullPath.c_str());
            }

            // Double-click opens animation files in the animation editor
            if (entry.type == AssetType::Animation && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                if (onOpenAnimation) {
                    onOpenAnimation(entry.fullPath);
                }
            }

            ImGui::PopStyleColor(2);
        }

        if (ImGui::IsItemHovered() && !entry.isDirectory) {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(255, 255, 255, 15));
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextItem("##ctx")) {
            ImGui::TextDisabled("%s", entry.name.c_str());
            ImGui::Separator();

            if (entry.type == AssetType::Sprite && !entry.isDirectory) {
                if (ImGui::MenuItem("Place in Scene")) {
                    isDraggingAsset_ = true;
                    draggedAssetPath_ = entry.relativePath;
                }
            }
#ifdef _WIN32
            if ((entry.type == AssetType::Script || entry.type == AssetType::Shader) && !entry.isDirectory) {
                if (ImGui::MenuItem("Open in VS Code")) {
                    std::string cmd = "code \"" + entry.fullPath + "\"";
                    system(cmd.c_str());
                }
            }
            if (ImGui::MenuItem("Show in Explorer")) {
                std::string dir = entry.fullPath;
                if (!entry.isDirectory) {
                    size_t lastSlash = dir.find_last_of("/\\");
                    if (lastSlash != std::string::npos) dir = dir.substr(0, lastSlash);
                }
                for (auto& c : dir) if (c == '/') c = '\\';
                std::string cmd = "explorer \"" + dir + "\"";
                system(cmd.c_str());
            }
#endif
            if (ImGui::MenuItem("Copy Path")) {
                ImGui::SetClipboardText(entry.fullPath.c_str());
            }

            ImGui::EndPopup();
        }

        // Tooltip on hover
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\n%s", entry.fullPath.c_str(),
                entry.isDirectory ? "(directory)" : entry.extension.c_str());
        }

        // Filename label (truncated)
        std::string displayName = entry.name;
        float maxLabelWidth = itemW + 4.0f;
        ImVec2 textSize = ImGui::CalcTextSize(displayName.c_str());
        if (textSize.x > maxLabelWidth && displayName.size() > 8) {
            displayName = displayName.substr(0, 7) + "..";
        }
        ImGui::TextWrapped("%s", displayName.c_str());

        ImGui::EndGroup();

        col++;
        if (col < columns) {
            ImGui::SameLine();
        } else {
            col = 0;
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
}

} // namespace fate
