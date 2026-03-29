#include "engine/editor/undo.h"
#include "engine/ui/ui_manager.h"
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/systems/spawn_system.h"

namespace fate {

void ResizeCommand::undo(World* w) {
    auto* e = w->getEntity(entityHandle);
    if (!e) return;
    if (auto* sz = e->getComponent<SpawnZoneComponent>()) { sz->config.size = oldSize; return; }
    if (auto* s = e->getComponent<SpriteComponent>()) s->size = oldSize;
}

void ResizeCommand::redo(World* w) {
    auto* e = w->getEntity(entityHandle);
    if (!e) return;
    if (auto* sz = e->getComponent<SpawnZoneComponent>()) { sz->config.size = newSize; return; }
    if (auto* s = e->getComponent<SpriteComponent>()) s->size = newSize;
}

void PropertyCommand::undo(World* w) {
    auto* e = w->getEntity(entityHandle);
    if (!e) return;
    w->destroyEntity(entityHandle);
    w->processDestroyQueue();
    auto* restored = PrefabLibrary::jsonToEntity(oldState, *w);
    if (restored) entityHandle = restored->handle();
}

void PropertyCommand::redo(World* w) {
    auto* e = w->getEntity(entityHandle);
    if (!e) return;
    w->destroyEntity(entityHandle);
    w->processDestroyQueue();
    auto* restored = PrefabLibrary::jsonToEntity(newState, *w);
    if (restored) entityHandle = restored->handle();
}

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

} // namespace fate
