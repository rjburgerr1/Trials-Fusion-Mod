# Editor Object Material Color Deep Dive

Date: 2026-05-22

Working base:

```text
GHIDRA_BASE_UPLAY = 0x00700000
```

Runtime RVA is `ghidra_address - 0x00700000` for Uplay. Steam mappings below
come from `uplay-to-steam-function-map.csv` where available.

## Summary

Changing selected editor object color should not write the mesh fields
`sceneObject+0x90..0x9c` directly. Those fields are serialized for some object
types, but the previous runtime test detached the editor gizmo from the object.

The stronger path is the material override path:

```text
scene object material parameter lookup
  -> 0x60-byte material override record
  -> create_mesh_instances(sceneObject, overrideList, blend)
  -> MeshInstance_ApplyMaterialColorOverrides(sceneObject, overrideRecord, blend)
  -> material parameter vfunc +0x74 writes float4 color
```

This path is already used by bike hidden-object tinting and by scene material
serialization. It is probably safe for a DEVELOPMENT_MODE live experiment once
the selected editor object's real scene root/mesh scene object is selected.

Durability is still open. The runtime material override can repaint a live scene
object, but save/load persistence probably requires either finding the editor
property backing store or patching the resource/GFX/material data before the
scene is created.

2026-05-22 follow-up: sticky/hook repainting is the wrong end state for editor
object recolor. Moving an object rebuilds or refreshes render state, so a
durable color feature should behave like scale: write the editor/object backing
state once and let normal editor movement/rendering consume that value. The
current DEVELOPMENT_MODE UI therefore separates:

```text
Try auto selectors      live diffuse/diffuse2 material override test
Set mesh color fields   one-shot write to serializer-tracked mesh +0x90..+0x9c
```

The second path is closer to "set and forget" because
`SerializeSceneMaterialsAndShaders` compares/writes these mesh color fields for
type 1 subtype 3 mesh objects. It is still an experiment until move/save/reload
behavior is verified.

Test result: `Set mesh color fields` did not visibly recolor the selected
object. Next direction is to locate a selected-object backing map for
color/material data, analogous to the known scale backing map at
`EntityManager+0xe90`.

The selected-object inspector report now scans `EntityManager+0xc00..0x13fc`
for hash maps keyed by several selected-object related addresses and dumps
matching mapped backing objects. Use this to look for a color/material backing
object near the known maps:

```text
EntityManager+0xe74  selected object -> editor visual/mapped object
EntityManager+0xe90  selected object -> scale backing object
EntityManager+0xea8  selected object -> editor transform object
```

The scan tests multiple key candidates (`selectedObject`, `parentKey1c`,
`sceneHolder44`, `resourceSceneRoot`, first mesh scene object, etc.) and a few
simple map layouts. This is needed because an initial selected-object-only scan
returned zero matches for the tested object.

Second scan result also returned zero matches. Next instrumentation target is
the known-working material parameter lookup itself. The inspector report now
dumps, per collected material object/selector/slot:

```text
scene object
selector
slot diffuse/diffuse2
material getter address
material parameter owner
setter address
value pointer
current float4 value
owner raw +0x00..0x7f
```

If the color can be made durable, the owner or value pointer should lead to a
resource/backing object that survives editor movement/rebuild.

First useful report hit:

```text
sceneObject=0x3D5E6890
selector=0x40000001
slot=0(diffuse)
owner=0x3D5E6890
valuePtr=0x29CC7A00
value=(1,0,1,1)
```

This suggests the live diffuse owner for the working selector is the material
scene object itself, with the actual color stored through a separate value
pointer. Added `Set material value ptr` to test a one-shot write directly to
that looked-up float4 pointer, bypassing `create_mesh_instances`, override
records, and sticky hooks.

Test result: direct `valuePtr` writes visibly set the color but reset on the
next frame. A trace showed `owner vtable+0x74` is called continuously with
animated/source colors, and the value pointer follows those writes. The assumed
`vtable+0x74` signature was wrong; treating its stack args as
`(handle, valuePtr, size)` caused VEH noise by interpreting float values such as
`0x3f800000` as pointers. The vtable hook path is now disabled. This confirms
the durable target is upstream of the material parameter cache/source color
animation, not the material value pointer itself.

Ghidra follow-up on `FUN_012008e0` shows the fallback/material update path reads
owner fields around `+0x104`, `+0x174`, `+0x1ac`, and `+0x1b0`, then calls into a
material/resource update API. The inspector report now dumps material owner
`+0x000..0x23f` and a `valuePtr-0x40..+0x7f` window so those upstream pointers
can be identified from a live selected object.

Live report follow-up: for the tested object the material parameter `owner` is
the scene object itself (`0x3D5E6890`), so offsets like `+0x104` and `+0x174`
are ordinary scene-object float fields, not the internal owner from
`FUN_012008e0`. The useful material tail starts later:

```text
0x1ac  pointer-like value
+0x1b0  pointer inside/near the scene-object allocation
+0x1b4  small count-like value
+0x1d0  repeats the +0x1ac pointer
+0x1d4  adjacent pointer inside/near the scene-object allocation
```

The report was expanded again to dump `owner+0x240..0x37f` and the pointed
targets at `+0x1ac`, `+0x1b0`, `+0x1d0`, and `+0x1d4`. That should show the
material/source table feeding the reset-prone `valuePtr` cache.

Ghidra follow-up on `TrackEventDispatcher_ApplyEventToSceneObject` found the
more likely durable backing maps. The object returned by
`*(gameManager+0x108)+0x174` is used as `this` for event application. In case
`0x13`, it looks up a tree map at `this+0x4c` by scene-object pointer and passes
the found node's `+0x14` vector directly to `create_mesh_instances`. In case
`0x12`, it looks up `this+0x94` and copies node fields `+0x20..+0x2c` into mesh
scene object `+0x90..+0x9c`. This explains why directly writing those mesh
fields does not persist. The next inspector report now probes both maps for the
selected object, first mesh, resource root, and collected material objects.

Runtime report result: `trackEventMaterialTarget` resolved, but both backing
maps were empty for the selected object and all related scene/material object
keys:

```text
materialOverrideMap+0x4c root=<null> node=<missing>
meshColorMap+0x94 root=<null> node=<missing>
```

So the tested editor object does not currently have a durable color/material
override node to edit. The safe next step is tracing the native event path that
would create or consume these nodes. DEVELOPMENT_MODE now includes a
`Trace backing events` button that hooks
`TrackEventDispatcher_ApplyEventToSceneObject` and logs event `0x12` and `0x13`
map state before and after the engine handles the event. This is read-only
instrumentation; it does not repaint or reapply color.

First trace test installed and armed the hook, but no event `0x12`/`0x13`
entries were logged after the report click. The trace was broadened to log the
first 120 total event IDs while armed, with detailed map dumps still limited to
events `0x12` and `0x13`. This will distinguish "wrong event IDs/path" from
"right path but no material/color event fired during the test action."

## Relevant Functions

```text
BikeVisuals_ApplyBikeHiddenObjectMeshTint
  Uplay 0x009226d0, RVA 0x002226d0
  Steam RVA 0x00221fc0

create_mesh_instances
  name present in Ghidra
  calls MeshInstance_ApplyMaterialColorOverrides for each 0x60-byte record

MeshInstance_ApplyMaterialColorOverrides
  name present in Ghidra
  material param getter vfunc: sceneObject vtable +0x64
  material param setter vfunc: parameter owner vtable +0x74

SceneObject_SetMaterialTextureAndUserData
  sets mesh material/texture by hash, then refreshes scene object resources

SetMaterialTextureByHash
  Uplay 0x00d8a970, Steam 0x007c93f0

SetMaterialTextureParameter
  Uplay 0x00d83510, Steam 0x007c1fa0

FindMaterialByHash
  Uplay 0x00d89d80, Steam 0x007c8800

FindMaterialByPath
  Uplay 0x00d826e0, Steam 0x007c1170

CollectMaterialsToList
  Uplay 0x00d7d110, Steam 0x007bbb90

SerializeSceneMaterialsAndShaders
  Uplay 0x0099ce90, Steam 0x003dc870
```

## Material Parameter Names

Ghidra hash constants use the same G4-style hash as other property names.

```text
diffuse         0x55e806b2
basecolor       0xf8788249
substanceColor  0x12140891
substanceMul    0x93d58564
substancePow    0xf91de455
refraction      0xff70ffca
color           0x1b80ec1e
```

`MeshInstance_ApplyMaterialColorOverrides` prefers:

```text
slot byte 0 -> diffuse
slot byte 1 -> diffuse2
slot byte N -> color{N+1}
```

`SerializeSceneMaterialsAndShaders` separately reads material parameters named:

```text
basecolor
substanceColor
substanceMul
substancePow
refraction
```

That means there may be two color concepts:

1. Mesh-instance tint/override color, used for bike hidden-object tinting.
2. Material-layer/substance color, serialized for scene material/shader data.

For editor object recolor, start with mesh-instance tint because the engine has
an established live write path for it.

## Override Record Shape

`BikeVisuals_ApplyBikeHiddenObjectMeshTint` allocates a vector of records with
element size `0x60`, then appends one color record:

```text
record+0x00  material/target hash or selector
record+0x10  slot byte; 0=diffuse, 1=diffuse2, else color{slot+1}
record+0x11  blend/source mode byte
record+0x20  red float
record+0x24  green float
record+0x28  blue float
record+0x2c  alpha float
record+0x30  override mode byte
record+0x31  secondary mode byte
record+0x40  extra0
record+0x44  extra1
record+0x48  extra2
record+0x4c  extra3
record+0x50  record count / active flag in copied buffers
```

The exact naming is still provisional, but `MeshInstance_ApplyMaterialColorOverrides`
uses these offsets clearly:

```text
param_2[0x14]                  record count
record+0x00                    skip record if zero
record+0x10                    selects diffuse/diffuse2/colorN parameter
record+0x11                    mode: 2 keeps live RGB and uses record alpha
record+0x20..0x2c              target RGBA
```

For a simple live tint experiment, use:

```text
record+0x00 = 1 or known material selector copied from bike path
record+0x10 = 0
record+0x11 = 0
record+0x20 = r
record+0x24 = g
record+0x28 = b
record+0x2c = a
record+0x50 = 1
blend       = 1.0
```

`record+0x00` is the least certain field. In bike tint, it comes from the bike
visual catalog entry at `entry+0x14`, then the record count is separately stored
by the vector wrapper passed to `create_mesh_instances`.

## Existing Runtime Anchors

The selected-object inspector already finds the scene objects needed for a live
experiment:

```text
selectedObject+0x44                     sceneHolder
sceneHolder+0x00                        resourceContainer
resourceContainer+0x68                  resourceSceneRoot
first mesh scene object                 collected as type 1 subtype 3
SceneObject_CollectByType               Uplay RVA 0x6a3000
```

Current note from `editor_object_properties_and_calls.md` still stands:

```text
mesh+0x90..0x9c direct writes are unsafe for color controls.
```

Updated nuance: these fields are not random; both `ApplyColorToMeshObjects` and
`SerializeSceneMaterialsAndShaders` use them as per-mesh color state. Direct
writes can still be unsafe if made on the wrong scene object or without marking
the editor dirty/refreshing. The `Set mesh color fields` DEVELOPMENT_MODE button
writes collected type 1/subtype 3 mesh objects once, marks the editor dirty, and
refreshes the scene object so we can test whether this is the durable backing
path for selected editor object recolor.

## Safe Experiment Plan

Implemented DEVELOPMENT_MODE test UI in:

```text
Dev Menu -> Mod -> Editor -> Selected Object Inspector -> Material Color Override Test
```

The panel exposes:

```text
Override color
Selector
Slot (0=diffuse, 1=diffuse2, N=color{N+1})
Source mode
Override mode
Secondary mode
Refresh after apply
Apply color to first mesh
Apply color to all meshes
```

Build/test status:

```text
TFPayload DEVELOPMENT_MODE build succeeded on 2026-05-22.
```

1. In DEVELOPMENT_MODE, add a hidden/test-only action that targets only the
   primary selected object's `firstMeshSceneObject`.
2. Build one stack/local 0x60-byte override record and a tiny vector wrapper
   matching the call shape expected by `create_mesh_instances`.
3. Call `create_mesh_instances(firstMeshSceneObject, &overrideVector, 1.0f)`.
4. Start with `diffuse` slot 0 and an obvious color such as magenta.
5. If nothing changes, try:
   - root scene object instead of first mesh scene object
   - every collected type 1 subtype 3 mesh object
   - record selector copied from a real bike tint call shape
   - `basecolor` / `substanceColor` parameter set path instead of diffuse
6. After a visible result, test selection changes, variation rebuild, object
   copy/paste, save, reload, and placing a new instance.

Expected first result: live repaint only. Do not assume persistence until save
and reload are verified.

## Ghidra Follow-Ups

1. Confirm the exact vector wrapper passed to `create_mesh_instances`.
2. Rename the material parameter vfuncs after checking a concrete vtable:
   - `sceneObject vfunc +0x64`: get material parameter by hash
   - `parameter owner vfunc +0x74`: set material parameter bytes
3. Resolve `record+0x00` by tracing the bike visual catalog entry at
   `entry+0x14` and comparing multiple tinted bike children.
4. Find editor/object property code that references `basecolor`,
   `substanceColor`, or `diffuse` during object placement and serialization.
5. Determine whether editor objects store per-instance material overrides, or
   whether all material changes must be resource-level changes.
