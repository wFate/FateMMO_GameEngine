# Editor Safety Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix path traversal in save dialogs, add delete confirmation dialogs, and guard console command parsing against crashes.

**Architecture:** All changes are in `engine/editor/editor.cpp`. A static `isValidAssetName()` helper rejects path separators and traversal sequences. Delete operations use ImGui modal popups for confirmation. Console numeric parsing wraps in try-catch.

**Tech Stack:** C++20, Dear ImGui (modal popups), `std::filesystem`

**Build command:** `"C:/Program Files/Microsoft Visual Studio/2025/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build`

**Test command:** `./build/Debug/fate_tests.exe`

**IMPORTANT:** Before building, `touch` every edited `.cpp` file (CMake misses changes silently on this setup).

---

## File Map

| Action | File | Responsibility |
|--------|------|----------------|
| Modify | `engine/editor/editor.cpp` | All changes — validation helper, confirmation popups, try-catch guards |
| Modify | `engine/editor/editor.h` | Add pending-delete state fields |

---

### Task 1: Add filename validation helper and apply to save dialogs

**Files:**
- Modify: `engine/editor/editor.cpp:341-349` (Scene Save As)
- Modify: `engine/editor/editor.cpp:1883-1884` (Prefab Save)

- [ ] **Step 1: Add isValidAssetName() helper**

In `engine/editor/editor.cpp`, add a static helper in the anonymous namespace near the top of the file (after the includes, before any class methods). Find a good spot after the last `#include` or after the namespace opening:

```cpp
namespace {
    bool isValidAssetName(const char* name) {
        if (!name || name[0] == '\0') return false;
        for (const char* p = name; *p; ++p) {
            if (*p == '/' || *p == '\\' || *p == '\0') return false;
        }
        // Reject ".." anywhere in the name
        if (strstr(name, "..") != nullptr) return false;
        return true;
    }
} // anonymous namespace
```

If there's already an anonymous namespace at the top, add the function inside it.

- [ ] **Step 2: Guard Scene "Save As"**

In `engine/editor/editor.cpp`, find the Scene Save As block (around line 344):

```cpp
                if (ImGui::Button("Save")) {
                    std::string path = std::string("assets/scenes/") + saveNameBuf + ".json";
                    saveScene(dockWorld_, path);
                    ImGui::CloseCurrentPopup();
                }
```

Replace with:

```cpp
                if (ImGui::Button("Save")) {
                    if (isValidAssetName(saveNameBuf)) {
                        std::string path = std::string("assets/scenes/") + saveNameBuf + ".json";
                        saveScene(dockWorld_, path);
                        ImGui::CloseCurrentPopup();
                    } else {
                        LOG_WARN("Editor", "Invalid scene name: %s", saveNameBuf);
                    }
                }
```

- [ ] **Step 3: Guard Prefab Save**

In `engine/editor/editor.cpp`, find the Prefab Save button (around line 1883):

```cpp
        if (ImGui::Button("Save", ImVec2(120, 0)) && prefabNameBuf[0] != '\0') {
            PrefabLibrary::instance().save(prefabNameBuf, selectedEntity_);
```

Replace with:

```cpp
        if (ImGui::Button("Save", ImVec2(120, 0)) && isValidAssetName(prefabNameBuf)) {
            PrefabLibrary::instance().save(prefabNameBuf, selectedEntity_);
```

This replaces the `prefabNameBuf[0] != '\0'` check with `isValidAssetName()` which also checks for empty strings, so functionality is preserved.

- [ ] **Step 4: Touch, build, verify compilation**

```bash
touch engine/editor/editor.cpp
```
Build. Expected: compiles cleanly.

- [ ] **Step 5: Run full test suite**

`./build/Debug/fate_tests.exe`
Expected: All tests pass (371).

- [ ] **Step 6: Commit**

```bash
git add engine/editor/editor.cpp
git commit -m "fix: validate asset names in save dialogs to prevent path traversal"
```

---

### Task 2: Add delete confirmation dialog for files

**Files:**
- Modify: `engine/editor/editor.h` (add pending delete state)
- Modify: `engine/editor/editor.cpp:1106-1112` (Delete File menu item)

- [ ] **Step 1: Add pending-delete state to editor.h**

In `engine/editor/editor.h`, find the section with editor state fields (around line 230, after `consoleCmdBuf_`). Add:

```cpp
    // Delete confirmation state
    bool pendingDeleteFile_ = false;
    std::string pendingDeletePath_;
    bool pendingDeletePrefab_ = false;
    std::string pendingDeletePrefabName_;
```

- [ ] **Step 2: Replace immediate file deletion with popup trigger**

In `engine/editor/editor.cpp`, find the Delete File menu item (around line 1106):

```cpp
                            if (ImGui::MenuItem("Delete File")) {
                                if (fs::exists(asset.fullPath)) {
                                    fs::remove(asset.fullPath);
                                    LOG_INFO("Editor", "Deleted: %s", asset.fullPath.c_str());
                                    scanAssets(); // refresh
                                }
                            }
```

Replace with:

```cpp
                            if (ImGui::MenuItem("Delete File")) {
                                pendingDeleteFile_ = true;
                                pendingDeletePath_ = asset.fullPath;
                            }
```

- [ ] **Step 3: Add confirmation modal popup**

In `engine/editor/editor.cpp`, find a suitable place to render the modal popup. Add it right after the asset browser's `ImGui::End()` call, or at the end of the `drawAssetBrowser()` or equivalent function that contains the delete menu item. If the delete code lives inside a large function, add the popup code right after the `ImGui::EndPopup()` that closes the asset context menu (around line 1113-1115):

Find the nearest clean insertion point after the context menu block. After line 1115 (the `};` that closes the lambda or block containing the delete), add the popup rendering. Actually, since ImGui popups need to be in the same window scope, add this code just before the final `ImGui::End()` of the asset browser panel. Search for where the asset browser panel ends.

The safest approach: add this popup code right before the function returns or the panel ends. Find a location near the delete code where the popup will be within the same ImGui window. Add after the closing of the context popup block:

After the line `ImGui::EndPopup();` at line 1113 (the one that closes the asset context menu), and after the `};` at line 1115, add:

```cpp
    // --- Delete File Confirmation Popup ---
    if (pendingDeleteFile_) {
        ImGui::OpenPopup("Delete File?");
        pendingDeleteFile_ = false;
    }
    if (ImGui::BeginPopupModal("Delete File?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete this file?");
        ImGui::TextWrapped("%s", pendingDeletePath_.c_str());
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            if (fs::exists(pendingDeletePath_)) {
                fs::remove(pendingDeletePath_);
                LOG_INFO("Editor", "Deleted: %s", pendingDeletePath_.c_str());
                scanAssets();
            }
            pendingDeletePath_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            pendingDeletePath_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
```

IMPORTANT: The `OpenPopup` and `BeginPopupModal` calls must be in the same ImGui window/ID scope. Place them in the same function scope as the Delete menu item, but OUTSIDE of any `BeginPopup`/`EndPopup` block (since you can't nest popups). The implementer should read the surrounding code to find the right insertion point — it needs to be within the same function, after the context menu's `EndPopup()`.

- [ ] **Step 4: Touch, build, verify compilation**

```bash
touch engine/editor/editor.h engine/editor/editor.cpp
```
Build. Expected: compiles cleanly.

- [ ] **Step 5: Commit**

```bash
git add engine/editor/editor.h engine/editor/editor.cpp
git commit -m "fix: add confirmation dialog for file deletion in asset browser"
```

---

### Task 3: Add delete confirmation dialog for prefabs

**Files:**
- Modify: `engine/editor/editor.cpp:1238-1250` (Delete Prefab menu item)

- [ ] **Step 1: Replace immediate prefab deletion with popup trigger**

In `engine/editor/editor.cpp`, find the Delete Prefab block (around line 1238):

```cpp
                        if (ImGui::MenuItem("Delete Prefab")) {
                            // Delete from runtime dir
                            std::string path = "assets/prefabs/" + pname + ".json";
                            if (fs::exists(path)) fs::remove(path);
                            // Delete from source dir if set
                            auto& plib = PrefabLibrary::instance();
                            // The library knows both paths, just reload
                            lib.loadAll();
                            LOG_INFO("Editor", "Deleted prefab: %s", pname.c_str());
                            ImGui::EndPopup();
                            ImGui::PopID();
                            break; // list changed, stop iterating
                        }
```

Replace with:

```cpp
                        if (ImGui::MenuItem("Delete Prefab")) {
                            pendingDeletePrefab_ = true;
                            pendingDeletePrefabName_ = pname;
                        }
```

- [ ] **Step 2: Add prefab delete confirmation popup**

Add this code in the same function scope, outside the prefab context menu's `BeginPopup`/`EndPopup` block, but still within the prefabs panel. Place it after the prefab list iteration loop ends (after the for loop over `lib.names()`). The implementer should find where the for loop ends and add this after it:

```cpp
    // --- Delete Prefab Confirmation Popup ---
    if (pendingDeletePrefab_) {
        ImGui::OpenPopup("Delete Prefab?");
        pendingDeletePrefab_ = false;
    }
    if (ImGui::BeginPopupModal("Delete Prefab?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete prefab '%s'?", pendingDeletePrefabName_.c_str());
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            std::string path = "assets/prefabs/" + pendingDeletePrefabName_ + ".json";
            if (fs::exists(path)) fs::remove(path);
            PrefabLibrary::instance().loadAll();
            LOG_INFO("Editor", "Deleted prefab: %s", pendingDeletePrefabName_.c_str());
            pendingDeletePrefabName_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            pendingDeletePrefabName_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
```

- [ ] **Step 3: Touch, build, verify compilation**

```bash
touch engine/editor/editor.cpp
```
Build. Expected: compiles cleanly.

- [ ] **Step 4: Commit**

```bash
git add engine/editor/editor.cpp
git commit -m "fix: add confirmation dialog for prefab deletion"
```

---

### Task 4: Guard console command parsing with try-catch

**Files:**
- Modify: `engine/editor/editor.cpp:3206-3228` (console command handlers)

- [ ] **Step 1: Wrap "delete" command parsing**

Find (around line 3206-3215):

```cpp
    else if (args[0] == "delete" && args.size() > 1) {
        if (!world) return;
        EntityId id = (EntityId)std::stoul(args[1]);
        auto* e = world->getEntity(id);
        if (e) {
            LOG_INFO("Console", "Deleted entity %u (%s)", id, e->name().c_str());
            world->destroyEntity(id);
        } else {
            LOG_WARN("Console", "Entity %u not found", id);
        }
    }
```

Replace with:

```cpp
    else if (args[0] == "delete" && args.size() > 1) {
        if (!world) return;
        try {
            EntityId id = (EntityId)std::stoul(args[1]);
            auto* e = world->getEntity(id);
            if (e) {
                LOG_INFO("Console", "Deleted entity %u (%s)", id, e->name().c_str());
                world->destroyEntity(id);
            } else {
                LOG_WARN("Console", "Entity %u not found", id);
            }
        } catch (const std::exception&) {
            LOG_WARN("Console", "Invalid entity id: %s", args[1].c_str());
        }
    }
```

- [ ] **Step 2: Wrap "spawn" command parsing**

Find (around line 3217-3223):

```cpp
    else if (args[0] == "spawn" && args.size() > 3) {
        if (!world) return;
        float x = std::stof(args[2]);
        float y = std::stof(args[3]);
        auto* e = PrefabLibrary::instance().spawn(args[1], *world, {x, y});
        if (e) LOG_INFO("Console", "Spawned '%s' at (%.0f, %.0f) id=%u", args[1].c_str(), x, y, e->id());
        else LOG_WARN("Console", "Prefab '%s' not found", args[1].c_str());
    }
```

Replace with:

```cpp
    else if (args[0] == "spawn" && args.size() > 3) {
        if (!world) return;
        try {
            float x = std::stof(args[2]);
            float y = std::stof(args[3]);
            auto* e = PrefabLibrary::instance().spawn(args[1], *world, {x, y});
            if (e) LOG_INFO("Console", "Spawned '%s' at (%.0f, %.0f) id=%u", args[1].c_str(), x, y, e->id());
            else LOG_WARN("Console", "Prefab '%s' not found", args[1].c_str());
        } catch (const std::exception&) {
            LOG_WARN("Console", "Invalid coordinates for spawn");
        }
    }
```

- [ ] **Step 3: Wrap "tp" command parsing**

Find (around line 3225-3233):

```cpp
    else if (args[0] == "tp" && args.size() > 2) {
        if (!world) return;
        float x = std::stof(args[1]);
        float y = std::stof(args[2]);
```

Replace with:

```cpp
    else if (args[0] == "tp" && args.size() > 2) {
        if (!world) return;
        try {
            float x = std::stof(args[1]);
            float y = std::stof(args[2]);
```

And find the closing `}` of the `tp` block and wrap it with the catch:

```cpp
        } catch (const std::exception&) {
            LOG_WARN("Console", "Invalid coordinates for tp");
        }
    }
```

The implementer should read the full `tp` block to identify its closing brace and insert the catch correctly.

- [ ] **Step 4: Touch, build, verify compilation**

```bash
touch engine/editor/editor.cpp
```
Build. Expected: compiles cleanly.

- [ ] **Step 5: Run full test suite**

`./build/Debug/fate_tests.exe`
Expected: All tests pass (371).

- [ ] **Step 6: Commit**

```bash
git add engine/editor/editor.cpp engine/editor/editor.h
git commit -m "fix: guard console commands against invalid input crashes"
```
