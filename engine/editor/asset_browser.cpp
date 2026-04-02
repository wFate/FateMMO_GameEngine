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

// Draw a folder icon (simple filled shape with tab)
static void drawFolderIcon(ImDrawList* dl, ImVec2 pos, float w, float h, ImU32 color, ImU32 tabColor) {
    float tabW = w * 0.4f;
    float tabH = h * 0.15f;
    float rnd = 3.0f;
    // Tab on top-left
    dl->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + tabW, pos.y + tabH + 2.0f), tabColor, rnd);
    // Main body
    dl->AddRectFilled(ImVec2(pos.x, pos.y + tabH), ImVec2(pos.x + w, pos.y + h), color, rnd);
    // Subtle highlight line at top of body
    dl->AddLine(ImVec2(pos.x + 1.0f, pos.y + tabH + 1.0f),
                ImVec2(pos.x + w - 1.0f, pos.y + tabH + 1.0f),
                IM_COL32(255, 255, 255, 30));
}

// Draw type badge centered in a card
static void drawAssetCard(ImDrawList* dl, ImVec2 pos, float w, float h,
                          ImVec4 typeColor, const char* icon, bool selected, bool hovered) {
    float rnd = 4.0f;
    // Card background
    ImU32 bg = ImGui::ColorConvertFloat4ToU32(
        ImVec4(typeColor.x * 0.2f, typeColor.y * 0.2f, typeColor.z * 0.2f, 1.0f));
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), bg, rnd);

    // Colored accent strip at top
    ImU32 accent = ImGui::ColorConvertFloat4ToU32(
        ImVec4(typeColor.x * 0.7f, typeColor.y * 0.7f, typeColor.z * 0.7f, 1.0f));
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + 3.0f), accent, rnd, ImDrawFlags_RoundCornersTop);

    // Type icon centered
    ImVec2 textSz = ImGui::CalcTextSize(icon);
    ImU32 iconCol = ImGui::ColorConvertFloat4ToU32(typeColor);
    dl->AddText(ImVec2(pos.x + (w - textSz.x) * 0.5f, pos.y + (h - textSz.y) * 0.5f), iconCol, icon);

    // Selection / hover borders
    if (selected) {
        dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(74, 138, 219, 255), rnd, 0, 2.0f);
    } else if (hovered) {
        dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(255, 255, 255, 50), rnd);
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
            e.type = classifyFile(e.name, e.extension);
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

AssetBrowser::AssetType AssetBrowser::classifyFile(const std::string& name, const std::string& ext) const {
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
        return AssetType::Sprite;
    if (ext == ".lua" || ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".c")
        return AssetType::Script;
    if (ext == ".json") {
        // .meta.json files are animation metadata, not scenes
        if (name.size() > 10 && name.substr(name.size() - 10) == ".meta.json")
            return AssetType::Animation;
        // .json files inside prefabs/ directory are prefabs, not scenes
        if (currentDir_.find("prefabs") != std::string::npos)
            return AssetType::Prefab;
        return AssetType::Scene;
    }
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
    float labelH = ImGui::GetTextLineHeightWithSpacing() + 2.0f;
    float cellW = thumbF + 12.0f;
    float cellH = thumbF + labelH + 6.0f;
    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columns = (int)(panelWidth / cellW);
    if (columns < 1) columns = 1;

    // Filter entries by search text
    std::string searchStr(searchBuf_);
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

    ImGui::BeginChild("AssetGrid", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    int col = 0;
    for (auto& entry : currentEntries_) {
        // Apply search filter
        if (!searchStr.empty()) {
            std::string nameLower = entry.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (nameLower.find(searchStr) == std::string::npos) continue;
        }

        bool isSel = (selectedPath_ == entry.fullPath);
        ImGui::PushID(entry.fullPath.c_str());
        ImGui::BeginGroup();

        ImVec2 cardPos = ImGui::GetCursorScreenPos();
        int typeInt = (int)entry.type;

        // Invisible button for the whole card area (hit-test + interactions)
        char btnId[128];
        snprintf(btnId, sizeof(btnId), "##card_%s", entry.name.c_str());
        if (ImGui::InvisibleButton(btnId, ImVec2(thumbF, thumbF))) {
            selectedPath_ = entry.fullPath;
            if (!entry.isDirectory) {
                isDraggingAsset_ = true;
                draggedAssetPath_ = entry.relativePath;
                LOG_INFO("AssetBrowser", "Click: name='%s' relPath='%s' type=%d dir='%s'",
                    entry.name.c_str(), entry.relativePath.c_str(), (int)entry.type, currentDir_.c_str());
            }
        }
        bool isHov = ImGui::IsItemHovered();

        // Double-click actions
        if (isHov && ImGui::IsMouseDoubleClicked(0)) {
            if (entry.isDirectory) {
                navigateTo(entry.relativePath);
                ImGui::EndGroup();
                ImGui::PopID();
                ImGui::EndChild();
                return;
            } else if (entry.type == AssetType::Animation) {
                if (onOpenAnimation) onOpenAnimation(entry.fullPath);
            }
        }

        // Draw the card content over the invisible button
        if (entry.isDirectory) {
            // Folder icon
            float pad = thumbF * 0.15f;
            float iconW = thumbF - pad * 2;
            float iconH = thumbF - pad * 2 - 4.0f;
            ImVec2 iconPos(cardPos.x + pad, cardPos.y + pad + 2.0f);
            ImU32 folderCol = isSel ? IM_COL32(90, 160, 230, 255) : IM_COL32(200, 180, 80, 255);
            ImU32 tabCol = isSel ? IM_COL32(70, 130, 200, 255) : IM_COL32(170, 150, 60, 255);
            drawFolderIcon(dl, iconPos, iconW, iconH, folderCol, tabCol);

            if (isHov) {
                dl->AddRect(cardPos, ImVec2(cardPos.x + thumbF, cardPos.y + thumbF),
                            IM_COL32(255, 255, 255, 50), 4.0f);
            }
            if (isSel) {
                dl->AddRect(cardPos, ImVec2(cardPos.x + thumbF, cardPos.y + thumbF),
                            IM_COL32(74, 138, 219, 255), 4.0f, 0, 2.0f);
            }
        } else if (entry.type == AssetType::Sprite) {
            // Sprite thumbnail
            auto thumb = getThumbnail(entry);
            if (thumb) {
                drawCheckerboard(dl, cardPos, thumbF, thumbF);
                ImTextureID texId = (ImTextureID)(intptr_t)thumb->id();
                dl->AddImage(texId, cardPos, ImVec2(cardPos.x + thumbF, cardPos.y + thumbF),
                             ImVec2(0, 1), ImVec2(1, 0));
            } else {
                drawAssetCard(dl, cardPos, thumbF, thumbF, colorForType(typeInt),
                              iconForType(typeInt), isSel, isHov);
            }
            if (isSel) {
                dl->AddRect(cardPos, ImVec2(cardPos.x + thumbF, cardPos.y + thumbF),
                            IM_COL32(74, 138, 219, 255), 4.0f, 0, 2.0f);
            } else if (isHov) {
                dl->AddRect(cardPos, ImVec2(cardPos.x + thumbF, cardPos.y + thumbF),
                            IM_COL32(255, 255, 255, 50), 4.0f);
            }
        } else {
            // File type card
            drawAssetCard(dl, cardPos, thumbF, thumbF, colorForType(typeInt),
                          iconForType(typeInt), isSel, isHov);
        }

        // Drag source for sprites and prefabs
        if ((entry.type == AssetType::Sprite || entry.type == AssetType::Prefab) && !entry.isDirectory) {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                const char* path = entry.fullPath.c_str();
                ImGui::SetDragDropPayload("ASSET", path, strlen(path) + 1);
                ImGui::Text("%s", entry.name.c_str());
                ImGui::EndDragDropSource();
            }
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextItem("##ctx")) {
            ImGui::TextDisabled("%s", entry.name.c_str());
            ImGui::Separator();

            if ((entry.type == AssetType::Sprite || entry.type == AssetType::Prefab) && !entry.isDirectory) {
                if (ImGui::MenuItem("Place in Scene")) {
                    isDraggingAsset_ = true;
                    draggedAssetPath_ = entry.relativePath;
                }
                if (entry.type == AssetType::Sprite && ImGui::MenuItem("Open in Animation Editor")) {
                    if (onOpenAnimation) onOpenAnimation(entry.fullPath);
                }
            }
#ifdef _WIN32
            if ((entry.type == AssetType::Script || entry.type == AssetType::Shader) && !entry.isDirectory) {
                if (ImGui::MenuItem("Open in VS Code")) {
                    std::string cmd = "start \"\" code \"" + entry.fullPath + "\"";
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
                std::string cmd = "start \"\" explorer \"" + dir + "\"";
                system(cmd.c_str());
            }
#endif
            if (ImGui::MenuItem("Copy Path")) {
                ImGui::SetClipboardText(entry.fullPath.c_str());
            }

            ImGui::EndPopup();
        }

        // Tooltip on hover
        if (isHov) {
            ImGui::SetTooltip("%s\n%s", entry.fullPath.c_str(),
                entry.isDirectory ? "(directory)" : entry.extension.c_str());
        }

        // Filename label (truncated, centered)
        std::string displayName = entry.name;
        float maxLabelWidth = thumbF + 4.0f;
        ImVec2 textSize = ImGui::CalcTextSize(displayName.c_str());
        if (textSize.x > maxLabelWidth && displayName.size() > 10) {
            displayName = displayName.substr(0, 9) + "..";
            textSize = ImGui::CalcTextSize(displayName.c_str());
        }
        float labelX = (thumbF - textSize.x) * 0.5f;
        if (labelX < 0) labelX = 0;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + labelX);
        if (isSel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
        ImGui::TextUnformatted(displayName.c_str());
        if (isSel) ImGui::PopStyleColor();

        ImGui::EndGroup();

        col++;
        if (col < columns) {
            ImGui::SameLine();
        } else {
            col = 0;
        }

        ImGui::PopID();
    }

    // Footer: selected file path
    ImGui::EndChild();
    if (!selectedPath_.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", selectedPath_.c_str());
    }
}

} // namespace fate
