#include "server/dungeon_manager.h"
#include "server/server_spawn_manager.h"

// This translation unit exists so that the server target (which globs
// server/*.cpp) has a TU that includes the full ServerSpawnManager
// definition.  All DungeonManager methods are inline in the header.

namespace fate {

// Factory helper: creates a ServerSpawnManager for a dungeon instance.
// Called by server code when entering a dungeon scene.
std::shared_ptr<ServerSpawnManager> createDungeonSpawnManager() {
    return std::make_shared<ServerSpawnManager>();
}

} // namespace fate
