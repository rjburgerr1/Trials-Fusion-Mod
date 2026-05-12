# GFX / MDL Structure Notes

These notes capture the current useful structure around the bytes we patch for
runtime visual scaling.

## Tools

`reverse_notes/tools/tf_struct_probe.py` can dump and compare binary structures
around ASCII references.

Useful commands:

```text
python reverse_notes/tools/tf_struct_probe.py gfx reverse_notes/out/codex_custom_log.gfx codex_mesh_log1
python reverse_notes/tools/tf_struct_probe.py mdl reverse_notes/out/logs_01_3321526194.mdl
python reverse_notes/tools/tf_struct_probe.py window reverse_notes/out/codex_custom_log.gfx codex_mesh_log1 --start -160 --end 180
```

## GFX Mesh Reference Record

The runtime visual scale patch modifies a per-mesh-reference transform record in
`.gfx` files. The anchor is the ASCII mesh/object name inside the GFX payload.

Relative to the mesh name offset:

```text
-84  float translation.x
-80  float translation.y
-76  float translation.z

-48  float scale.x
-44  float scale.y
-40  float scale.z

-36  float rotation quaternion.x
-32  float rotation quaternion.y
-28  float rotation quaternion.z
-24  float rotation quaternion.w

-20  float radius_or_extent-like value
-16  uint32 variation/hash-like value
-12  uint32 resource/object id-like value
 -4  uint32 name length
  0  ASCII mesh/object name
```

Examples:

```text
codex_custom_log.gfx / codex_mesh_log1
  translation = 0,0,0
  scale       = 1,1,1
  rotation    = 0,0,0,1
  radius-ish  = 1.82432
  resource id = 0xdfb81485

codex_custom_cloned_mesh_ref_variation1_scale3.gfx / codex_mesh_0001
  scale       = 3,3,3

codex_custom_cloned_mesh_ref_variation1_move2_rot90.gfx / codex_mesh_0001
  translation = 2,0,0
  rotation    = 0,0,0.707107,0.707107
```

Stock `wooden_ramp.gfx` has one record per variation:

```text
woodenramp      radius-ish 3.17182
woodenramp_u15  radius-ish 3.17046
woodenramp_u30  radius-ish 3.15498
woodenramp_u45  radius-ish 3.13556
woodenramp_u60  radius-ish 3.09432
woodenramp_u75  radius-ish 3.04153
woodenramp_u90  radius-ish 2.9722
```

Interpretation so far:

```text
This transform affects rendered mesh placement inside the loaded GFX resource.
It does not move editor pick bounds or collision. That matches observed behavior:
after visually scaling/moving the broken-wall mesh, the editor cursor cannot
reclick the visual object and the ramp collision stays at the original placement.
```

## MDL Surface Records

The `.mdl` files start with an `OBJ\x01VER01` header and contain repeated
surface records. These records are not 4-byte aligned because they contain
variable-length strings.

Initial header:

```text
0x00  "OBJ\x01VER01"
0x09  uint32 unknown, observed 11
0x0d  byte/string marker
0x0e  "LRS01"
0x13  uint32 LOD/surface count
0x17  byte/string marker
0x18  "LR005"
0x1d  float model_scale.x
0x21  float model_scale.y
0x25  float model_scale.z
```

For the first `visible` surface, after the length-prefixed name:

```text
name+0x10  float range / LOD distance
name+0x18  float bounds.min.x
name+0x1c  float bounds.min.y
name+0x20  float bounds.min.z
name+0x24  float bounds.max.x
name+0x28  float bounds.max.y
name+0x2c  float bounds.max.z
name+0x38  uint16 material string length
name+0x3a  ASCII material id string
after material: flags/metadata then compressed geometry
```

Examples:

```text
woodenramp_2594257364.mdl
  LOD count = 3
  model scale = 1,1,1
  visible LOD0 range = 64
  bounds = (-3, -0.0652, -1.0277) -> (3, 0.0652, 1.0277)
  material = 863135581

logs_01_3321526194.mdl
  LOD count = 4
  visible LOD0 range = 16
  bounds = (-0.5436, -0.531, -3) -> (0.5177, 0.5174, 3)
  material = 1461996828

broken_brickwall_03_3915179641.mdl
  LOD count = 2
  visible LOD0 range = 16
  bounds = (-1.94701, -0.753876, -0.234182) -> (1.92817, 0.765998, 0.230108)
  material = 3703809648
```

## Objectcollection Mirror

The generated `objectcollection6` mesh/container metadata mirrors the MDL surface
bounds. For example:

```text
codex_mesh_log1_3753383045.mdl
  MDL/objectcollection bounds:
  (-0.5436, -0.531, -3) -> (0.5177, 0.5174, 3)

codex_mesh_0001_302248137.mdl
  MDL/objectcollection bounds:
  (-1.9470059, -0.7538759, -0.234182)
  -> (1.928173, 0.765998, 0.230373)
```

The objectcollection attrs currently decoded only by hash are:

```text
#0041ebe6  bounds.min.x
#00427886  bounds.min.y
#00430526  bounds.min.z
#0041ebca  bounds.max.x
#0042786a  bounds.max.y
#0043050a  bounds.max.z
```

Mesh child records repeat similar bounds under:

```text
#dd4cf1a6  mesh.min.x
#dd4dc496  mesh.min.y
#dd4e9786  mesh.min.z
#173c7ca6  mesh.max.x
#173d4f96  mesh.max.y
#173e2286  mesh.max.z
```

## Current Hypothesis

Runtime GFX transform patching is a render-only path. Collision and editor
selection likely come from one or more of:

```text
1. objectcollection collision profile and object metadata
2. objectcollection generated bounds/container/mesh metadata
3. MDL visible surface bounds and compressed geometry
4. placed editor object instance transform, if available at runtime
```

Observed behavior favors this split:

```text
- A broken-wall custom object can carry the stock Ramp_CP6 collision profile.
- Scaling the GFX mesh does not scale or translate that collision.
- The scaled visual cannot be clicked with the editor cursor at its new visual
  bounds, implying editor pick bounds did not follow the GFX transform.
```

## Next Experiments

1. Static scale test:
   Patch both the GFX transform scale and the matching objectcollection/MDL
   bounds for a custom object, then load from a rebuilt pack.

   Built test pack:

   ```text
   python reverse_notes/tools/tf_static_scale_pack.py build reverse_notes/out/data6_codex_static_scale3_bounds.pak --scale 3
   ```

   Important: this builder uses
   `objectcollection6_codex_custom_half_ramp.bin` as its objectcollection
   baseline. Using stock `objectcollection6.bin` drops the placeable
   `codex_custom_half_ramp` object entry, leaving the custom object absent from
   the editor.

   Outputs:

   ```text
   reverse_notes/out/codex_mesh_0001_static_scale3.gfx
   reverse_notes/out/codex_mesh_0001_static_scale3.mdl
   reverse_notes/out/objectcollection6_codex_mesh_0001_static_scale3.bin
   reverse_notes/out/data6_codex_static_scale3_bounds.pak
   ```

   The pack patches:

   ```text
   GFX codex_mesh_0001 transform scale = 3,3,3
   MDL visible surface bounds for both LOD records *= 3
   objectcollection container bounds *= 3
   objectcollection generated mesh bounds for 4 mesh records *= 3
   ```

2. Bounds-only test:
   Patch only objectcollection/MDL bounds and see whether editor pick/collision
   changes while the visual stays unchanged.

3. Instance-transform search:
   Instrument placed editor object creation/selection and look for runtime
   transforms that affect visual, collision, and picking together.

## Control Packs

Codex custom slot using stock half-ramp mesh and collision:

```text
python reverse_notes/tools/tf_codex_stock_half_ramp_pack.py build reverse_notes/out/data6_codex_stock_half_ramp_mesh_collision.pak
```

This rebuilds `codex_custom_half_ramp` from the stock `wooden_ramp_half`
objectcollection metadata while preserving the Codex object name, ID, and GFX
path. It also writes the stock `wooden_ramp_half.gfx` payload behind
`contentpack6/manmade/props/codex/codex_custom_half_ramp.gfx`.

Expected result:

```text
Codex object name/path remains custom.
Rendered mesh is wooden_ramp_half.
Collision profile is TrialsFusion/ObjectCollisions/Wood/Ramp_CP6.
Variation metadata matches stock wooden_ramp_half.
```

Static pre-scaled GFX control:

```text
python reverse_notes/tools/tf_codex_stock_half_ramp_pack.py build reverse_notes/out/data6_codex_stock_half_ramp_static_gfx_scale3.pak --gfx-scale 3
```

This keeps the same stock half-ramp objectcollection metadata and collision
profile, but writes scale `3,3,3` into all six `woodenramp_half*` mesh refs in
the Codex GFX payload before packing. Use it to distinguish static GFX transform
effects from runtime GFX cache reload/publish effects.
