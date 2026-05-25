# Trials Fusion save-state Ghidra deep dive

Date: 2026-05-25

## Goal

Explore whether Trials Fusion can support TAS-style save states from the mod, and identify the smallest practical prototype before attempting full engine-state serialization.

## Short answer

A full emulator-style save state is probably not practical as a first target. The game state spans physics islands, task queues, replay buffers, checkpoint structures, timers, render/editor caches, and async task objects. Blind heap snapshots would be brittle and likely unsafe.

A practical first target is a "checkpoint-plus-physics snapshot":

1. Capture the current checkpoint pointer/index and race counters/timers.
2. Capture bike/rider scene-object transforms and velocities.
3. Restore by calling the engine's checkpoint respawn path, then reapply the captured physical pose/state.
4. Expand only after the minimal restore is stable.

This fits the engine paths already used by `TFPayload::Respawn`.

## Existing mod foothold

`TFPayload/respawn.cpp` already has the most important entry points:

- `g_pGameManager` global pointer:
  - Uplay RVA `0x104b308`
  - Steam RVA `0x104d308`
- active game/session manager:
  - `*(g_pGameManager + 0xdc)`
- bike array:
  - `manager + 0x2f0`
  - first bike pointer is currently read from `bikeArray + 0x14`
- checkpoint list:
  - `manager + 0x938`
- active bike checkpoint pointer:
  - `bike + 0x1dc`
- engine respawn function:
  - Uplay `HandlePlayerRespawn` at `0x00905ae0`, RVA `0x205ae0`
  - Steam currently mapped in code as RVA `0x205420`

Current checkpoint restore flow:

```cpp
*(void**)(bike + 0x1dc) = targetCheckpoint;
HandlePlayerRespawn(bike, 0, 0x802, (int)targetCheckpoint, 1);
```

Ghidra confirms this is an engine-native path rather than a purely synthetic teleport.

## Ghidra findings

### `HandlePlayerRespawn` at Uplay `0x00905ae0`

Prototype from decompile:

```cpp
void __thiscall HandlePlayerRespawn(void* bike, char isMpRespawn, uint flags, int checkpoint, char force);
```

Observed behavior:

- blocks unless the bike respawn flag at `bike+0x138` allows it or `force != 0`;
- sets `bike+0x138 |= 0x40`;
- clears a game-state byte through `ClearByteAt0x2FA(*(g_pGameManager+0xdc))`;
- if `flags & 0x20`, it derives the checkpoint from `bike+0x1dc`;
- allocates a 0x14-byte message object:
  - vtable at `DAT_014827d8`;
  - `+0x04` stores a secondary command/flag;
  - `+0x08` is set to `1`;
  - `+0x0c` stores respawn flags;
  - `+0x10` stores checkpoint pointer;
- queues the message through `SendMessage(*(g_pGameManager+0x100), msg, 3, 0)`.

Implication:

The existing mod flow is good for "restore to checkpoint". It queues normal engine work instead of mutating every physics/task subsystem directly.

### `ResetAllRidersToCheckpoint`

Decompile shape:

```cpp
void __thiscall ResetAllRidersToCheckpoint(void* riderPool, void* checkpoint);
```

Key writes/calls:

- for active bike/rider slots:
  - writes `slot+0x1dc = checkpoint`;
  - calls `CleanupSceneGeometry(slot)`;
  - calls `InitializeRiderForCheckpoint(slot, 0, 0)`;
  - calls `InitializeCheckpointTask(slot)`.

Implication:

This path is broader than `HandlePlayerRespawn` and may be useful for multiplayer/all-rider restore, but it is heavier and may disrupt more state.

### `ResetGameState`

`ResetGameState(manager, flags, checkpoint)` is large and high-risk. It touches:

- manager flags around `+0x8e5d`, `+0x8e60`, `+0x8e64`;
- checkpoint/segment selection;
- collision/physics rebuild;
- rider initialization;
- race/session transition state;
- graphics/render cleanup;
- environment settings;
- async tasks and checksums.

Important flag observations from decompile:

- checks `flags & 0x10`, `0x20`, `0x40`, `0x80`, `0x800`, `0x1000`, `0x2000`, `0x8000`;
- calls `ResetAllRidersToCheckpoint` when mode/state conditions line up;
- calls `HandleGameStateTransition(..., 7)` in multiple branches.

Implication:

Do not use `ResetGameState` directly for first save-state work unless we map the flag contract carefully. It is a session reset primitive, not a narrow state restore primitive.

### `HandleSessionEvent`

Important cases:

- event `4`: when replay/session mode byte at `this+0x19b0` is `2`, calls `apply_player_checkpoint_restart(this)`;
- event `0x0c`: calls `rebuild_replay_frame_queue(this)`;
- event `0x12`: pops a state stack entry, calls `handle_replay_frame_state(this)`, then adjusts elapsed time offsets for active players.

Implication:

The replay subsystem has its own notion of frame state and time correction. We should not assume restoring the bike pose alone will make replay/ghost/session state internally consistent.

### Replay frame buffer

`rebuild_replay_frame_queue(this)` shows a ring-buffer-like structure:

- state/mode byte: `this+0x19b0`;
- source frame count: `this+0x1a14`;
- source frame base: `this+0x1a1c`;
- frame stride: `0x90`;
- ring capacity/mask: `this+0x1aa0`;
- ring pointer table: `this+0x1aa4`;
- head: `this+0x1aa8`;
- tail/count-ish: `this+0x1aac`;
- current frame-ish: `this+0x1ac0`.

`handle_replay_frame_state(this)` reads frame records from the ring and switches on byte `frame+0x10`.

Implication:

Replay frames may become a useful source for historical bike/rider state. They are not enough alone for a full save state, but they may contain already-sampled physical state in 0x90-byte chunks.

## Scene-object physical state

Useful decompiled functions:

### `SetSceneObjectPosition`

Writes:

- physics/state struct at `sceneObject+0xa0`:
  - `+0x20..0x28` = position vector
- live body/object at `sceneObject+0xa8`, if non-null and `sceneObject+0xb0 == 0`:
  - `+0x150..0x158` = position vector

### `SetSceneObjectVelocity`

Writes:

- physics/state struct at `sceneObject+0xa0`:
  - `+0x2c..0x34` = velocity vector
- live body/object at `sceneObject+0xa8`, if non-null and `sceneObject+0xb0 == 0`:
  - `+0x160..0x168` = velocity vector

### Orientation / angular state

`reset_rider_physics_state` directly manipulates live transform matrices:

- if `sceneObject+0xb0 == 0`, live transform/body pointer is `sceneObject+0xa8`;
- matrix rows/transform data are written at live body offsets:
  - `+0x10..0x3c`
  - `+0x40..0x48`

`get_rigid_body_angular_velocity` is currently named misleadingly. It derives a quaternion from the live body's matrix at `sceneObject+0xa8+0x10`, or falls back to `GetWorldQuaternion`.

Implication:

For a useful save-state restore, position and velocity are not enough. We need orientation, and probably angular velocity or equivalent matrix state. The engine exposes enough write patterns to do this, but we need a small capture/restore test around the rider/bike scene objects.

## Recommended prototype

### Phase 1: capture and restore a single in-race slot

Capture:

- current checkpoint index and pointer:
  - `Respawn::GetCurrentCheckpointIndex()`;
  - `bike+0x1dc`;
- fault counter:
  - existing `Respawn::GetFaultCount()`;
- race time/session timer candidates:
  - current mod reads `bike + 0x898` for faults;
  - Ghidra shows `ResetGameState` and `HandleSessionEvent` touching timer/task fields, but timer capture needs a separate pass;
- bike/rider scene objects:
  - identify via existing bike pointer/rider scene root;
  - collect type `3,1` objects, matching `SceneObject_CollectByType(root, out, 3, 1, 0, 0)`;
  - for each object, capture:
    - scene object pointer or stable hash/id;
    - position;
    - velocity;
    - live matrix/quaternion;
    - enabled/active flags if they affect physics.

Restore:

1. Disable input or run on the main/game update hook where the current bike-swap code already stages work.
2. Call `RespawnAtCheckpointIndex(capturedIndex)` or write `bike+0x1dc` then call `HandlePlayerRespawn`.
3. After the respawn message has been processed, reapply captured object transforms/velocities.
4. Restore faults/time only after physical state is stable.

### Phase 2: make the restore deterministic enough for repeated trials

Add instrumentation:

- dump `bike+0x1dc`, checkpoint index, faults, and a small transform checksum before capture;
- dump the same after restore;
- log the count and identities/hashes of captured scene objects;
- verify the restore object count matches before applying transforms.

### Phase 3: investigate replay-assisted rewind

The 0x90-byte replay frame records may give a better rolling-history source than our own snapshot code. Ghidra targets:

- `HandleSessionEvent`
- `rebuild_replay_frame_queue`
- `handle_replay_frame_state`
- `collect_ghost_velocities`

Open question:

Can replay frames be used to reconstruct live bike/rider pose, or are they only for ghost/render playback?

## Risks

- Pointer identity may not survive respawn. Store stable object identifiers/hashes where possible, not only raw pointers.
- Applying transform too early may be overwritten by the queued respawn task.
- Reapplying only bike transforms may leave rider ragdoll joints or constraints inconsistent.
- Timer/replay/session queues can diverge from physical state.
- Multiplayer/all-rider paths likely need `ResetAllRidersToCheckpoint` or a per-slot loop rather than the single-bike helper.

## Next Ghidra targets

1. Find the concrete scene root from the active bike/rider pointer.
2. Identify stable object IDs/hashes for bike/rider rigid bodies.
3. Rename and prototype the duplicate/misleading getters around:
   - `GetSceneObjectPosition`
   - `SetSceneObjectPosition`
   - `SetSceneObjectVelocity`
   - `get_rigid_body_velocity`
   - `get_rigid_body_angular_velocity`
4. Trace the 0x14-byte respawn message vtable at `DAT_014827d8` to its handler.
5. Map race timer and fault storage writes around checkpoint/respawn events.

## Second pass findings

Second pass focus: determine whether the engine already has a narrower "move rider/bike while preserving pose state" path we can reuse instead of inventing one.

### Steam mappings confirmed from CSV

- `ProcessRespawnCommand`
  - Uplay `0x008bb690`
  - Steam `0x002fb3d0`
- `HandlePlayerRespawn`
  - Uplay `0x00905ae0`
  - Steam `0x00345420`
- `InitializeRiderPhysicsAndVisuals`
  - Uplay `0x00929cc0`
  - Steam `0x00369590`
- `GetSceneObjectPosition`
  - Uplay `0x0090bd20`
  - Steam `0x0034b5d0`
- `GetWorldQuaternion`
  - Uplay `0x00da2590`
  - Steam `0x007e0f80`
- `CollectValidSceneObjects`
  - Uplay `0x00c413f0`
  - Steam `0x00680a20`

### `ProcessRespawnCommand`

`ProcessRespawnCommand(command)` starts by sampling race/session time:

```cpp
command+0x08 = ExecuteTaskWithLocking(manager + 0x14);
```

If `command+0x70 != 0`, it handles subcommand `command+0xa4`:

- `0`: calls a small reset helper on the first bike.
- `1`: sets a respawn flag on the first bike.
- `2`: checkpoint/frame-index style restore:
  - selects a checkpoint-like list entry using `command+0xa8`;
  - if `command+0xac == 0`, calls:

```cpp
ApplyTrackPlaybackFrameToBikeAndCamera(firstBike, selectedCheckpoint, command+0xad, 1);
```

  - otherwise calls `HandlePlayerRespawn(firstBike, 0, flags, selectedCheckpoint, 0)`.
- `3`: distance/time-within-checkpoint style restore:
  - computes a target position along checkpoint segment data;
  - if `command+0xac == 0`, calls:

```cpp
update_rider_position_and_animation(firstBike, targetPosition, command+0xad);
```

  - otherwise calls `HandlePlayerRespawn(...)` and queues a follow-up message with the target position.
- `4`: physics reset:

```cpp
reset_rider_physics_state(firstBike, command+0xad, command+0xae);
```

Implication:

There is already an engine-level command path for non-checkpoint repositioning, and it exposes two useful control bytes:

- `command+0xac`: choose direct playback-position update vs respawn path.
- `command+0xad`: preserve/reapply dynamic object state in the direct update path.

This is probably the best next implementation target.

### `ApplyTrackPlaybackFrameToBikeAndCamera`

Prototype from decompile:

```cpp
void __thiscall ApplyTrackPlaybackFrameToBikeAndCamera(
    void* bike,
    void* checkpointOrPlaybackFrame,
    char preserveDynamicObjects,
    int trackEvalMode
);
```

When `preserveDynamicObjects != 0`, it:

1. Collects type `3,1` objects under `bike+0x678`.
2. Captures each object's current velocity via `get_rigid_body_velocity`.
3. Captures each object's ragdoll/bone position via `GetRagdollBonePosition`.
4. Repositions the bike/root using:
   - `UpdateAnimationFramePosition`;
   - `CalculateTrackPosition`;
   - `CalculateTrackTangent`;
   - virtual transform writes on a hashed scene object.
5. Re-collects type `3,1` objects under `bike+0x678`.
6. Reapplies:

```cpp
SetSceneObjectPosition(object, capturedBonePosition);
SetSceneObjectVelocity(object, capturedVelocity);
```

Implication:

This is a built-in "move the bike along the track, but keep dynamic physical state" primitive. It does not appear to preserve full orientation/angular velocity per object, but it is much closer to a save-state restore than a plain checkpoint respawn.

For a first prototype, we should either:

- call this function directly after selecting a target checkpoint/frame; or
- copy its capture/reapply pattern and add matrix/quaternion restoration.

### `update_rider_position_and_animation`

This function is the distance/time-within-checkpoint sibling of `ApplyTrackPlaybackFrameToBikeAndCamera`.

When its `preserveDynamicObjects` argument is nonzero, it uses the same pattern:

1. collect type `3,1` objects under `bike+0x678`;
2. capture velocity and ragdoll/bone position;
3. compute a new root position from track data, raycasts, and animation frame state;
4. update the root transform;
5. reapply captured object positions and velocities.

Implication:

This path may be useful for a later "restore to exact progress along checkpoint segment" state. For now, checkpoint-index restore is simpler.

### Scene root and stable lookup

`FindSceneObjectByHash(bike, &hash)` uses:

- bike scene-tree root/cache at `bike+0x134`;
- hash-table storage around `bike+0x18`;
- recursive search through child objects if the cache misses.

This means hashes are preferable to raw scene-object pointers when restoring after respawn/rebuild.

Important hashes seen in rider/bike initialization and playback code:

- `0x1329aad5`
  - primary pose/camera/center object used by `GetSceneObjectPosition`;
  - live body matrix sampled for `bike+0xd0..0xe8`.
- `0x5bd7085d`
  - secondary object sampled into `bike+0xec..0xf4`.
- `0x330b811d`
  - another secondary object sampled into `bike+0xf8..0x100`.
- `0x73d39136`
  - object used when applying track position/tangent.
- `0x3a05dccc`
  - root used to collect one type `3,1` group for collision filter setup.
- `0x4ff08f15`
  - root used to collect another type `3,1` group for collision filter setup.

Open naming:

The exact bike-vs-rider meaning of `0x3a05dccc` and `0x4ff08f15` still needs runtime logging or string-hash recovery.

### `InitializeRiderPhysicsAndVisuals`

This function is too broad to use directly for save-state restore. It resets or rebuilds many fields:

- clears `bike+0x138`, `bike+0xb0`, many state vectors/counters;
- calls `CleanupRenderResources`;
- calls `CleanupSceneGeometry`;
- calls `LoadSceneObjectsFromStream`;
- rebuilds child scene objects;
- updates animation frame position;
- computes checkpoint track position/tangent;
- updates transparency, collision filters, ragdoll constraints, visual/audio state;
- finally clears the respawn-in-progress bit:

```cpp
bike+0x138 &= ~0x40;
```

However, it reveals important stable state:

- current checkpoint index lives at `bike+0x8d0`;
- current checkpoint pointer/record lives at `bike+0x8d4`;
- primary pose state cache:
  - `bike+0xd0..0xd8` position;
  - `bike+0xdc..0xe8` quaternion-ish orientation;
- secondary sampled object state:
  - `bike+0xec..0xf4`;
  - `bike+0xf8..0x100`.

Implication:

After a restore, verify both scene-object state and these bike-level cached pose fields. If they diverge, downstream camera, animation, or checkpoint logic may snap back.

### Playback/session writers

`WriteAllBikeCameraPosePacket_CPC0` and `WriteNearbyObjectStatePacket_STA0` are not direct save-state serializers, but they show how the engine samples pose/camera state for replay/network-ish output.

`WriteAllBikeCameraPosePacket_CPC0`:

- iterates checkpoint/list entries under `manager+0x938`;
- calls `ApplyTrackPlaybackFrameToBikeAndCamera(firstBike, entry, 0, 1)`;
- samples camera/entity pose through virtual methods at offsets `0x30` and `0x34`;
- writes packet tag `CPC0` (`0x30435043`).

`WriteNearbyObjectStatePacket_STA0`:

- applies the first checkpoint/list entry to the bike;
- samples camera pose;
- gathers nearby scene objects by spatial proximity;
- writes packet tag `STA0`.

Implication:

These writers are useful references for state layout and camera pose sampling, but they intentionally call `ApplyTrackPlaybackFrameToBikeAndCamera(..., preserveDynamicObjects=0)`, so they are not the preserve-physics path.

## Revised prototype recommendation

Phase 1 should be narrower than the original first-pass plan:

1. Add a debug-only save-state module with one slot.
2. On capture, store:
   - checkpoint index: `bike+0x8d0`;
   - checkpoint pointer: `bike+0x8d4` and/or `bike+0x1dc`;
   - race time sample from `ExecuteTaskWithLocking(manager+0x14)`;
   - fault counter through existing mod code;
   - `bike+0xd0..0x100` pose cache;
   - collected type `3,1` dynamic object hashes/positions/velocities/matrix rows.
3. On restore:
   - first test direct call to `ApplyTrackPlaybackFrameToBikeAndCamera(bike, checkpoint, 1, 1)`;
   - then restore `bike+0xd0..0x100`;
   - then reapply collected object transforms/velocities by hash lookup.
4. Only fall back to `HandlePlayerRespawn` if direct playback-frame restore leaves the bike in a bad state.

Critical test:

Compare these paths side-by-side:

- A: current `RespawnAtCheckpointIndex`.
- B: `ApplyTrackPlaybackFrameToBikeAndCamera(..., preserveDynamicObjects=1)`.
- C: `RespawnAtCheckpointIndex`, then delayed transform reapply one update tick later.

Expected outcome:

Path B should be the least disruptive if the target is a checkpoint/list entry and if the captured state is in the same track/session.

## Third pass findings

Third pass focus: turn the second-pass restore idea into a concrete implementation contract for `TFPayload`, and identify what should not be attempted yet.

### Function addresses for likely prototype calls

Additional Steam mappings:

- `UpdateBikePositionsOnTrack`
  - Uplay `0x00937020`
  - Steam `0x00376760`
- `UpdateBikeCheckpointPositions`
  - Uplay `0x00937f60`
  - Steam `0x00377710`
- `UpdateAnimationFramePosition`
  - Uplay `0x009060d0`
  - Steam `0x00345a10`
- `update_rider_position_and_animation`
  - Uplay `0x00928120`
  - Steam `0x003679f0`
- `FindSceneObjectByHash`
  - Uplay `0x0090a180`
  - Steam `0x00349a30`
- `find_scene_object_by_hash_recursive`
  - Uplay `0x00da2200`
  - Steam `0x007e0bf0`
- `SetRagdollCollisionFilters`
  - Uplay `0x00d5c5a0`
  - Steam `0x0079b1a0`
- `GetRagdollBonePosition`
  - Uplay `0x00d59430`
  - Steam `0x007980a0`
- `get_rigid_body_angular_velocity`
  - Uplay `0x00d595d0`
  - Steam `0x00798240`

`ApplyTrackPlaybackFrameToBikeAndCamera` appears in the CSV as `UpdateBikeAndCameraFromTrack`:

- Uplay `0x00918690`, RVA `0x00218690`;
- Steam `0x00357f80`, RVA `0x00217f80`.

### Time and fault restore is already mostly solved

The existing `Respawn` module already wraps the encrypted counters we need:

- race time:
  - struct pointer: `manager + 0x14`;
  - read: `ExecuteTaskWithLocking(manager + 0x14)`;
  - write: `ExecuteAsyncTask(manager + 0x14, value)`.
- faults:
  - struct pointer: `bike + 0x898`;
  - read: `ExecuteTaskWithLocking(bike + 0x898)`;
  - write: `ExecuteAsyncTask(bike + 0x898, value)`.

Ghidra confirms the helper behavior:

```cpp
ExecuteTaskWithLocking(ptr)
```

- acquires the task lock at `ptr+8`;
- decrypts 4 bytes using the task hash/key;
- returns the plaintext value.

```cpp
ExecuteAsyncTask(ptr, value)
```

- takes the byte count from `*ptr`;
- takes the encrypted/task metadata from `ptr+4`;
- reads the new value from the stack argument;
- writes it through the same encrypted task path.

Implication:

Save-state code should call the existing public helpers:

- `Respawn::GetRaceTimeMs()`
- `Respawn::SetRaceTimeMs(value)`
- `Respawn::GetFaultCount()`
- `Respawn::SetFaultCounterValue(value)`

No new encrypted-counter reverse engineering is required for the first prototype.

### Do not synthesize `ProcessRespawnCommand` yet

`ProcessRespawnCommand` has no normal xrefs in the current Ghidra query, which strongly suggests it is invoked through a virtual dispatch table, message pump, or callback registration. It also expects a large command object with fields as far as `+0xb0`.

Known fields:

- `+0x08`: filled with current time by `ProcessRespawnCommand`;
- `+0x70`: enables subcommand handling;
- `+0x94`: filled with current time when `+0x70 != 0`;
- `+0xa4`: subcommand selector;
- `+0xa8`: checkpoint/frame/time parameter;
- `+0xac`: direct playback update vs respawn path;
- `+0xad`: preserve dynamic object state flag;
- `+0xae`: secondary physics reset flag;
- `+0xaf`, `+0xb0`: additional mode flags used in respawn variants.

Implication:

It is tempting to allocate and queue a native command object, but that is a bad first implementation because we do not yet know the constructor, vtable, ownership, or queue contract. A direct call to the inner helper is lower risk.

Recommended first call target:

```cpp
ApplyTrackPlaybackFrameToBikeAndCamera(bike, checkpoint, 1, 1);
```

Fallback only if needed:

```cpp
Respawn::RespawnAtCheckpointIndex(index);
// then apply captured pose one or more update ticks later
```

### Object identity: record two hashes, not just one pointer

`find_scene_object_by_hash_recursive(sceneObject, &hash)` returns a match if either:

```cpp
sceneObject + 0x38 == hash
```

or:

```cpp
*(sceneObject + 0x28) + 0x08 == hash
```

The parent/root lookup `FindSceneObjectByHash(bike, &hash)` also caches misses and hits through the bike-local hash table around `bike+0x18`.

Implication:

A dynamic-object snapshot entry should not rely on raw pointers. It should record:

- raw pointer at capture time, for same-frame fast path;
- `sceneObject+0x38` hash;
- `(*(sceneObject+0x28)+0x08)` hash when readable;
- optional traversal index among collected type `3,1` objects as a fallback;
- object kind/type word at `sceneObject+0x08`;
- child count / parent pointer for diagnostics.

Restore matching order:

1. if raw pointer is still readable and hashes match, use it;
2. else `FindSceneObjectByHash(bike, hash38)`;
3. else `FindSceneObjectByHash(bike, hashFromDescriptor)`;
4. else fall back to the Nth collected type `3,1` object only in debug mode, with a warning.

### Position and velocity function naming correction

The first two passes used engine names as Ghidra labels, but the duplicate functions reveal a naming trap:

- `get_rigid_body_velocity(sceneObject, out)` reads:
  - fallback state: `sceneObject+0xa0 + 0x20..0x28`;
  - live body: `sceneObject+0xa8 + 0x150..0x158`.
- `GetRagdollBonePosition(sceneObject, out)` reads:
  - fallback state: `sceneObject+0xa0 + 0x2c..0x34`;
  - live body: `sceneObject+0xa8 + 0x160..0x168`.
- `SetSceneObjectPosition(sceneObject, vec3)` writes:
  - `sceneObject+0xa0 + 0x20..0x28`;
  - live body `+0x150..0x158`.
- `SetSceneObjectVelocity(sceneObject, vec3)` writes:
  - `sceneObject+0xa0 + 0x2c..0x34`;
  - live body `+0x160..0x168`.

So the Ghidra label `GetRagdollBonePosition` appears to pair with `SetSceneObjectVelocity`, while `get_rigid_body_velocity` appears to pair with `SetSceneObjectPosition`. The engine's own `ApplyTrackPlaybackFrameToBikeAndCamera` does this:

```cpp
capturedA = get_rigid_body_velocity(object);
capturedB = GetRagdollBonePosition(object);
...
SetSceneObjectPosition(object, capturedB);
SetSceneObjectVelocity(object, capturedA);
```

Implication:

For implementation, name these fields by offsets, not by current Ghidra names:

- `state20_live150_vec3`
- `state2c_live160_vec3`

Then add semantic names only after runtime testing confirms which is position vs velocity in the current object type.

### Collision/activation state to capture

`SetRagdollCollisionFilters(sceneObject, a, b)` writes:

- `sceneObject+0x84` = low 16 bits of `a`;
- `sceneObject+0x86` = low 16 bits of `b`;
- `sceneObject+0xa0+0x5c` = `a`;
- `sceneObject+0xa0+0x60` = `b`;
- if `sceneObject+0xb2 & 1`, it activates/updates the physics body around the write.

Implication:

The first prototype can skip collision filters if it only tests same-session checkpoint restore. But a robust save-state entry should eventually capture:

- `sceneObject+0x84`;
- `sceneObject+0x86`;
- `sceneObject+0xb2`;
- `sceneObject+0xa0+0x5c`;
- `sceneObject+0xa0+0x60`.

This matters if restore happens after a reset path that rebuilds collision filters.

### Checkpoint-position update path

`UpdateBikeCheckpointPositions(bike, optionalPosition, flags, param4)` computes per-checkpoint track/spline progress and then calls:

```cpp
UpdateBikePositionsOnTrack(bike, param4);
```

`UpdateAnimationFramePosition(bike, frame)` walks children under `bike+0x134`, applies animation frame state, and updates:

- `bike+0x9a4`;
- `bike+0x9a8`.

Implication:

If direct `ApplyTrackPlaybackFrameToBikeAndCamera` restore works, we probably do not need to call these separately. If we implement manual restore without the native helper, these are required to keep checkpoint/spline/camera frame state coherent.

### Revised implementation sketch

Add a new `TFPayload/save-state.{h,cpp}` with a single in-memory slot and no persistence.

Capture state:

```cpp
struct SaveStateObject {
    uintptr_t capturedPtr;
    uint32_t hash38;
    uint32_t descriptorHash;
    uint16_t typeWord;
    float state20_live150[3];
    float state2c_live160[3];
    float liveMatrix10_48[14]; // optional, raw floats from live body +0x10..+0x48
    uint32_t collision5c;
    uint32_t collision60;
    uint16_t collision84;
    uint16_t collision86;
    uint8_t flagsB0;
    uint8_t flagsB2;
};

struct SaveStateSlot {
    bool valid;
    int checkpointIndex;
    void* checkpointPtr1dc;
    void* checkpointPtr8d4;
    int raceTimeMs;
    int faults;
    uint8_t bikePoseCacheD0_100[0x34]; // dword-inclusive bike+0xd0..0x100
    std::vector<SaveStateObject> objects;
};
```

Practical simplification:

Use a fixed-size array instead of `std::vector` if we want to avoid allocator surprises inside the injected DLL. `ApplyTrackPlaybackFrameToBikeAndCamera` stack buffers allow up to 4092 collected objects, but a sane prototype can cap at 256 or 512 and log truncation.

Restore state:

1. Validate slot and current track/session:
   - checkpoint count still matches or checkpoint index is valid;
   - current bike pointer is readable;
   - collected object count is close to captured count.
2. Resolve `checkpoint = GetCheckpointAtIndex(index)`.
3. Call direct helper:

```cpp
ApplyTrackPlaybackFrameToBikeAndCamera(bike, checkpoint, 1, 1);
```

4. Re-resolve each object by hash and write captured offset-based vectors.
5. Restore `bike+0xd0..0x100`.
6. Restore faults and time last:

```cpp
Respawn::SetFaultCounterValue(slot.faults);
Respawn::SetRaceTimeMs(slot.raceTimeMs);
```

7. Log before/after checksums.

### Runtime validation checklist

For each restore strategy, log:

- current checkpoint index and pointer before capture;
- checkpoint index and pointer after restore;
- `Respawn::GetRaceTimeMs()`;
- `Respawn::GetFaultCount()`;
- object count from `SceneObject_CollectByType(bike+0x678, ..., 3, 1, 0, 0)`;
- first 16 object hashes and both offset-vector triples;
- `bike+0xd0..0x100` before capture, after native helper, after final patch-up.

Run three test cases:

1. stationary at checkpoint;
2. mid-air with rotation;
3. crashing/ragdoll transition.

Expected result:

- stationary checkpoint should work first;
- mid-air will reveal whether matrix/angular state must be restored;
- ragdoll transition will reveal whether constraints/collision filters must be captured.

### Current risk ranking

Lowest risk:

- capture/restore time and faults using existing `Respawn` helpers;
- call direct playback-frame helper by function pointer;
- restore same-session checkpoint/list entry.

Medium risk:

- reapply object vectors by hash after the native helper;
- restore `bike+0xd0..0x100` pose cache;
- capture collision filters.

High risk:

- allocate/queue synthetic `ProcessRespawnCommand` objects;
- use `ResetGameState`;
- support cross-track or persistent save-state files;
- support multiplayer slots before single-player is stable.

## Fourth pass findings

Fourth pass focus: resolve the direct helper mapping, clarify the call ABI for a prototype, and identify the first orientation/matrix patch-up targets.

### Direct helper mapping resolved

The function we were calling `ApplyTrackPlaybackFrameToBikeAndCamera` is present in the CSV under a better name:

```text
UpdateBikeAndCameraFromTrack
```

Mapping:

- Uplay Ghidra address: `0x00918690`
- Uplay runtime RVA: `0x00218690`
- Steam Ghidra address: `0x00357f80`
- Steam runtime RVA: `0x00217f80`

The Uplay Ghidra function at `0x00918690` decompiles as:

```cpp
void __thiscall ApplyTrackPlaybackFrameToBikeAndCamera(
    void* bike,
    void* checkpointOrTrackFrame,
    char preserveDynamicObjects,
    int trackEvalMode
);
```

Recommended typedef:

```cpp
using UpdateBikeAndCameraFromTrackFunc =
    void(__thiscall*)(void* bike, void* checkpointOrTrackFrame, char preserveDynamicObjects, int trackEvalMode);
```

Runtime init:

```cpp
static constexpr uintptr_t UPDATE_BIKE_AND_CAMERA_FROM_TRACK_RVA_UPLAY = 0x00218690;
static constexpr uintptr_t UPDATE_BIKE_AND_CAMERA_FROM_TRACK_RVA_STEAM = 0x00217f80;
```

First call to test:

```cpp
g_updateBikeAndCameraFromTrack(bike, checkpoint, 1, 1);
```

Where:

- `bike` is the current first rider/bike pointer from existing `Respawn::GetBikePointer()`;
- `checkpoint` is the checkpoint/list entry from `manager+0x938`;
- `preserveDynamicObjects = 1` enables the capture/reapply of the two vec3 state groups;
- `trackEvalMode = 1` matches replay/network writers and direct respawn command case `2`.

### What the helper actually refreshes

After optional dynamic-object capture, the helper:

1. finds scene object hash `0x73d39136`;
2. calls `UpdateAnimationFramePosition(bike, checkpoint[0x14 + bike+0x8d0*4])`;
3. computes track position and tangent using the checkpoint/frame object;
4. calls virtual methods on the `0x73d39136` object:
   - vtable `+0x0c`: apply position-like vector;
   - vtable `+0x1c`: apply tangent/orientation-like vector;
5. calls child update and camera focus update;
6. if `bike+0x13c != 0`, refreshes bike pose caches:
   - hash `0x1329aad5` into `bike+0xd0..0xe8`;
   - hash `0x5bd7085d` into `bike+0xec..0xf4`;
7. calls a broad rider/bike state reset helper `FUN_00912bb0` unless current mode/state suppresses it;
8. refreshes physics/collision recursively under `bike+0x678`;
9. if `preserveDynamicObjects != 0`, re-collects type `3,1` objects and reapplies:
   - `SetSceneObjectPosition(object, capturedB)`;
   - `SetSceneObjectVelocity(object, capturedA)`.

Implication:

This helper is better than checkpoint respawn for first restore tests, but it is not a full physical snapshot restore. It refreshes the root/camera/cache state and preserves two vec3 groups for dynamic objects. It does not explicitly restore every object's orientation matrix.

### Orientation helpers

`GetQuaternionOrAngularVelocity(bike, outQuat)` is the bike-level orientation getter:

- if `bike+0x13c == 0`, finds hash `0x1329aad5` and calls `GetWorldQuaternion`;
- otherwise finds hash `0x1329aad5` and calls `get_rigid_body_angular_velocity`;
- fallback reads `bike+0xdc..0xe8`.

Mapping:

- Uplay `0x0090bdc0`
- Steam `0x0034b670`

`SetQuaternionRotation(sceneObject, quat)` is a scene-object-level quaternion setter:

- calls `UpdatePhysicsWorld(sceneObject)`;
- if quaternion length is positive, writes to `*(sceneObject+0x28)+0x20..0x2c`;
- clears dirty bits on the object and its parents.

Mapping:

- Uplay `0x00da3ec0`
- Steam `0x007e1f10`

`UpdateSceneObjectTransform(sceneObject)` updates live physics parameters from scene-object fields, but it does not look like a general transform restore API. It writes mass/inertia/velocity/damping-related state and depends heavily on flags at `sceneObject+0xb2`.

Mapping:

- Uplay `0x00d58ec0`
- Steam `0x00797b30`

Implication:

If mid-air save-state restore needs orientation patch-up, test `SetQuaternionRotation` on stable hash `0x1329aad5` first. Do not use `UpdateSceneObjectTransform` as the first orientation restore primitive.

### Virtual transform helper

`SceneObject_ApplyTransformAndRender(sceneObject, position, rotation, scale)` is a thin wrapper:

```cpp
sceneObject->vtable[0x0c](position);
sceneObject->vtable[0x1c](rotation);
sceneObject->vtable[0x18](scale);
SceneObject_RenderAndUpdateCamera(sceneObject);
```

Existing editor notes already identify it as a transform application helper.

Implication:

This is a candidate for editor/static-object transform work, but for live bike/rider save-state restore the more relevant path is still `UpdateBikeAndCameraFromTrack`, followed by targeted per-object patch-up.

### Implementation contract tightened

For `TFPayload/save-state.cpp`, initialize function pointers with version-specific RVAs:

```cpp
static UpdateBikeAndCameraFromTrackFunc g_updateBikeAndCameraFromTrack = nullptr;
static FindSceneObjectByHashFunc g_findSceneObjectByHash = nullptr;
static SetSceneObjectPositionFunc g_setSceneObjectPosition = nullptr;
static SetSceneObjectVelocityFunc g_setSceneObjectVelocity = nullptr;
static SetQuaternionRotationFunc g_setQuaternionRotation = nullptr; // optional phase 2
```

Minimum first restore:

```cpp
bool RestoreCheckpointFrameOnly(const SaveStateSlot& slot) {
    void* bike = Respawn::GetBikePointer();
    void* checkpoint = Respawn::GetCheckpointPointer(slot.checkpointIndex);
    g_updateBikeAndCameraFromTrack(bike, checkpoint, 1, 1);
    Respawn::SetFaultCounterValue(slot.faults);
    Respawn::SetRaceTimeMs(slot.raceTimeMs);
    return true;
}
```

This intentionally skips manual object patch-up at first, so the first runtime test isolates what the native helper can do by itself.

Second restore layer:

```cpp
RestoreCheckpointFrameOnly(slot);
RestoreBikePoseCacheD0_100(slot);
RestoreObjectVec3PairsByHash(slot);
```

Third restore layer only if mid-air rotation fails:

```cpp
RestorePrimaryObjectQuaternion(slot); // hash 0x1329aad5 via SetQuaternionRotation
RestoreLiveBodyMatrix10_48(slot);     // raw write, only after quaternion setter is insufficient
```

### Public API surface

`respawn.h` already exposes the two helpers needed for the first prototype:

- `Respawn::GetBikePointer()`
- `Respawn::GetCheckpointPointer(int index)`

The internal `GetGameManager()` and `GetCheckpointListBase()` are private to `respawn.cpp`, but the save-state prototype does not need them for the first restore path. Avoid duplicating pointer-chain logic unless later capture code needs direct access to `manager+0x938`.

### Fourth-pass risk update

Lowest-risk first code:

1. one-slot capture of checkpoint index, time, faults;
2. one hotkey restore using `UpdateBikeAndCameraFromTrack(..., 1, 1)`;
3. logging only, no manual object writes yet.

Next risk tier:

1. save and restore `bike+0xd0..0x100`;
2. restore the two vec3 groups per collected object;
3. add hash-based object matching.

Highest risk remains:

1. raw live body matrix writes;
2. synthetic command object queueing;
3. cross-track persistence.

## Fifth pass findings: prototype wiring

This pass focused on turning the Ghidra findings into an implementation-ready first prototype.

### Native confidence update

`UpdateBikeAndCameraFromTrack` has direct xrefs from:

- `ResetAndLoadTrackById`
- `ResetAndLoadTrackFromPath`
- `TogglePlaybackMode`
- `PausePlayback`
- `InitializePlaybackMode`
- `StartPlaybackSession`
- `RenderCurrentBike`
- `WriteNearbyObjectStatePacket_STA0`
- `WriteAllBikeCameraPosePacket_CPC0`
- `ProcessRespawnCommand`

The important caller is `ProcessRespawnCommand`. Its subcommand `2` path does:

```cpp
if (*(char *)(command + 0xac) == 0) {
    ApplyTrackPlaybackFrameToBikeAndCamera(
        bike,
        checkpoint,
        *(char *)(command + 0xad),
        1);
} else {
    HandlePlayerRespawn(...);
}
```

So the direct helper call is not just an editor/playback artifact. It is the native direct-position branch for respawn command processing:

- `command+0xac == 0`: direct bike/camera update;
- `command+0xac != 0`: queue normal respawn through `HandlePlayerRespawn`;
- `command+0xad`: preserve dynamic objects flag;
- fourth argument `1`: track evaluation mode for checkpoint-style respawn.

This strengthens the first prototype call:

```cpp
g_updateBikeAndCameraFromTrack(bike, checkpoint, 1, 1);
```

It is equivalent to the direct branch with dynamic-object preservation enabled.

### Existing mod integration points

`dllmain.cpp` already has the pattern needed for a small module:

- include the feature header at the top;
- initialize modules from the base address during startup;
- shut modules down from `Cleanup`;
- poll feature hotkeys from `KeyMonitorThread`.

Relevant current order:

```cpp
Respawn::Initialize(ctx->baseAddress);
...
Respawn::Shutdown();
...
Respawn::CheckHotkey();
Camera::CheckHotkey();
Multiplayer::CheckHotkey();
```

Recommended save-state wiring:

```cpp
#include "save-state.h"

SaveState::Initialize(ctx->baseAddress); // after Respawn::Initialize
SaveState::Shutdown();                   // before or near Respawn::Shutdown
Respawn::CheckHotkey();
SaveState::CheckHotkey();
```

Place `SaveState::CheckHotkey()` next to `Respawn::CheckHotkey()` because the first implementation depends on checkpoint, fault, and time helpers.

### Keybinding shape

Do not reuse `CaptureSessionState`.

`CaptureSessionState` is already grouped with multiplayer monitoring and is exposed in the dev menu as a multiplayer/logging action. Reusing it for save states would create ambiguous UI and config behavior.

Add new actions instead:

```cpp
CaptureSaveState,
RestoreSaveState,
DebugSaveState,
```

Default them to unbound for the first pass. That avoids collisions with the existing dense debug keys:

- `F8`: `DebugGameState`
- `F9`: `DebugBikeInfo`
- `F11`: development console
- `Q/W/E`: checkpoint stepping
- `1/2`: fault/time reset

After the prototype is stable, optional DEVELOPMENT_MODE defaults can be added, but unbound is safer for the first build.

### First module contract

Recommended initial files:

```cpp
// save-state.h
namespace SaveState {
    bool Initialize(uintptr_t baseAddress);
    void Shutdown();
    void CheckHotkey();

    bool CaptureSlot();
    bool RestoreSlotNativeOnly();
    void DebugDumpSlot();
}
```

First-slot data should stay intentionally small:

```cpp
struct SaveStateSlot {
    bool valid = false;
    int checkpointIndex = -1;
    void* checkpointPtr = nullptr;
    void* bikePtr = nullptr;
    int raceTimeMs = 0;
    int faults = 0;
};
```

For the first runtime test, avoid object snapshots, quaternion writes, raw matrices, and synthetic command objects.

### First capture path

Use only public `Respawn` helpers:

```cpp
bool CaptureSlot() {
    void* bike = Respawn::GetBikePointer();
    int checkpointIndex = Respawn::GetCurrentCheckpointIndex();
    void* checkpoint = Respawn::GetCheckpointPointer(checkpointIndex);

    slot.bikePtr = bike;
    slot.checkpointIndex = checkpointIndex;
    slot.checkpointPtr = checkpoint;
    slot.raceTimeMs = Respawn::GetRaceTimeMs();
    slot.faults = Respawn::GetFaultCount();
    slot.valid = bike && checkpoint && checkpointIndex >= 0;
    return slot.valid;
}
```

This captures enough to validate the native helper without duplicating `respawn.cpp` private pointer-chain code.

### First restore path

Minimal restore should be:

```cpp
bool RestoreSlotNativeOnly() {
    if (!slot.valid) {
        return false;
    }

    void* bike = Respawn::GetBikePointer();
    void* checkpoint = Respawn::GetCheckpointPointer(slot.checkpointIndex);
    if (!bike || !checkpoint || !g_updateBikeAndCameraFromTrack) {
        return false;
    }

    g_updateBikeAndCameraFromTrack(bike, checkpoint, 1, 1);
    Respawn::SetFaultCounterValue(slot.faults);
    Respawn::SetRaceTimeMs(slot.raceTimeMs);
    return true;
}
```

Wrap the native call with the same defensive style used elsewhere in `TFPayload`: pointer checks, readable-memory checks where cheap, and SEH around the cross-binary call.

Restore counters after the native call. If the native helper or downstream state refresh touches race state, restoring time/faults last gives the smallest observable state drift.

### Project-file impact

If implemented as new source files, the Visual Studio project needs:

- `TFPayload/save-state.h`
- `TFPayload/save-state.cpp`
- corresponding `TFPayload.vcxproj` entries;
- corresponding `TFPayload.vcxproj.filters` entries.

The project files are already actively edited in this workspace, so any later implementation should inspect their current state before modifying them and avoid reordering unrelated entries.

### Validation plan

Recommended first runtime matrix:

1. Capture while stationary at a checkpoint, ride forward, restore.
2. Capture shortly after crossing a checkpoint, ride forward, restore.
3. Capture mid-air, restore, observe whether the helper snaps to checkpoint frame rather than mid-air pose.
4. Capture with a moved/knocked dynamic object nearby, restore, observe whether `preserveDynamicObjects=1` keeps it from resetting.

Expected result for prototype one:

- checkpoint-frame restore works;
- time and faults restore;
- dynamic objects are not obviously reset by the helper;
- mid-air pose is not fully preserved yet.

If that result holds, the next pass should add the second layer:

```cpp
uint8_t bikePoseCacheD0_100[0x34];
```

Then restore that cache after the native helper and test whether it improves mid-air orientation before touching per-object matrices.
