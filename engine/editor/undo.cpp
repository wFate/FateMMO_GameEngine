#include "engine/editor/undo.h"
#include "game/components/transform.h"
#include "game/components/sprite_component.h"

namespace fate {

void ResizeCommand::undo(World* w) {
    auto* e = w->getEntity(entityId);
    if (e) if (auto* s = e->getComponent<SpriteComponent>()) s->size = oldSize;
}

void ResizeCommand::redo(World* w) {
    auto* e = w->getEntity(entityId);
    if (e) if (auto* s = e->getComponent<SpriteComponent>()) s->size = newSize;
}

void PropertyCommand::undo(World* w) {
    auto* e = w->getEntity(entityId);
    if (!e) return;
    // Delete and recreate with old state
    w->destroyEntity(entityId);
    w->processDestroyQueue();
    auto* restored = PrefabLibrary::jsonToEntity(oldState, *w);
    if (restored) entityId = restored->id();
}

void PropertyCommand::redo(World* w) {
    auto* e = w->getEntity(entityId);
    if (!e) return;
    w->destroyEntity(entityId);
    w->processDestroyQueue();
    auto* restored = PrefabLibrary::jsonToEntity(newState, *w);
    if (restored) entityId = restored->id();
}

} // namespace fate
