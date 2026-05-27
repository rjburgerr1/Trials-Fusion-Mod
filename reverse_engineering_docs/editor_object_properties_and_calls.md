# Editor Object Properties and Calls

Date: 2026-05-22

Working base:

```text
GHIDRA_BASE_UPLAY = 0x00700000
```

Runtime RVA is `ghidra_address - 0x00700000` for the Uplay build. Steam RVAs
still need separate mapping unless already listed in `TFPayload/devMenu.cpp`.

## Current Runtime Anchors

```text
g_pGameManager                      Ghidra 0x0174b308, Uplay RVA 0x104b308
EditorManager                       [g_pGameManager + 0x104]
EntityManager                       [g_pGameManager + 0x0dc]
EditorSelectionManager              EditorManager + 0x28
Selection list head/map-ish storage SelectionManager + 0x04
Selection constraint map/list       SelectionManager + 0x24
Selection pivot/position            SelectionManager + 0x48 (vec3)
Selection rotation quaternion       SelectionManager + 0x54 (quat x,y,z,w)
Selection uniform scale/radius      SelectionManager + 0x64 (float)
Live variation object               SelectionManager + 0x8c
Variation sources                   SelectionManager + 0x90, +0x94, +0x98
```

Per selected object:

```text
selectedObject+0x04  movement state candidate
selectedObject+0x08  packed type/subtype candidate
selectedObject+0x0c  flags; bit 0x00010000 tracks physics enabled in tests
selectedObject+0x10  child mode/count-like field
selectedObject+0x18  children pointer
selectedObject+0x1c  parent/key
selectedObject+0x20  count/flags depending object shape
selectedObject+0x28  transform pointer used heavily by scene calls
selectedObject+0x44  scene holder/resource holder
```

Scene holder:

```text
sceneHolder+0x00  resource container
sceneHolder+0x08  flags
sceneHolder+0x1c  buoyancy candidate
sceneHolder+0x20  variation mask; observed values 1,2,4,8 map to variation 0..3
```

Scene/mesh object fields seen in the inspector:

```text
sceneObject+0x28  transform block
transform+0x14    position vec3
transform+0x20    rotation quaternion
transform+0x30    scale vector
mesh+0x90         unknown; writing detached the editor gizmo from the object
mesh+0x94         unknown; writing detached the editor gizmo from the object
mesh+0x98         unknown; writing detached the editor gizmo from the object
mesh+0x9c         unknown; writing detached the editor gizmo from the object
```

Entity manager maps that have been useful from runtime:

```text
EntityManager+0xe90  selected editor object -> backing scale object
EntityManager+0xea8  selected editor object -> editor transform object
EntityManager+0xe74  selected editor object -> editor visual object/map
```

## Engine-Native Calls Worth Using

### Upload object value serialization

```text
BuildTrackObjectLists
Uplay RVA 0x0a1c40, Steam RVA 0x1e1710

BuildObjectValuesJson
Uplay RVA 0xc20380, Steam RVA 0x65fa40
```

`BuildObjectValuesJson` emits an array of `{ objectName, values }` dictionaries
from the global object-values staging list at Uplay RVA `0x105125c`. Each object
record is `0x40` bytes; `record+0x08` is the object name C string,
`record+0x34` is the value count, and `record+0x3c` points to `0x3c`-byte value
records. Each value record has `+0x08` value name, `+0x34` value type, and
`+0x38` value pointer.

The first DEVELOPMENT_MODE `Trace upload values` probe hooked
`BuildObjectValuesJson` and dereferenced the staging records in the hook. That
caused access violations during publish, so the live button now only hooks
`BuildTrackObjectLists` and logs object/value list counts. Do not re-enable
name/type dereferencing in the publish path without moving it to a safer
post-call snapshot or a manual inspector action.

The trace now copies the flat `objectList` pointer vector and `valueList`
`0x50`-byte records into TFPayload-owned buffers immediately after
`BuildTrackObjectLists` returns. Use `Dump upload snapshot` after a publish
attempt to log raw bytes/dwords/floats for the last captured records without
following embedded pointers.

### Selected object scale delta

```text
EditorSelection_ScaleSelectedByDelta(selectionManager, float delta)
Uplay RVA 0x0f3db0
```

This is the editor's own selected-object scale delta path. It iterates
`selectionManager+0x04`, maps each selected object through
`[g_pGameManager+0xdc]+0xe90`, reads the current scalar from the mapped object,
and calls the clamped absolute scale setter
`EditorScaleBacking_SetUniformScaleAndRefresh`.

This is currently the safest scale call because it matches the editor's normal
input path. Native callers:

```text
UpdateObjectTransform @ 0x007b745f
Editor input handler FUN_00763f60 @ 0x007646a8
```

### Reset selected object scale

```text
EditorSelection_ResetSelectedScale(selectionManager)
Uplay RVA 0x0f3e20
```

Same iteration path as scale delta, but it sets every selected backing object to
the engine's default `1.0` constant through
`EditorScaleBacking_SetUniformScaleAndRefresh`.

Native caller:

```text
HandleEditorModeInputs @ 0x00761d2e
```

### Absolute backing scale setter

```text
EditorScaleBacking_SetUniformScaleAndRefresh(backingObject, float scale)
Uplay RVA 0x1fdc10
```

This setter clamps the input against an object-specific minimum read from a
property lookup, writes `backingObject+0x10`, then refreshes the transform path:

```text
FUN_008fd5e0(backingObject, 2)
ClampPositionToBounds([g_pGameManager+0xdc], ...)
backingObject vfunc+0x0c(position)
SceneObject_ApplyTransformAndRender(sceneObject, position, rotation, scale)
```

This is the right candidate for an absolute "set selected scale to X" control,
but it should be used via the `EntityManager+0xe90` backing object, not by
writing raw scene scale fields.

### Visual scene scale vector

```text
sceneObject vfunc+0x18(scaleVec3*)
```

The DEVELOPMENT_MODE selected-object inspector can call the scene object's
scale-vector vfunc directly. Passing `{s,s,s}` gives the current uniform visual
scale slider behavior; passing `{x,y,z}` gives non-uniform/axis visual scaling.
This is useful for live experimentation and appears to preserve the visual mesh
relationship when followed by the same refresh/nudge path as the uniform visual
scale control.

Runtime test note: using the uniform slider's selected-object nudge/refresh for
non-uniform scale made the axis values visibly apply and then snap back toward
the original scale. The axis control now writes `transform+0x30` before calling
the scene-object scale vfunc and refreshes only the mesh scene object, avoiding
the parent selected-object refresh that can reapply the editor's stored uniform
transform.

The inspector's global visual scale slider is now treated as a multiplier over
the current visual scale vector. After axis scaling to `{x,y,z}`, moving the
global slider from `a` to `b` applies ratio `b/a` to each component, preserving
the existing aspect ratio instead of replacing it with `{b,b,b}`.
The editor scale hotkeys use the same proportional axis-vector scaling path and
run from `DevMenu::UpdateRuntime`, so they do not depend on the mod menu window
being open.

2026-05-26 follow-up: visual scale writes now mark the editor transform dirty.
Running the selection constraint/bounds refresh here made selected objects
disappear during scale changes, so that path stays reserved for editor-native
scale operations until the non-uniform backing fields are identified.

This is still different from the editor's durable backing scale path above. It
does not replace `EditorScaleBacking_SetUniformScaleAndRefresh` for serialized
uniform object scale until we identify the editor-native non-uniform backing
object/constraint fields.

### Selection position/pivot update

```text
EditorSelection_MoveSelectedToClampedPosition(selectionManager, vec3 targetPosition implicit on stack)
Uplay RVA 0x0c7010
```

Ghidra's signature is poor, but the function clamps a target position, computes
a delta against `selectionManager+0x48`, applies that delta to each selected
object's current world position, and updates/creates constraints. It finishes by
updating selection constraint objects, not by directly writing scene transforms.

This is promising for "move selected object" controls, but the exact calling
convention needs a small native caller shim or more stack analysis before
calling directly from the mod menu.

### Selection rotation update

```text
SetObjectRotation(selectionManager, quat*)
Uplay address name in Ghidra: SetObjectRotation
```

Despite the name, this is selection-manager rotation. It iterates selected
objects, composes the new selection quaternion with each object's current
transform, updates/creates constraints, and stores the selection quaternion into
`selectionManager+0x54..0x60`.

This is a strong candidate for editor rotation controls once the desired input
is expressed as a quaternion.

### Physics constraint processing

```text
ProcessPhysicsConstraints(selectionManager, char updateViaMessage)
Uplay RVA 0x137c30
```

This consumes the selection constraint list/map and either pushes changes
directly to physics/entity manager structures or packages them into an editor
message when `updateViaMessage` is non-zero. Native editor transform paths call
this after position/rotation edits.

Use after direct selection-manager field changes only if the selection has valid
constraints. Prefer calling the higher-level move/rotate/scale paths first.

### Bounds and selection refresh

```text
EditorSelection_RefreshBoundsAndGizmo(selectionManager)
Uplay RVA 0x138ee0
```

Thin wrapper:

```text
FUN_007cbb40(selectionManager)
FUN_007cbc20(selectionManager)
FUN_007cc140(selectionManager)
```

This is a post-transform selection bounds/gizmo refresh. Native paths commonly
call it after `ProcessPhysicsConstraints`.

### Variation transform refresh

```text
EditorSelection_UpdateVariationTransform(selectionManager)
Uplay RVA 0x148250
```

Reads live variation object at `selectionManager+0x8c`, then pushes selection
position, rotation, and `selectionManager+0x64` scale into the variation preview:

```text
SceneObject_ApplyTransformAndRender(liveVariationObject, selection+0x48, selection+0x54, scaledVec3)
```

This is useful when editing the selected placement preview or rebuilding the
current object variation.

### Rebuild object variation

```text
EditorSelection_RebuildObjectVariation(selectionManager, int variationIndex)
Uplay RVA 0x14a570
```

Destroys the current live variation object at `selectionManager+0x8c`, clones or
creates the requested variation from `selectionManager+0x90 + index*4`, adds it
to the entity/world manager, then calls
`EditorSelection_UpdateVariationTransform` to sync transform.

Observed native uses:

```text
ConfirmObjectPlacement
SetEditorMode
RenderAnimatedTracks
```

### Editor visual refresh

```text
EditorManager_RefreshObjectVisual(editorManager, selectedObject)
Uplay RVA 0x096480
```

Looks up the visual/editor object in `EditorManager+0x650` and synchronizes its
scene transform from the selected object position. If the passed object is also
`EditorManager+0x768`, it updates cached editor camera/selection orientation
fields at `EditorManager+0x710..0x728`.

Native callers include `RenderGraphicsAndUpdateCamera`, icon/destructible visual
creation, and other editor visual maintenance. This is a refresh helper, not the
primary property setter.

### Scene graph collection

```text
SceneObject_CollectByType(sceneRoot, outSnapshot, type, subtype, useVirtualType, requiredFlags)
Uplay RVA 0x6a3000
```

Recursively traverses a scene tree and appends matching scene objects to
`outSnapshot.objects`, with the count at `outSnapshot+0x3ff0`. Type is the low
byte of `sceneObject+0x08`; subtype is either the high byte, `sceneObject+0x09`,
or vfunc `+0x80` depending object type and `useVirtualType`.

This is safe as a read-only inspector helper when the root object and output
buffer are valid.

### Mesh color-looking helper, not safe for selected-object color

```text
ApplyColorToMeshObjects(this, unused, colorObject)
Uplay RVA 0x105640
Steam RVA 0x244dd0
```

This engine helper iterates a list of object/scene entries, collects mesh scene
objects with `SceneObject_CollectByType(root, snapshot, 1, 3, 0, 0)`, then
writes four consecutive values from the color object into each mesh scene
object:

```text
mesh+0x90 = colorData+0x20
mesh+0x94 = colorData+0x24
mesh+0x98 = colorData+0x28
mesh+0x9c = colorData+0x2c
```

Despite the function name and write pattern, writing these fields directly from
the selected-object inspector did not behave like material color. Runtime test
result: changing one of these values detached the editor gizmo from the object.
Treat these as unknown transform/editor-coupled fields until the real call
context and object type are understood. Do not expose direct write controls for
`mesh+0x90..0x9c`.

## Input-Path Clues

`HandleEditorModeInputs` maps editor commands to the same functions above:

```text
command 0x62  -> EditorSelection_ResetNonUniformScaleConstraints(selectionManager)
                 reset non-uniform scale vectors
command 0x63/0x64 -> FUN_007ce130(..., 0x27a, bool)    unknown toggle/property
command 0x65/0x66 -> FUN_007ce5a0(..., 0x16c, bool)    unknown toggle/property
command 0x08  -> selection vfunc+0x2c(float)           rotate/scroll-style delta path
command 0x0d/0x0e -> reset scale path / another reset path
command 0x14/0x15 -> FUN_0084a740(selectionManager,+/-1) variation next/prev
command 0x0f/0x10 -> FUN_00849860(selectionManager, 0 or 5) mode/property switch
```

The generic editor input loop `FUN_00763f60` also routes mouse/key deltas through
selection-manager vfuncs:

```text
selection vfunc+0x2c(float)                 single float delta, used for scale/one-axis control
selection vfunc+0x30(float, float, float)   three-axis delta, likely move/rotate depending mode
EditorSelection_ScaleSelectedByDelta(selectionManager, float) selected scale delta
EditorManager_MarkDirtyFlags(editorManager, flags)            marks editor dirty/refresh flags
```

Those vfunc slots are worth resolving from a live selection-manager vtable before
hard-wiring more controls.

## Practical Mod-Menu Direction

Good next controls:

```text
Scale selected +/- delta:
  call EditorSelection_ScaleSelectedByDelta(selectionManager, delta)

Reset selected scale:
  call EditorSelection_ResetSelectedScale(selectionManager)

Set selected scale absolute:
  for each selected object:
    backing = lookup EntityManager+0xe90[selectedObject]
    call EditorScaleBacking_SetUniformScaleAndRefresh(backing, absoluteScale)

Set selected visual scale vector:
  call selected mesh scene object vfunc+0x18 with {x,y,z}
  refresh visual transform and nudge selected object to repaint

Refresh current variation preview:
  call EditorSelection_UpdateVariationTransform(selectionManager)

Change current variation:
  call EditorSelection_RebuildObjectVariation(selectionManager, variationIndex)

```

Higher-risk controls that need one more pass:

```text
Move selected:
  likely use EditorSelection_MoveSelectedToClampedPosition / selection vfunc+0x30,
  then ProcessPhysicsConstraints + EditorSelection_RefreshBoundsAndGizmo

Rotate selected:
  use SetObjectRotation(selectionManager, quat*), then
  ProcessPhysicsConstraints + EditorSelection_RefreshBoundsAndGizmo

Toggle object/editor flags:
  inspect FUN_007ce130 and FUN_007ce5a0 before exposing them
```

## Open Questions

1. Resolve the selection-manager vtable slots from a live object:
   `+0x2c`, `+0x30`, `+0x17c`, `+0x184`, `+0x188`.
2. Confirm whether `sceneHolder+0x08` bit `0x08` is the durable visibility bit
   or only an editor/runtime display flag.
3. Confirm the exact relation between `selectedObject+0x0c` bit `0x10000` and
   physics-enabled state after toggling physics in the editor.
4. Map Steam RVAs for every newly documented Uplay-only function before enabling
   controls outside DEVELOPMENT_MODE.
