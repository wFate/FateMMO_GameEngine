# Animation Windup System

Attacks use a short windup animation to hide network latency. When the player
presses attack, the client sends the message to the server **immediately** and
starts a ~300 ms animation. The predicted damage text and hit audio fire on the
animation's **hit frame** (~100 ms in), by which time the server response has
usually already arrived. The result feels instant even on high-latency
connections.

## How it works

```
t=0 ms   Button press
         ├─ Send attack to server
         └─ Start 3-frame attack animation (10 fps, non-looping)

t=0–100  Frame 0 — Windup
         Sprite pulls back 2 px away from target (procedural offset)

t=100    Frame 1 — Hit frame fires
         ├─ Predicted damage text spawns on target
         └─ Optimistic audio plays (hit_melee / hit_crit / miss)

t=100–   Server response typically arrives here
         Prediction resolved, kill SFX played if applicable

t=200    Frame 2 — Recover
         Sprite eases back to neutral position

t=300    Animation complete
         renderOffset reset to zero, attack state cleared
```

## Key files

| File | Role |
|------|------|
| `game/components/animator.h` | `AnimationDef` with `hitFrame`, `Animator` with event callbacks |
| `game/systems_backup/movement_system.h` | `AnimationSystem` — fires `onHitFrame` and `onComplete` |
| `game/components/sprite_component.h` | `renderOffset` for procedural lunge |
| `game/systems_backup/render_system.h` | Applies `renderOffset` at draw time |
| `game/systems/combat_action_system.h` | Windup flow, prediction deferral, lunge curve |
| `game/game_app.cpp` | Wires `onPlaySFX`, skill activation, server response handling |

## Animator API

### Registering an animation with a hit frame

```cpp
// addAnimation(name, startFrame, frameCount, fps, loop, hitFrame)
anim->addAnimation("attack", 0, 3, 10.0f, false, 1);
//                                              ^     ^
//                                         non-loop   fires event on frame 1
```

`hitFrame` is the 0-based frame index within the animation that triggers the
`onHitFrame` callback. Set to `-1` (default) for no event.

### Setting callbacks

```cpp
anim->onHitFrame = [this]() {
    // Called once when the animation reaches the hit frame
    spawnDamageText(pos, damage, isCrit);
    audioManager.playSFX("hit_melee");
};

anim->onComplete = [this, spr]() {
    // Called when a non-looping animation finishes its last frame
    spr->renderOffset = {0, 0};
};
```

Callbacks are **not** cleared automatically on `play()` — set them before each
`play()` call if you need different behavior per attack.

### Auto-transition

```cpp
anim->returnAnimation = "idle";
```

After a non-looping animation completes, `AnimationSystem` will automatically
call `play(returnAnimation)` if the named animation exists in the map. If it
doesn't exist, the animator just stops.

### Querying state

```cpp
anim->getLocalFrameIndex();  // 0-based index within current animation
anim->isFinished();          // true if non-looping and past last frame
```

## Procedural attack offsets (temporary)

Until real attack sprite frames exist, the windup feel comes from a positional
offset applied to `SpriteComponent::renderOffset`. This offset does **not**
affect the entity's `Transform` position — it's purely visual.

The offset curve differs by class (`attackIsMage_` flag):

### Melee (Warrior / Archer) — directional lunge

| Phase | Progress | Offset |
|-------|----------|--------|
| Windup | 0.00 – 0.33 | 0 to -2 px (pull back away from target) |
| Strike | 0.33 – 0.50 | -2 to +3 px (snap toward target) |
| Recover | 0.50 – 1.00 | +3 to 0 px (ease-out) |

Direction is a normalized vector from the player to the current target.

### Mage — stationary channeling settle

The mage stays in place and settles into a casting stance (TWOM-style: staff
held, eyes closed, energy gathering). No directional movement.

| Phase | Progress | Offset |
|-------|----------|--------|
| Settle | 0.00 – 0.33 | 0 to +1.5 px downward (ease-in, planting feet) |
| Channel | 0.33 – 0.80 | Hold at +1.5 px (channeling) |
| Release | 0.80 – 1.00 | +1.5 to 0 px (cast fires, return to neutral) |

The downward dip is a Y-only offset (positive Y = down in screen space). Skill
activations always use the mage channeling pose regardless of class.

### Tuning constants

In `CombatActionSystem`:

```cpp
static constexpr float kPullbackPx  = 2.0f;  // melee: windup pull-back distance
static constexpr float kLungePx     = 3.0f;  // melee: strike lunge distance
static constexpr float kChannelDipPx = 1.5f;  // mage: channeling settle depth
```

In `ensureAttackAnimation()`:

```cpp
anim->addAnimation("attack", baseFrame, 3, 10.0f, false, 1);
//                                      ^  ^^^^         ^
//                                 frames  fps      hitFrame
```

- **Frame count** controls total duration: 3 frames at 10 fps = 300 ms.
- **FPS** controls per-frame timing: 10 fps = 100 ms per frame.
- **hitFrame** controls when the impact fires: frame 1 = 100 ms into the animation.

## Upgrading to real sprites

When real attack sprite sheets are ready:

1. **Register the real animation** with the correct `startFrame`, `frameCount`,
   and `hitFrame` pointing to the impact frame in the sheet:

   ```cpp
   anim->addAnimation("attack", 8, 5, 12.0f, false, 2);
   //                           ^  ^  ^^^^         ^
   //                    sheet frame 8, 5 frames, 12fps, hit on frame 2
   ```

2. **Remove the procedural lunge.** Delete or zero out the offset code in
   `updateAttackLunge()` and the `renderOffset` assignments in
   `tryAttackTarget()` / `triggerAttackWindup()`. The real frames handle the
   visual motion.

3. **Remove `ensureAttackAnimation()`** — no longer needed since the animation
   is registered with real frame data.

4. **Optionally remove `SpriteComponent::renderOffset`** if no other system
   uses it.

The hit-frame event system, callbacks, and `AnimationSystem` event firing all
stay as-is — they work the same with real or placeholder frames.

**Animation Editor panel** (`Window > Animation Editor`) provides the full
authoring workflow: import individual frame PNGs, define animation states per
direction, set hit frames visually, preview playback, and pack into runtime
sprite sheets. Use `AnimationLoader` to load packed metadata at runtime. See
`Docs/superpowers/specs/2026-03-21-animation-editor-panel-design.md` for the
full spec.

## Audio timing

| Event | Where | What plays |
|-------|-------|------------|
| Hit frame (optimistic) | `CombatActionSystem::tryAttackTarget` | `hit_melee`, `hit_crit`, or `miss` based on client prediction |
| Server kill confirmed | `game_app.cpp` `onCombatEvent` | `kill` SFX |
| Remote attack (mob/player hitting us) | `game_app.cpp` `onCombatEvent` | Spatial `hit_melee` or `miss` |
| Skill hit frame | `triggerAttackWindup()` | None (skill results come from server) |
| Skill server result | `game_app.cpp` `onSkillResult` | `hit_skill` or `hit_melee` |

Local auto-attack audio is **optimistic** — it plays on the hit frame using
predicted results. If the prediction is wrong (e.g., client predicted hit but
server said miss), the sound has already played. This matches TWOM behavior
where the audio feel of connecting matters more than perfect accuracy.

## Skill activation

Skills use the same lunge animation but without client-side damage prediction.
`GameApp` calls `combatSystem_->triggerAttackWindup()` which plays the attack
animation with `onHitFrame = nullptr`. Damage text and audio for skills come
from the server via `SvSkillResultMsg`.
