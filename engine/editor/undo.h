#pragma once
#include "engine/ecs/world.h"
#include "engine/ecs/entity.h"
#include "engine/ecs/entity_handle.h"
#include "engine/ecs/prefab.h"
#include "engine/core/types.h"
#include "game/components/transform.h"
#include <vector>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace fate {

// Base undo command
struct UndoCommand {
    virtual ~UndoCommand() = default;
    virtual void undo(World* world) = 0;
    virtual void redo(World* world) = 0;
    virtual std::string description() const = 0;
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
};

// Resize entity
struct ResizeCommand : UndoCommand {
    EntityHandle entityHandle;
    Vec2 oldSize, newSize;

    void undo(World* w) override;
    void redo(World* w) override;
    std::string description() const override { return "Resize"; }
};

// Create entity (undo = delete, redo = recreate)
struct CreateCommand : UndoCommand {
    nlohmann::json entityData;
    EntityHandle createdHandle;

    void undo(World* w) override {
        if (createdHandle) { w->destroyEntity(createdHandle); w->processDestroyQueue(); }
    }
    void redo(World* w) override {
        auto* e = PrefabLibrary::jsonToEntity(entityData, *w);
        if (e) createdHandle = e->handle();
    }
    std::string description() const override { return "Create"; }
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
        if (deletedHandle) { w->destroyEntity(deletedHandle); w->processDestroyQueue(); }
    }
    std::string description() const override { return "Delete"; }
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
};

// Generic property change (stores full entity snapshot)
struct PropertyCommand : UndoCommand {
    EntityHandle entityHandle;
    nlohmann::json oldState, newState;
    std::string desc;

    void undo(World* w) override;
    void redo(World* w) override;
    std::string description() const override { return desc; }
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
};

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
};

// Undo/Redo stack
class UndoSystem {
public:
    static UndoSystem& instance() {
        static UndoSystem s;
        return s;
    }

    void push(std::unique_ptr<UndoCommand> cmd) {
        // Clear redo stack when new action is performed
        redoStack_.clear();
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
        redoStack_.push_back(std::move(cmd));
    }

    void redo(World* w) {
        if (redoStack_.empty()) return;
        auto cmd = std::move(redoStack_.back());
        redoStack_.pop_back();
        cmd->redo(w);
        undoStack_.push_back(std::move(cmd));
    }

    void clear() { undoStack_.clear(); redoStack_.clear(); }

    size_t undoCount() const { return undoStack_.size(); }
    size_t redoCount() const { return redoStack_.size(); }

    const std::string& lastUndoDesc() const {
        static std::string empty;
        return undoStack_.empty() ? empty : undoStack_.back()->description();
    }

private:
    UndoSystem() = default;
    std::vector<std::unique_ptr<UndoCommand>> undoStack_;
    std::vector<std::unique_ptr<UndoCommand>> redoStack_;
    size_t maxHistory_ = 200;
};

} // namespace fate
