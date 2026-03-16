#pragma once
#include "engine/ecs/world.h"
#include "engine/ecs/entity.h"
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
    EntityId entityId;
    Vec2 oldPos, newPos;

    void undo(World* w) override {
        auto* e = w->getEntity(entityId);
        if (e) if (auto* t = e->getComponent<Transform>()) t->position = oldPos;
    }
    void redo(World* w) override {
        auto* e = w->getEntity(entityId);
        if (e) if (auto* t = e->getComponent<Transform>()) t->position = newPos;
    }
    std::string description() const override { return "Move"; }
};

// Resize entity
struct ResizeCommand : UndoCommand {
    EntityId entityId;
    Vec2 oldSize, newSize;

    void undo(World* w) override;
    void redo(World* w) override;
    std::string description() const override { return "Resize"; }
};

// Create entity (undo = delete, redo = recreate)
struct CreateCommand : UndoCommand {
    nlohmann::json entityData;
    EntityId createdId = 0;

    void undo(World* w) override {
        if (createdId) { w->destroyEntity(createdId); w->processDestroyQueue(); }
    }
    void redo(World* w) override {
        auto* e = PrefabLibrary::jsonToEntity(entityData, *w);
        if (e) createdId = e->id();
    }
    std::string description() const override { return "Create"; }
};

// Delete entity (undo = recreate, redo = delete)
struct DeleteCommand : UndoCommand {
    nlohmann::json entityData;
    EntityId deletedId = 0;

    void undo(World* w) override {
        auto* e = PrefabLibrary::jsonToEntity(entityData, *w);
        if (e) deletedId = e->id();
    }
    void redo(World* w) override {
        if (deletedId) { w->destroyEntity(deletedId); w->processDestroyQueue(); }
    }
    std::string description() const override { return "Delete"; }
};

// Generic property change (stores full entity snapshot)
struct PropertyCommand : UndoCommand {
    EntityId entityId;
    nlohmann::json oldState, newState;
    std::string desc;

    void undo(World* w) override;
    void redo(World* w) override;
    std::string description() const override { return desc; }
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
