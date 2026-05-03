# API Reference

Audience: Engine Contributor

This reference is a practical index into the engine APIs most demo users and contributors touch. It is not a generated Doxygen replacement.

## Component Macros

Components use macros from `engine/ecs/component_registry.h`.

```cpp
FATE_COMPONENT(MyComponent)
FATE_COMPONENT_HOT(MyHotComponent)
FATE_COMPONENT_COLD(MyColdComponent)
```

Each macro supplies:

- `COMPONENT_NAME`
- `COMPONENT_TYPE_ID`
- `COMPONENT_TIER`
- `enabled`

The `enabled` field is intentionally part of serialized component layout.

## Component Metadata Registry

Header: `engine/ecs/component_meta.h`

Core API:

- `ComponentMetaRegistry::instance()`
- `registerComponent<T>()`
- `registerComponent<T>(customToJson, customFromJson)`
- `registerAlias(alias, canonical)`
- `findByName(name)`
- `findById(id)`
- `forEachMeta(fn)`

Use custom serializers when a component has runtime-only fields that cannot be represented directly as JSON. `SpriteComponent` does this for its live texture pointer.

## Demo Components

The public demo registration path currently registers:

| Component | Purpose |
| --- | --- |
| `Transform` | Position, rotation, and scale for scene entities. |
| `SpriteComponent` | Texture-backed 2D sprite rendering and sprite-sheet fields. |
| `TileLayerComponent` | Tile layer data for editor-authored maps. |

Registered in:

```text
engine/components/register_engine_components.h
```

## Current Full-Tree Registered Components

The local full tree currently registers these component names. Treat this list as a source snapshot, not a permanent contract.

Regenerate with:

```powershell
rg -o "registerComponent<[^>]+>" game\register_components.h engine\components\register_engine_components.h
```

Current unique registered names:

- `Animator`
- `AppearanceComponent`
- `ArenaNPCComponent`
- `BankerComponent`
- `BankStorageComponent`
- `BattlefieldNPCComponent`
- `BoxCollider`
- `CharacterFlagsComponent`
- `CharacterStatsComponent`
- `ChatComponent`
- `CollectionComponent`
- `CombatControllerComponent`
- `CostumeComponent`
- `CraftingNPCComponent`
- `CrowdControlComponent`
- `DamageableComponent`
- `DroppedItemComponent`
- `DungeonNPCComponent`
- `EnemyStatsComponent`
- `FactionComponent`
- `FriendsComponent`
- `GuardComponent`
- `GuildComponent`
- `GuildNPCComponent`
- `InnkeeperComponent`
- `InteractSiteComponent`
- `InventoryComponent`
- `LeaderboardNPCComponent`
- `MarketComponent`
- `MarketplaceNPCComponent`
- `MobAIComponent`
- `MobNameplateComponent`
- `NPCComponent`
- `NameplateComponent`
- `ParticleEmitterComponent`
- `PartyComponent`
- `PetComponent`
- `PetFollowComponent`
- `PlayerController`
- `PointLightComponent`
- `PolygonCollider`
- `PortalComponent`
- `QuestComponent`
- `QuestGiverComponent`
- `QuestMarkerComponent`
- `ShopComponent`
- `SkillManagerComponent`
- `SpawnPointComponent`
- `SpawnZoneComponent`
- `SpriteComponent`
- `StatusEffectComponent`
- `StoryNPCComponent`
- `TargetingComponent`
- `TeleporterComponent`
- `TileLayerComponent`
- `TradeComponent`
- `Transform`
- `ZoneComponent`

Many of these are full-game components. Public demo docs should not promise them unless they are included in the release package.

## ECS Storage

Header: `engine/ecs/archetype.h`

Important types:

- `ArchetypeId`
- `RowIndex`
- `ArchetypeColumn`
- `Archetype`
- `TypeInfo`
- `ArchetypeStorage`

Important methods:

- `registerType<T>()`
- `registerTypeById(cid, size, alignment)`
- `findOrCreateArchetype(typeIds)`
- `addEntity(archId, handle)`

Structural changes can migrate entities between archetypes. Re-fetch component pointers after add/remove/destroy operations.

## Prefabs

Headers:

- `engine/ecs/prefab.h`
- `engine/ecs/prefab_variant.h`

Use prefabs for authorable entity templates. Components must be registered in `ComponentMetaRegistry` to serialize and deserialize reliably.

## Asset Registry

Header: `engine/asset/asset_registry.h`

Important types:

- `AssetKind`
- `AssetState`
- `AssetLoader`
- `AssetSlot`
- `AssetHandle`

Important methods:

- `registerLoader(loader)`
- `load(path)`
- `loadAsync(path)`
- `processAsyncLoads(maxPerFrame)`
- `isReady(handle)`
- `isResolved(handle)`
- `pendingAsyncCount()`
- `queueReload(path)`
- `processReloads(currentTime)`
- `find(path)`
- `clear()`
- `setSource(source)`
- `readBytes(assetKey)`
- `readText(assetKey)`
- `recentReads()`

Threading rule: `queueReload` is thread-safe. Most other registry operations are main-thread-only.

## Texture Cache

Header: `engine/render/texture.h`

Important methods:

- `TextureCache::instance()`
- `load(path)`
- `get(path)`
- `clear()`
- `setVRAMBudget(bytes)`
- `vramBudget()`
- `estimatedVRAM()`
- `touch(path)`
- `evictIfOverBudget()`
- `entryCount()`
- `advanceFrame()`
- `placeholder()`
- `requestAsyncLoad(path)`
- `processUploads(maxPerFrame)`
- `hasPendingLoads()`

The default VRAM budget is 512 MB. Device recommendations are in `engine/platform/device_info.*`.

## Job System

Header: `engine/job/job_system.h`

Important types:

- `Job`
- `Counter`
- `CounterPool`
- `JobSystem`

Important methods:

- `JobSystem::instance()`
- `init(workerCount)`
- `shutdown()`
- `submit(jobs, count)`
- `submitFireAndForget(jobs, count)`
- `tryPushFireAndForget(job)`
- `waitForCounter(counter, target)`
- `fiberScratchArena()`

Worker fibers are for background work. Keep ECS mutation, editor state mutation, and GPU upload on the main thread unless a system explicitly documents otherwise.

## Device Info

Header: `engine/platform/device_info.h`

Important methods:

- `getPhysicalRAM_MB()`
- `getDeviceTier()`
- `recommendedVRAMBudget()`
- `getThermalState()`
- `getDisplayRefreshRate()`
- `recommendedFPS()`

Device tiers:

- Low: 200 MB VRAM budget.
- Medium: 350 MB VRAM budget.
- High: 512 MB VRAM budget.

## Networking

Headers:

- `engine/net/packet.h`
- `engine/net/game_messages.h`
- `engine/net/net_client.h`
- `engine/net/net_server.h`
- `engine/net/reliability.h`
- `engine/net/packet_crypto.h`

Current source protocol version:

```cpp
PROTOCOL_VERSION = 20
```

Do not duplicate large packet tables by hand without a regeneration command. Packet IDs and payloads change as the full game evolves.

