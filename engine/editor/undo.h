#pragma once
#include "engine/ecs/world.h"
#include "engine/ecs/entity.h"
#include "engine/ecs/entity_handle.h"
#include "engine/ecs/prefab.h"
#include "engine/core/types.h"
#include "engine/components/transform.h"
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <nlohmann/json.hpp>

namespace fate {

// Domain a command mutates. Drives the editor's dirty bookkeeping so Ctrl+S
// can write only the file(s) that actually changed.
enum class UndoDomain {
    Scene,         // generic entity edit that lands in the scene .json
    PlayerPrefab,  // entity edit on the player; saveScene filters players out
    UIScreen       // UI widget edit; target is the screen .json
};

// Base undo command
struct UndoCommand {
    virtual ~UndoCommand() = default;
    virtual void undo(World* world) = 0;
    virtual void redo(World* world) = 0;
    virtual std::string description() const = 0;
    // Update any stored EntityHandles that match oldH to newH (used by PropertyCommand fixup)
    virtual void remapEntityHandle(EntityHandle oldH, EntityHandle newH) { (void)oldH; (void)newH; }

    // Per-instance flag set by the call site when the command targets the
    // player entity. Lets PropertyCommand/MoveCommand/etc. route to the
    // PlayerPrefab dirty bucket without a tag lookup at save time (the tag
    // would be unreachable after PropertyCommand recreated the entity).
    bool isPlayerPrefab = false;

    // UI commands override to UIScreen. Default routes via isPlayerPrefab.
    virtual UndoDomain domain() const {
        return isPlayerPrefab ? UndoDomain::PlayerPrefab : UndoDomain::Scene;
    }
};

// Move entity
struct MoveCommand : UndoCommand {
    EntityHandle entityHandle;
    Vec2 oldPos, newPos;

    void undo(World* w) override {
        auto* e = w->getEntity(entityHandle);
        if (e) if (auto* t = e->getComponent<Transform>()) t->position = oldPos;
    }
    void redo(World* w) override {
        auto* e = w->getEntity(entityHandle);
        if (e) if (auto* t = e->getComponent<Transform>()) t->position = newPos;
    }
    std::string description() const override { return "Move"; }
    void remapEntityHandle(EntityHandle oldH, EntityHandle newH) override {
        if (entityHandle == oldH) entityHandle = newH;
    }
};

// Resize entity
struct ResizeCommand : UndoCommand {
    EntityHandle entityHandle;
    Vec2 oldSize, newSize;

    void undo(World* w) override;
    void redo(World* w) override;
    std::string description() const override { return "Resize"; }
    void remapEntityHandle(EntityHandle oldH, EntityHandle newH) override {
        if (entityHandle == oldH) entityHandle = newH;
    }
};

// Create entity (undo = delete, redo = recreate)
struct CreateCommand : UndoCommand {
    nlohmann::json entityData;
    EntityHandle createdHandle;

    void undo(World* w) override {
        if (createdHandle) { w->destroyEntity(createdHandle); w->processDestroyQueue("editor_undo_create"); }
    }
    void redo(World* w) override {
        auto* e = PrefabLibrary::jsonToEntity(entityData, *w);
        if (e) createdHandle = e->handle();
    }
    std::string description() const override { return "Create"; }
    void remapEntityHandle(EntityHandle oldH, EntityHandle newH) override {
        if (createdHandle == oldH) createdHandle = newH;
    }
};

// Delete entity (undo = recreate, redo = delete)
struct DeleteCommand : UndoCommand {
    nlohmann::json entityData;
    EntityHandle deletedHandle;

    void undo(World* w) override {
        auto* e = PrefabLibrary::jsonToEntity(entityData, *w);
        if (e) deletedHandle = e->handle();
    }
    void redo(World* w) override {
        if (deletedHandle) { w->destroyEntity(deletedHandle); w->processDestroyQueue("editor_redo_delete"); }
    }
    std::string description() const override { return "Delete"; }
    void remapEntityHandle(EntityHandle oldH, EntityHandle newH) override {
        if (deletedHandle == oldH) deletedHandle = newH;
    }
};

// Rotate entity
struct RotateCommand : UndoCommand {
    EntityHandle entityHandle;
    float oldRotation, newRotation;

    void undo(World* w) override {
        auto* e = w->getEntity(entityHandle);
        if (e) if (auto* t = e->getComponent<Transform>()) t->rotation = oldRotation;
    }
    void redo(World* w) override {
        auto* e = w->getEntity(entityHandle);
        if (e) if (auto* t = e->getComponent<Transform>()) t->rotation = newRotation;
    }
    std::string description() const override { return "Rotate"; }
    void remapEntityHandle(EntityHandle oldH, EntityHandle newH) override {
        if (entityHandle == oldH) entityHandle = newH;
    }
};

// Scale entity (via ImGuizmo)
struct ScaleCommand : UndoCommand {
    EntityHandle entityHandle;
    Vec2 oldScale, newScale;

    void undo(World* w) override {
        auto* e = w->getEntity(entityHandle);
        if (e) if (auto* t = e->getComponent<Transform>()) t->scale = oldScale;
    }
    void redo(World* w) override {
        auto* e = w->getEntity(entityHandle);
        if (e) if (auto* t = e->getComponent<Transform>()) t->scale = newScale;
    }
    std::string description() const override { return "Scale"; }
    void remapEntityHandle(EntityHandle oldH, EntityHandle newH) override {
        if (entityHandle == oldH) entityHandle = newH;
    }
};

// Generic property change (stores full entity snapshot)
struct PropertyCommand : UndoCommand {
    EntityHandle entityHandle;
    nlohmann::json oldState, newState;
    std::string desc;

    void undo(World* w) override;
    void redo(World* w) override;
    std::string description() const override { return desc; }
    void remapEntityHandle(EntityHandle oldH, EntityHandle newH) override {
        if (entityHandle == oldH) entityHandle = newH;
    }
};

// Group multiple commands into a single undo step (e.g., brush stroke)
struct CompoundCommand : UndoCommand {
    std::vector<std::unique_ptr<UndoCommand>> commands;
    std::string desc;

    void undo(World* w) override {
        for (auto it = commands.rbegin(); it != commands.rend(); ++it)
            (*it)->undo(w);
    }
    void redo(World* w) override {
        for (auto& cmd : commands)
            cmd->redo(w);
    }
    std::string description() const override { return desc; }
    bool empty() const { return commands.empty(); }
    void remapEntityHandle(EntityHandle oldH, EntityHandle newH) override {
        for (auto& cmd : commands)
            cmd->remapEntityHandle(oldH, newH);
    }
};

#ifdef FATE_HAS_GAME
// UI widget property change (stores full screen JSON snapshot)
class UIManager; // forward decl
struct UIPropertyCommand : UndoCommand {
    std::string screenId;
    std::string oldJson, newJson;
    std::string nodeId;
    std::string desc;
    UIManager* uiMgr = nullptr;

    void undo(World* w) override;
    void redo(World* w) override;
    std::string description() const override { return desc; }
    UndoDomain domain() const override { return UndoDomain::UIScreen; }
};

// Lightweight UI widget move — patches offset in-place instead of replacing
// the entire screen tree (avoids pointer invalidation and use-after-free).
struct UIWidgetMoveCommand : UndoCommand {
    std::string screenId;
    std::string nodeId;
    Vec2 oldOffset, newOffset;
    UIManager* uiMgr = nullptr;

    void undo(World*) override;
    void redo(World*) override;
    std::string description() const override { return "Move UI Widget"; }
    UndoDomain domain() const override { return UndoDomain::UIScreen; }
};
#endif // FATE_HAS_GAME

// Undo/Redo stack
class UndoSystem {
public:
    static UndoSystem& instance() {
        static UndoSystem s;
        return s;
    }

    // Editor wires this in init(). Fires on push, undo, AND redo so that the
    // editor's dirty bookkeeping reflects the live state of the world rather
    // than just the last user-initiated edit. Without this, a Ctrl+S followed
    // by a Ctrl+Z would leave the reverted state unsavable.
    using DirtyCallback = std::function<void(UndoCommand*)>;
    void setDirtyCallback(DirtyCallback cb) { dirtyCb_ = std::move(cb); }

    void push(std::unique_ptr<UndoCommand> cmd) {
        // Clear redo stack when new action is performed
        redoStack_.clear();
        if (dirtyCb_) dirtyCb_(cmd.get());
        undoStack_.push_back(std::move(cmd));
        // Limit stack size
        if (undoStack_.size() > maxHistory_) {
            undoStack_.erase(undoStack_.begin());
        }
    }

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    void undo(World* w) {
        if (undoStack_.empty()) return;
        auto cmd = std::move(undoStack_.back());
        undoStack_.pop_back();
        cmd->undo(w);
        // Re-mark the same domain as dirty: the world now diverges from disk
        // again, just in the opposite direction from the original edit.
        if (dirtyCb_) dirtyCb_(cmd.get());
        redoStack_.push_back(std::move(cmd));
    }

    void redo(World* w) {
        if (redoStack_.empty()) return;
        auto cmd = std::move(redoStack_.back());
        redoStack_.pop_back();
        cmd->redo(w);
        if (dirtyCb_) dirtyCb_(cmd.get());
        undoStack_.push_back(std::move(cmd));
    }

    void clear() {
        undoStack_.clear();
        redoStack_.clear();
    }

    // After a command recreates an entity (new handle), update all other commands
    // in both stacks so they reference the new handle instead of the stale one.
    void remapHandle(EntityHandle oldH, EntityHandle newH) {
        for (auto& cmd : undoStack_)
            cmd->remapEntityHandle(oldH, newH);
        for (auto& cmd : redoStack_)
            cmd->remapEntityHandle(oldH, newH);
    }

    size_t undoCount() const { return undoStack_.size(); }
    size_t redoCount() const { return redoStack_.size(); }

    std::string lastUndoDesc() const {
        return undoStack_.empty() ? std::string{} : undoStack_.back()->description();
    }

private:
    UndoSystem() = default;
    std::vector<std::unique_ptr<UndoCommand>> undoStack_;
    std::vector<std::unique_ptr<UndoCommand>> redoStack_;
    size_t maxHistory_ = 200;
    DirtyCallback dirtyCb_;
};

} // namespace fate
