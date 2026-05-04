#include "engine/editor/undo.h"
#include "engine/core/logger.h"
#ifdef FATE_HAS_GAME
#include "engine/ui/ui_manager.h"
#include "engine/components/transform.h"
#include "engine/components/sprite_component.h"
#include "game/systems/spawn_system.h"
#endif // FATE_HAS_GAME

namespace fate {

void ResizeCommand::undo(World* w) {
#ifdef FATE_HAS_GAME
    auto* e = w->getEntity(entityHandle);
    if (!e) return;
    if (auto* sz = e->getComponent<SpawnZoneComponent>()) { sz->config.size = oldSize; return; }
    if (auto* s = e->getComponent<SpriteComponent>()) s->size = oldSize;
#endif
}

void ResizeCommand::redo(World* w) {
#ifdef FATE_HAS_GAME
    auto* e = w->getEntity(entityHandle);
    if (!e) return;
    if (auto* sz = e->getComponent<SpawnZoneComponent>()) { sz->config.size = newSize; return; }
    if (auto* s = e->getComponent<SpriteComponent>()) s->size = newSize;
#endif
}

void PropertyCommand::undo(World* w) {
    auto* e = w->getEntity(entityHandle);
    if (!e) return;
    EntityHandle oldH = entityHandle;
    // Two-phase: build the replacement FIRST, then destroy the old entity.
    // If jsonToEntity fails we leave the original entity alone instead of
    // losing it entirely (prior to this fix, a malformed snapshot would
    // silently delete the entity with no rollback).
    auto* restored = PrefabLibrary::jsonToEntity(oldState, *w);
    if (!restored) {
        LOG_ERROR("Undo", "PropertyCommand::undo: failed to rebuild entity — keeping current state");
        return;
    }
    w->destroyEntity(entityHandle);
    w->processDestroyQueue("editor_undo_property");
    entityHandle = restored->handle();
    LOG_INFO("Undo", "PropertyCommand::undo '%s' rebuilt '%s' handle %u -> %u",
             desc.c_str(), restored->name().c_str(), oldH.index(), entityHandle.index());
    if (entityHandle != oldH)
        UndoSystem::instance().remapHandle(oldH, entityHandle);
}

void PropertyCommand::redo(World* w) {
    auto* e = w->getEntity(entityHandle);
    if (!e) return;
    EntityHandle oldH = entityHandle;
    auto* restored = PrefabLibrary::jsonToEntity(newState, *w);
    if (!restored) {
        LOG_ERROR("Undo", "PropertyCommand::redo: failed to rebuild entity — keeping current state");
        return;
    }
    w->destroyEntity(entityHandle);
    w->processDestroyQueue("editor_redo_property");
    entityHandle = restored->handle();
    LOG_INFO("Undo", "PropertyCommand::redo '%s' rebuilt '%s' handle %u -> %u",
             desc.c_str(), restored->name().c_str(), oldH.index(), entityHandle.index());
    if (entityHandle != oldH)
        UndoSystem::instance().remapHandle(oldH, entityHandle);
}

#ifdef FATE_HAS_GAME
void UIPropertyCommand::undo(World*) {
    if (uiMgr && !oldJson.empty()) {
        uiMgr->loadScreenFromString(screenId, oldJson);
    }
}

void UIPropertyCommand::redo(World*) {
    if (uiMgr && !newJson.empty()) {
        uiMgr->loadScreenFromString(screenId, newJson);
    }
}

void UIWidgetMoveCommand::undo(World*) {
    if (!uiMgr) return;
    auto* root = uiMgr->getScreen(screenId);
    if (!root) return;
    auto* node = root->findById(nodeId);
    if (node) node->anchor().offset = oldOffset;
}

void UIWidgetMoveCommand::redo(World*) {
    if (!uiMgr) return;
    auto* root = uiMgr->getScreen(screenId);
    if (!root) return;
    auto* node = root->findById(nodeId);
    if (node) node->anchor().offset = newOffset;
}
#endif // FATE_HAS_GAME

} // namespace fate
