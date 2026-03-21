# Batch E: Pet System Wiring — Design Spec

**Date:** 2026-03-21
**Scope:** Wire existing pet system into gameplay — stat bonuses, equip/unequip commands, XP sharing, pet definition data.

---

## Problem
`pet_system.h` has full shared logic (definitions, instances, stat application, XP). `PetComponent` and `PetRepository` exist. Server loads/saves pets. But pet bonuses never reach CharacterStats, there are no network commands to equip/unequip pets, and pets don't gain XP.

## Design

### 1. Pet Definition Cache

Hardcoded `PetDefinitionCache` with starter pets. DB table deferred.

```cpp
struct PetDefinitionCache {
    // Returns nullptr if not found
    const PetDefinition* getDefinition(const std::string& petDefId) const;
    void addDefinition(const PetDefinition& def);
};
```

Starter pets (hardcoded at server init):
- `pet_wolf`: baseHP=10, baseCritRate=0.01, baseExpBonus=0.0, hpPerLevel=2, critPerLevel=0.002
- `pet_hawk`: baseHP=5, baseCritRate=0.02, baseExpBonus=0.05, hpPerLevel=1, critPerLevel=0.003
- `pet_turtle`: baseHP=20, baseCritRate=0.0, baseExpBonus=0.0, hpPerLevel=4, critPerLevel=0.0

### 2. Wire Pet Bonuses into recalcEquipmentBonuses()

In `server/server_app.cpp`, inside `recalcEquipmentBonuses()` (or wherever equipment bonuses are applied), after equipment stat application and before `recalculateStats()`:

```cpp
auto* petComp = player->getComponent<PetComponent>();
if (petComp && petComp->hasPet()) {
    auto* petDef = petDefCache_.getDefinition(petComp->equippedPet.petDefinitionId);
    if (petDef) {
        PetDefinition scaledDef = *petDef; // copy for level scaling
        charStats->stats.equipBonusHP += static_cast<int>(scaledDef.effectiveHP(petComp->equippedPet.level));
        charStats->stats.equipBonusCritRate += scaledDef.effectiveCritRate(petComp->equippedPet.level);
    }
}
```

Uses existing `PetDefinition::effectiveHP()` and `effectiveCritRate()` which scale by level.

### 3. Network Messages

`CmdPet` (client → server) — packet `0x24`:
- `uint8_t action` — 0=Equip, 1=Unequip
- `int32_t petDbId` — DB row ID of the pet to equip (from character_pets.id)

`SvPetUpdate` (server → client) — packet `0xAE`:
- `uint8_t equipped` — 1 if a pet is now equipped, 0 if unequipped
- `std::string petDefId` — definition ID (for client to look up sprite/name)
- `std::string petName`
- `uint8_t level`
- `int32_t currentXP`
- `int32_t xpToNextLevel`

### 4. Server Handlers

**processEquipPet():**
1. Validate: petDbId belongs to this character (query `PetRepository::loadPets()` or check component)
2. Unequip current pet if any (`petRepo_->unequipAllPets(charId)`)
3. Equip new pet (`petRepo_->equipPet(charId, petDbId)`)
4. Load pet data into `PetComponent::equippedPet`
5. `recalcEquipmentBonuses(player)` → applies pet + equipment stats
6. `sendPlayerState(clientId)` + send `SvPetUpdate`

**processUnequipPet():**
1. `petRepo_->unequipAllPets(charId)`
2. Clear `PetComponent::equippedPet`
3. `recalcEquipmentBonuses(player)` → removes pet bonuses
4. `sendPlayerState(clientId)` + send `SvPetUpdate` with equipped=0

### 5. Pet XP Sharing

In the mob kill handler (where XP is awarded to the player), after `charStats->stats.addXP(xp)`:

```cpp
auto* petComp = player->getComponent<PetComponent>();
if (petComp && petComp->hasPet()) {
    int petXP = static_cast<int>(xp * PetSystem::PET_XP_SHARE); // 50%
    bool leveled = PetSystem::addXP(petComp->equippedPet, petXP, charStats->stats.level);
    if (leveled) {
        recalcEquipmentBonuses(player); // pet stats changed
        sendPlayerState(clientId);
    }
    // Send SvPetUpdate with new XP
}
```

### Files to modify
- Create: `server/cache/pet_definition_cache.h` — Hardcoded pet definitions
- Modify: `engine/net/packet.h` — Add CmdPet (0x24), SvPetUpdate (0xAE)
- Modify: `engine/net/game_messages.h` — Add CmdPetMsg, SvPetUpdateMsg
- Modify: `server/server_app.h` — Add PetDefinitionCache, declare handlers
- Modify: `server/server_app.cpp` — processEquipPet, processUnequipPet, wire bonuses, XP sharing
- Modify: `engine/net/net_client.h/cpp` — Add onPetUpdate callback

## Testing Plan

| Test | Validates |
|---|---|
| PetDefinitionCache lookup by ID | Cache functionality |
| Unknown pet def returns nullptr | Cache miss |
| Pet bonuses apply to equipBonusHP/CritRate | Stat application |
| Pet unequip removes bonuses | Bonus clearing |
| Pet XP sharing: 50% of mob XP | XP calculation |
| Pet cannot outlevel player | Level cap |
| CmdPetMsg/SvPetUpdateMsg round-trip | Protocol |
