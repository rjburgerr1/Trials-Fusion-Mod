# Bike / Rider race-entity first-pass map

## Current working hypothesis

The object passed through `ChangeBikeWithMeshReload`, `LoadBikeSettings`,
`LoadBikeMeshAndVisuals`, `SerializeBikeSceneObjects`, and `FinalizeRiderSetup`
is a single large **bike+rider race entity**, not a tiny bike-only object.

Evidence:

- `InitializeRiderPool` allocates `0xB20` bytes per entity before calling
  `RaceConstructor`.
- `ChangeBikeWithMeshReload` writes bike appearance fields, then calls both
  bike reload functions and `FinalizeRiderSetup`.
- `FinalizeRiderSetup` directly resolves rider attachment nodes such as
  `"AttachLeftLeg"`, `"AttachRightLeg"`, `"AttachLeftArm"`, `"AttachRightArm"`,
  `"AttachHead"`, and `"AttachPelvis"` from the same `this` object.

## Useful anchor functions

| Function | Address | Why it matters |
|---|---:|---|
| `ReloadBikeFromSettings` | `0x0079FD40` | Pulls selected bike id and appearance packet, then reloads current bike entity |
| `ChangeBikeWithMeshReload` | `0x00929C00` | Best entry point for mapping bike entity fields |
| `LoadBikeSettings` | `0x00908490` | Bulk copies bike-data table fields into entity offsets |
| `LoadBikeMeshAndVisuals` | `0x009144E0` | Builds visual subobjects and scene graph links |
| `InitializeBikeAppearanceSlots` | `0x00929980` | Applies appearance slot ids / material hashes |
| `SerializeBikeSceneObjects` | `0x00905750` | Walks flattened scene tree from the bike entity |
| `FinalizeRiderSetup` | `0x0090A7C0` | Hooks rider limbs / attachment points into the same entity |
| `GetBikeAppearanceData` | `0x00A055A0` | Builds the 0x20-byte appearance packet consumed by the entity |

## High-confidence bike+rider entity offsets

| Offset | Proposed field | Evidence |
|---:|---|---|
| `0x134` | scene object collection ptr | `LoadBikeMeshAndVisuals` replaces/releases it |
| `0x13C` | rider/visual mode flag | Gates rider attachment setup and `BikeVisuals` construction behavior |
| `0x13D` | scene processed flag | Used after `ProcessChildSceneObjects` |
| `0x44` | mesh owner / scene host ptr | Used with `BikeEntity_UpdateMesh` and `ProcessChildSceneObjects` |
| `0x678` | current render root / bike scene root | Set from bike visuals; flattened in `SerializeBikeSceneObjects` |
| `0x67C` | bike mesh/material id | Loaded from bike data table `+0x28`; passed to material functions |
| `0x680` | bike id | Written by `ChangeBikeWithMeshReload`; read throughout bike logic |
| `0x9E4` | player / rider slot id | Read by `UpdatePlayerBikesAppearance` |
| `0x9EC..0xA08` | appearance packet cache | Written by `ChangeBikeWithMeshReload` from the 0x20-byte packet |
| `0xAD8` | `BikeVisuals*` | Allocated by `LoadBikeMeshAndVisuals` |

## Appearance packet layout

`GetBikeAppearanceData` constructs a 0x20-byte packet which
`ChangeBikeWithMeshReload` copies into the entity:

| Packet offset | Entity offset | Likely role |
|---:|---:|---|
| `0x00` | `0x9EC` | appearance slot id 0 |
| `0x02` | `0x9EE` | appearance slot id 1 |
| `0x04` | `0x9F0` | appearance slot id 2 |
| `0x08` | `0x9F4` | material/hash for slot 0 |
| `0x0C` | `0x9F8` | material/hash for slot 1 |
| `0x10` | `0x9FC` | material/hash for slot 2 |
| `0x14` | `0xA00` | secondary appearance slot id 0 |
| `0x16` | `0xA02` | secondary appearance slot id 1 |
| `0x18` | `0xA04` | material/hash for secondary slot 0 |
| `0x1C` | `0xA08` | material/hash for secondary slot 1 |

`InitializeBikeAppearanceSlots` strongly suggests:

- the first 3 slot ids at `0x9EC..0x9F0` are one category of bike visual parts;
- the 2 slot ids at `0xA00..0xA02` are a second category;
- the paired dwords at `0x9F4..0x9FC` and `0xA04..0xA08` are resolved material / variation ids.

If the known Pit Viper tire-color chain lands inside this band, the next step is
to identify **which slot index** corresponds to tires by watching reads/writes
while changing only tire color in-game.

## Bike data table -> runtime entity copies

`LoadBikeSettings` is effectively a field-copy oracle from the static bike data
record into the live entity. A few especially useful examples:

| Bike data offset | Entity offset |
|---:|---:|
| `0x28` | `0x67C` |
| `0x34` | `0x274` |
| `0x38` | `0x27C` |
| `0x3C` | `0x260` |
| `0x40` | `0x268` |
| `0x44` | `0x1E0` |
| `0x58` | `0x2C8` |
| `0x64` | `0x514` |
| `0x68` | `0x51C` |
| `0x6C..0xA8` | `0x21C..0x280` family |
| `0x11C..0x160` | `0x4A4..0x4E8` family |
| `0x164 / 0x16C` | dynamic array -> `0x704..0x70C` |
| `0x170 / 0x178` | dynamic array -> `0x710..0x718` |
| `0x17C..0x1A0` | `0x6E8..0x728` family |

This makes `LoadBikeSettings` the best function for reconstructing the static
`BikeData` record in parallel with the runtime `BikeRiderEntity`.

## Rider-facing evidence inside the same entity

`FinalizeRiderSetup` uses these entity fields:

| Offset | Proposed role |
|---:|---|
| `0x4EC` | rider attachment list / left-side attachment container |
| `0x4F0` | rider attachment list / right-side attachment container |
| `0x4F4` | serialized scene-object registry |
| `0x298`, `0x29C`, `0x2A0`, `0x2A4` | rider constraint / ragdoll tuning values |
| `0x2A8`, `0x2AC` | values passed into rider setup helper |

The rider code is not merely adjacent; it is operating on the same owning entity.

## Appearance bridge, refined

The first half of the bridge is now clearer:

### `FUN_00922E90` = candidate `ApplyAppearanceGroup`

- Input: one selected appearance id from the packet (`0x9EC..0xA08`)
- Looks up an expansion record for that id.
- Iterates the list of concrete part ids in that record.
- Calls `FUN_009226D0` for each concrete part id.

### `FUN_009226D0` = candidate `ApplyAppearancePart`

- Resolves the concrete part record from the customization manager at
  `g_pGameManager + 0x114`.
- Reads the part name string stored in that record.
- Hashes the string and finds the matching scene node under the bike visual root
  at entity `+0x678`.
- Rebuilds mesh instances for that scene node.

This means the packet fields at `0x9EC..0xA08` are definitely a
**part-selection layer**. They choose geometry / scene-node variants. They are
not themselves the live RGB material floats.

## Material layer, refined

`BikeVisuals_InitializeMaterialOverrides` is a second stage that runs after the
mesh root is built:

- finds scene nodes by hash,
- applies override handles from entity `+0x534..+0x548`,
- registers those override scene objects with the entity host at `+0x44`.

That makes `+0x534..+0x548` the most promising current candidate band for
**material override objects** associated with the live bike visual tree.

## Current strongest model

```text
appearance packet
  entity +0x9EC..+0xA08
        |
        v
ApplyAppearanceGroup / ApplyAppearancePart
        |
        v
chosen bike scene nodes / mesh variants under +0x678
        |
        v
BikeVisuals_InitializeMaterialOverrides
  entity +0x534..+0x548
        |
        v
renderer/resource material graph
        |
        v
live tire material floats
  RGB +0x50/+0x54/+0x58, brightness +0x74
```

## What static analysis has not proven yet

We have **not yet proven which appearance group or material override corresponds
specifically to the tires**. The static code tells us the architecture, but the
semantic label "tire" is still hiding in runtime customization records / names.
The cleanest discriminator now is a runtime diff:

1. capture `BikeRiderEntity + 0x9EC..0xA08`,
2. capture `BikeRiderEntity + 0x534..0x548`,
3. change only tire color,
4. capture both bands again,
5. compare which field(s) moved while the rendered tire material chain also
   changed.

If only `+0x534..+0x548` changes, tire color is a pure material override.
If one of `+0x9EC..+0xA08` also changes, tire selection is coupled to the same
appearance family.

## Rider customization path, first pass

The rider path is a sibling system to the bike path, but it is not identical.

### Relevant persistent state

`SaveGearAndCustomizationState` explicitly serializes:

- `activeRiderGear`
- `riderGearColors`
- `riderGearColorsNew`
- `savedRiderGear`
- `savedRiderGearColors`

So rider part selection and rider color selection are both first-class systems.

### Live rider entity fields

`FUN_00A06BB0` reveals a likely rider customization state object layout:

| Offset | Working meaning |
|---:|---|
| `+0x108..+0x10C` | 3 rider gear ids / categories |
| `+0x129...` | `riderGearColorsNew` backing area |
| `+0x688..` | expanded rider color selection table written by `FUN_00A061B0` |
| `+0x346...` | active bike part / bike color-related table, separate from rider gear |

### Rider gear application

`ProcessUnicodeCharacters` expands a rider customization entry into concrete ids
and calls `FUN_00915A40` for each id.

`FUN_00915A40`:

- resolves a rider gear record,
- hashes the gear part's name string,
- finds the named scene node under entity `+0x678`,
- calls `ReplaceSceneObjectMaterialByHash(node, 0, 0, true)`.

That suggests this stage is preparing or clearing the target rider scene node so
the active rider color system can later tint it.

### Rider color rebuild path

`FUN_00A06BB0` rebuilds rider gear defaults and rider/bike color tables before
calling `SaveGearAndCustomizationState`.

- First loop over 3 entries at `+0x108` uses `FUN_00A060E0`.
- Larger nested loop over 16 x 2 entries uses `FUN_00A061B0`.
- `FUN_00A061B0` writes 16-bit selections into:
  `this + (param_3 + 0x68C + param_2 * 2) * 2`

The string names from persistence strongly imply this table is where rider gear
color choices are normalized before being saved / reused.

### Current torso answer

`activeRiderGear[1]` / `riderGearColors[1]` still looks like the torso lane for
the currently observed setup, but the color reader is not a simple fixed
`gear slot -> color slot` lookup.

`GetRiderGearPackedColor` calls:

```text
colorGroup = FUN_00726030(param_2)
bodySlot = lookup gear record name in customizationState + 0x2D60
packedColor = *(customizationState + 0x110 + (colorGroup * 3 + bodySlot) * 4)
```

It masks the returned value to RGB unless the high byte is `0x01`; if the entry
is not an explicit custom color, it falls back through the gear record/default
appearance tables. This means `customization + 0x110` is a packed color table
with 3 body slots per color group, not a single flat "legs/torso/head" array.

The in-track rebuild issue is also clearer:

- `RebuildAllRiderGearVisuals` is called directly by `LoadSceneObjects`.
- `ProcessPendingRiderGearRebuilds` has no direct static caller in the current
  Ghidra project and appears to be reached only through a mode/update path that
  is not active while riding a track.
- Appending to the queue at `visualManager + 0x28/+0x2C/+0x30` is therefore not
  enough in-track, because nothing drains it afterward.
- `BuildRiderGearMeshWithColor` is where the packed color is actually consumed:
  it calls `UnpackRgb24ToFloat4(param_2, &localColor)`, writes that color into a
  0x60-byte mesh override descriptor, then calls `create_mesh_instances`.
  Therefore, writing `customization + 0x110` can only affect future rider mesh
  construction unless we find and patch the already-instanced material data.
- The current payload experiment queues a direct call to `RebuildRiderGearGroup`
  on the next hooked game frame after writing `riderGearColors[1]`. This should
  prove whether in-track rider visuals can be refreshed by reusing the gear
  rebuild path, or whether that path is only safe during load/menu contexts.
- Stronger visual-manager evidence: rider customization bridge helpers queue
  gear/color changes through `FUN_009cc600(*(g_pGameManager + 0xD4), gearId)`.
  `FUN_009cc600` appends to the secondary pending list at
  `visualManager + 0x34/+0x38/+0x3C`, matching the list drained by
  `ProcessPendingRiderGearRebuilds`. This makes `*(g_pGameManager + 0xD4)` the
  current best static source for the rider visual manager, better than trying
  to recover it from the global input list.

Runtime disproved that last hypothesis in-track:

```text
*(g_pGameManager + 0xD4) = 0x425ca690
mode = 1
primaryCount / secondaryCount = ASCII-like garbage
+0x98 / +0xCC / +0xF8 = mostly null or non-rider fields
```

So `g_pGameManager + 0xD4` is related to customization queues in some mode, but
it is not the live rider visual manager while riding a track.

The `DAT_0174b4fc` global input/update list was also empty in-track:

```text
list=0x989c810 activeHead=0 pendingHead=0 activeNodes=0 pendingNodes=0
```

So it is not useful for recovering a rider visual object after hot reload.

`DAT_0174b4ec` is now renamed/treated as `RaceController`. It is constructed by
`RaceController_ctor` (`0x009D3CE0`) and destroyed by `RaceController_dtor`
(`0x009D3E80`). It has several ref-counted callback/action slots:

| Offset | Evidence |
|---:|---|
| `+0x2A0` | set by `RaceController_SetRefSlot_0x2A0`; invoked from `FUN_00AE8700` |
| `+0x2B8` | set by `RaceController_SetRefSlot_0x2B8_ThenDispatch`; calls vfunc `+0x14` |
| `+0x2E8` | set by `update_object_reference`; calls vfunc `+0x18` |

Runtime dump after the failed visual-manager lookup:

```text
RaceController = 0x0B57B560
RaceController + 0x2E0 -> 0x0B57B800, first dword 0, not rider visual manager
```

This suggests these race-controller children are callback/action wrappers, not
the rider scene visual manager itself.

## Ghidra renames applied during rider-visual investigation

| Address | New name | Evidence |
|---:|---|---|
| `0x009D3CE0` | `RaceController_ctor` | Constructs `DAT_0174b4ec`; initializes race-controller vtables and ref slots |
| `0x009D3E80` | `RaceController_dtor` | Releases race-controller ref slots and calls base destructor |
| `0x009D5980` | `RaceController_scalar_dtor` | Calls `RaceController_dtor`, conditionally frees `this` |
| `0x009CC600` | `QueueRiderVisualSecondaryRebuild` | Appends a 0xC node to `this +0x34/+0x38/+0x3C` |
| `0x009CD8F0` | `ClearRiderVisualGearSlotMeshes` | Clears current rider gear meshes for slot table `this +0x40` |
| `0x009CCF30` | `ClearBikeVisualGearSlotMeshes` | Clears current bike visual meshes for slot table `this +0x54` |
| `0x009CF700` | `RiderVisualManager_Cleanup` | Removes `this+8` from input list, frees `+0xF8/+0xA0/+0x9C/+0x98` |
| `0x009D0A70` | `SetRaceInputModeAndForward` | Writes `this+0x14`, forwards to input manager mode |
| `0x009D0960` | `SetRaceObjectFlag_0x1C` | Tiny setter for `this+0x1C` |
| `0x012B6BF0` | `SetRaceObjectField_0x18` | Tiny setter for `this+0x18` |
| `0x009D40C0` | `RaceController_SetRefSlot_0x2A0` | Replaces ref-counted object at race controller `+0x2A0` |
| `0x009D4160` | `RaceController_SetRefSlot_0x2B8_ThenDispatch` | Replaces ref-counted object at `+0x2B8`, then dispatches vfunc `+0x14` |

## Rider color persistence rename cluster

`savedRiderGearColors` is serialized by `SaveGearAndCustomizationState`, but
the live/custom rider gear color setter writes `riderGearColors` first and then
calls the same save routine.

| Uplay address | Steam address | Proposed name | Evidence |
|---:|---:|---|---|
| `0x00A025A0` | `0x00441960` | `SetRiderGearCustomColor` | writes `this + 0x110 + ((colorGroup - 1) * 3 + bodySlot) * 4`, ORs RGB with `0x01000000`, calls `SaveGearAndCustomizationState` |
| `0x00A05440` | `0x004446D0` | `SetRiderGearCustomColorByGearName` | resolves body slot from `this + 0x2D60`, then calls `SetRiderGearCustomColor` |
| `0x00A05300` | `0x00444590` | `GetCurrentRiderGearColorForGear` | resolves gear body slot and returns the current packed color from `riderGearColors` |
| `0x00A05D70` | `0x00445000` | `ToggleGenderAndSwapSavedRiderGear` | copies between `activeRiderGear` / `riderGearColors` and `savedRiderGear` / `savedRiderGearColors` during gender/gear-set switching |

`uplay-to-steam.csv` confidence:

- `00A025A0 -> 00441960`: exact function mnemonics/instructions match.
- `00A05440 -> 004446D0`: exact function mnemonics/instructions match.
- `00A05300 -> 00444590`: exact function mnemonics/instructions match.
- `00A05D70 -> 00445000`: implied match only, so treat as useful but lower-confidence.

The binary now shows that:

1. rider gear ids are a 3-slot system,
2. rider gear colors are persisted separately,
3. named rider scene nodes are located and prepared at runtime,
4. color tables are rebuilt afterward.

So changing torso color should be achievable once we identify:

- which of the 3 `+0x108/+0x10A/+0x10C` slots maps to torso,
- which rider color entry in the rebuilt table corresponds to that slot,
- which live material node under `+0x678` receives the final tint.

## Payload safety adjustment

The dev-menu torso color picker now writes only the verified customization table
entry:

```text
customization + 0x110 + ((colorGroup - 1) * 3 + bodySlot) * 4
colorGroup = 1
bodySlot = 1
packed = 0x01000000 | rgb24
```

The previous payload also tried to patch the live pointer graph and then call
`BikeVisuals_InitializeMaterialOverrides` on the active bike+rider entity. Those
live-scene refresh attempts are disabled from the picker path because updating
the active rider scene mid-track is the current crash source. The next target is
therefore the already-instanced rider material endpoint, not a broad scene graph
refresh.

## Evidence-backed rider refresh path

The narrow rider rebuild path is `RebuildRiderGearGroup` at Uplay `0x009CE750`.
It is safer than bike/rider entity material reinitialization because it operates
on the rider visual manager object and rebuilds one rider gear group:

```text
RebuildRiderGearGroup(this = RiderVisualManager*, gearId)
  BikeVisualCatalog_GetGearSlotEntry(g_pGameManager + 0x114, gearId)
  FUN_00728b50(g_pGameManager + 0x114, &bodySlotTable)
  FUN_00A044F0(customizationState, resolvedTask) -> body slot index
  ClearRiderVisualGearSlotMeshes(this, bodySlot)
  GetRiderGearPackedColor(customizationState, gearId, *bodySlotTable, 0)
  for each hidden/concrete object in gear record:
    BuildRiderGearMeshWithColor(this, objectId, packedColor)
  this + 0x40 + bodySlot * 2 = gearId
```

`BuildRiderGearMeshWithColor` at Uplay `0x009CD0C0` is the point where the packed
color is consumed. It resolves the rider scene object under `this + 0xCC`,
unpacks RGB with `FUN_00C40370`, writes the color into the 0x60-byte mesh
descriptor, and calls `create_mesh_instances`.

The queue path is also explicit:

```text
QueueRiderVisualSecondaryRebuild(this, gearId) at Uplay 0x009CC600
  appends a 0xC node to this + 0x34 / +0x38
  increments count at this + 0x3C

ProcessPendingRiderGearRebuilds(this, param_1)
  drains this + 0x28 / +0x30 through RebuildRiderGearGroup
  drains this + 0x34 / +0x3C through ApplyBikeVisualGearSlot
```

So the safe mid-track approach is not to rediscover globals with logging. Hook a
game-owned rider visual manager method, capture the actual live
`RiderVisualManager*` from its `this` pointer, and queue or call the narrow rider
gear rebuild on that object from the same game-frame context. The unsafe part of
the previous attempt was using the bike/rider race entity as the refresh target.

## Best next steps

1. Treat the known Pit Viper tire-color CE chain as a **downstream rendered
   material path**, not yet as proof of the canonical bike appearance field.
   Current chain:
   - base global: `DAT_01755278` on Uplay (`moduleBase + 0x01055278`)
   - offsets: `0x18 -> 0x268 -> 0x74 -> 0x94 -> 0x20 ->`
     `0x50/0x54/0x58/0x74`
   - terminal floats: RGB at `+0x50/+0x54/+0x58`, brightness at `+0x74`
2. `DAT_01755278` appears to be a renderer/resource-factory style global, not
   the bike entity itself:
   - `load_material_textures` calls virtual methods on it to create texture-like
     resources.
   - `InitializeUnderwaterEffectSystem` uses the same object to load unrelated
     global effect textures.
   - `BuildSoftBodyMesh` also allocates GPU-ish resources through it.
3. Because of that, use the CE tire chain as the **material-instance endpoint**
   and the entity appearance packet as the **upstream authoring/config layer**.
   The bridge between them is likely inside `InitializeBikeAppearanceSlots`,
   `FUN_00922E90`, and the material-override helpers used by `BikeVisuals`.
4. Determine whether the canonical tire selector is one of `0x9EC..0xA08` by
   changing only tire color and watching:
   - which appearance slot id/hash changes on the entity, and
   - which rendered material floats are rebuilt afterward.
5. Put a Cheat Engine write breakpoint on the rendered tire-color floats while changing only
   tire color. The writer should identify whether the value is:
   - loaded directly from the appearance manager,
   - copied from the 0x20-byte packet, or
   - transformed later by a visuals/material method.
6. In Ghidra, rename the main object provisionally to `BikeRiderEntity` and define
   a temporary struct containing only the offsets above.
7. In parallel, define a `BikeAppearancePacket` struct for the 0x20-byte blob and
   apply it to:
   - stack locals in `ReloadBikeFromSettings`,
   - the third argument of `ChangeBikeWithMeshReload`,
   - the local buffers in `UpdatePlayerBikesAppearance`.
8. Once the tire-color slot is proven, walk xrefs to the corresponding material
   resolver helper (`FUN_00A05300`, `FUN_00A05120`, or `FUN_00A030C0`) to recover
   the backing appearance tables.

## Names worth applying in Ghidra now

- `RaceConstructor` remains fine for now, but the allocated instance can be typed
  as `BikeRiderEntity*`.
- `FUN_00922E90` -> candidate `ApplyBikeAppearanceSlot`
- `FUN_00A05300` -> candidate `ResolvePrimaryAppearanceMaterial`
- `FUN_00A05120` -> candidate `ResolveVariantAppearanceMaterial`
- `FUN_00A030C0` -> candidate `ResolveSecondaryAppearanceMaterial`

These are provisional names, but they make the decompiler much easier to reason
about while we validate the slot semantics.
