# Trials Fusion Object Loading Deep Dive

Date: 2026-05-12

This is the condensed working reference for custom object injection, package
loading, and runtime object/GFX reloading in Trials Fusion.

## Current Status

The runtime reload path is confirmed working for custom codex objects:

1. F12 evicts captured custom `.gfx` entries from the parsed GFX cache.
2. The game reopens the `.gfx` through the package manager.
3. The resource stream hook patches selected GFX transform floats in memory.
4. `LoadAndCacheGfxResource` parses a new resource.
5. The new non-null parsed resource pointer is published into the captured
   object owner's `owner+0x68` scene/resource slot.
6. Newly placed editor objects use the modified resource.

Confirmed behavior:

```text
#codex_custom_log# toggles between 1x and 5x scale at runtime.
#codex_custom_half_ramp# now participates once both custom targets are captured.
F12 handles captured custom/codex .gfx targets as a group instead of only the
latest captured path.
```

The remaining major task is turning the current hardcoded GFX byte patch into a
general runtime editing/reload tool for arbitrary object changes.

## Useful Files

```text
TFPayload/pak-runtime-hook.cpp
TFPayload/pak-runtime-hook.h
reverse_notes/trials_object_loading_deepdive.md
F:/Trials Fusion/datapack/tfpayload_log.txt
```

## Pak Container Format

Every inspected pak begins with:

```text
u32 magic       = 0x12345678
u32 data_start  = 12 + entry_count * 17
u32 entry_count
```

Each table record is 17 bytes:

```text
u32 path_hash
u32 stored_size
u32 unpacked_size
u8  flags
u32 data_offset  ; absolute file offset
```

The final table entry is a filename list:

```text
u32 count
repeat count:
  u16 byte_len
  byte[byte_len] utf8_path
```

The filename list maps to table entries by order. The table itself stores only
hashes, sizes, flags, and offsets.

Normal flags:

```text
0x00 = raw/stored
0x01 = zlib-compressed
```

Protected flags observed in `build/data_pc/data_patch.pak`:

```text
0x10 = protected/encrypted; stored_size is often unpacked_size + 128
0x11 = protected/encrypted plus compression or protected compressed payload
```

`data_patch.pak` / pack20 is the worst first target for custom injection because
its protected entries do not inflate as plain zlib after the 128-byte overhead.

## G4 Path Hash

Pak path hashes are not CRC32. They match the G4 hash family. Pak paths are
hashed uppercase, usually without a leading slash.

```python
def g4_hash_path(path: str) -> int:
    b = path.upper().encode("ascii")
    seed = len(b)
    h = 0
    for ch in b:
        h = (h + ch * seed) & 0xffffffff
        seed = ((seed & 0xffff) * 18000 + (seed >> 16)) & 0xffffffff
    return h
```

Verified examples:

```text
AREAVOLUMES.XML                              -> 0x721a1da8
OBJECTCATEGORY.XML                           -> 0xef6ea347
OBJECTCOLLECTION.XML                         -> 0x6b108937
OBJECTCOLLECTION20.XML                       -> 0xe583a04e
PACK20/OBJECTS.XML                           -> 0x18630d86
OBJECTS/PACK20/AIDEN_BOTTOM_1669118754.MDL   -> 0xc8711195
```

Property names use the same hash logic without uppercasing:

```text
name        -> 0xca82e140
id          -> 0x0036ef52
category    -> 0xf71d1550
subcategory -> 0x7335dcd5
package     -> 0x6a582b65
directory   -> 0xeb739b94
packageID   -> 0xa4016384
surface     -> 0xd955d183
meshes      -> 0xa4512b1b
source      -> 0x3f427ca3
```

## DLC Layout

Official DLC packs use:

```text
datapack/DLCContent/packN/data/configs.pak
datapack/DLCContent/packN/data/dataN.pak
```

Config-side files:

```text
objectcategoryN.xml
packN/objectgroups.xml
packN/objects.xml
packinfo/pack.xml
packinfo/packN.xml
```

Data-side files:

```text
objectcollectionN.xml
objects/packN/*.mdl
```

Important: official `packN/objects.xml` files are mostly empty `ObjectList`
shells. The editor/game object definitions live in `objectcollectionN.xml`.

Pack20 is special-cased in `build/data_pc/data_patch.pak`, not in
`datapack/DLCContent/pack20`.

## Binary Objectcollection Format

Despite the `.xml` extension, decompressed `objectcollection*.xml` is a binary
property tree. Real files start with binary tokens, for example:

```text
01 00 54 c7 c4 68 00 01 ...
```

Parser functions observed in Ghidra imply:

```text
file:
  node

node:
  node_name
  attributes
  children

node_name:
  u8 marker              ; observed 1 for real nodes
  u8 name_len
  if name_len == 0:
    u32 name_hash
  else:
    byte[name_len] name

attributes:
  repeat:
    u8 present
    if present == 0: stop
    attr_key
    u8 value_type
    u16 value_len
    value bytes

attr_key:
  u8 key_len
  if key_len == 0:
    u32 key_hash
  else:
    byte[key_len] key

children:
  repeat:
    u8 present
    if present == 0: stop
    node
```

Useful value types:

```text
3 = int32
4 = int32-like enum/value
5 = float32
6 = string
```

## Custom Object Injection Lessons

Adding a custom object is not just a text edit. A valid object entry includes:

```text
public editor object record
generated child records for surfaces/LODs/bounds
mesh offsets and material IDs
atlas/icon coordinates
container metadata derived from the .mdl/.gfx
active content-pack/category registration
```

The successful custom test path uses pack6/contentpack6 and codex-named objects,
because pack6 is a normal DLC-style content pack and avoids pack20 protection.

Current custom runtime targets:

```text
<evo2_devfiles>contentpack6/manmade/props/codex/codex_custom_log.gfx
<evo2_devfiles>contentpack6/manmade/props/codex/codex_custom_half_ramp.gfx
```

The runtime GFX patch currently searches stream buffers for:

```text
codex_mesh_log1
codex_mesh_0001
```

When found, it patches three float scale values located at:

```text
mesh_name_offset - 48
mesh_name_offset - 44
mesh_name_offset - 40
```

The F12 toggle alternates those scale floats between:

```text
1.0
5.0
```

## Ghidra Addresses

Base used in these notes:

```text
GHIDRA_BASE_UPLAY = 0x00700000
```

Important functions:

```text
0x00a98800  LoadObjectCollectionAndInitializeGameplay
0x00a958f0  LoadObjectCollectionXML
0x00a94290  LoadAndCacheTextureFromFile
0x00e37880  LoadAndCacheGfxResource
0x00d85e60  LoadWorldMeshScene
0x00d7cc80  ObjectLoaderJob_Constructor
0x00d7dd30  InsertHashTable
0x00d81c80  CreateObjectResourceAndQueueLoaderJob
0x00d80070  WaitForMaterialLoad
```

Important globals:

```text
0x01755298  package manager global
0x017563d0  pending ObjectLoaderJob table area
0x017563e0  pending ObjectLoaderJob entries/table used by owner+0x1c lookup
0x017563b4  lock used around pending object-loader table
```

Package manager vtable slots:

```text
vtable+0x20  Pkg20-style open
vtable+0x24  Pkg24-style open
```

Resource stream vtable slots:

```text
vtable+0x1c  read-at
vtable+0x20  read-current
```

## Loader Flow

Startup/content flow:

```text
LoadGameDataPackages
  mounts data.pak
  special-cases data_patch.pak
  mounts other base paks
  discovers/registers DLCContent/*

LoadObjectCollectionAndInitializeGameplay
  emits BeforeLoadObjectCollection
  calls LoadObjectCollectionXML
  initializes gameplay/track systems
  emits AfterLoadObjectCollection

LoadObjectCollectionXML
  loads objectcollection.xml
  numbered loader attempts objectcollection1.xml through objectcollection256.xml
  category/group/music/seed/audio/stamp collections load per active pack ID
```

Pack IDs are gated by:

```text
LoadObjectCategoryDefinitions(pack_id)
PopulateContentPackIdArray(...)
GetContentPackStatus(...)
```

Inventing `objectcollection99.xml` is not enough. The pack/content ID must be
known and active enough for category/editor registration.

## Object Resource Lifecycle

The key object scene pointer is:

```text
owner+0x68 = completed scene/resource pointer
owner+0x1c = key used in pending ObjectLoaderJob table
owner+0x18 = package/content id-like value
```

Observed lifecycle:

```text
CreateObjectResourceAndQueueLoaderJob @ 0x00d81c80
  allocates the 0x6c owner/resource object
  creates ObjectLoaderJob via ObjectLoaderJob_Constructor @ 0x00d7cc80
  inserts job into DAT_017563e0 keyed by owner+0x1c
  starts/schedules the job through vtable methods

LoadWorldMeshScene @ 0x00d85e60
  locks DAT_017563b4
  looks up owner+0x1c in DAT_017563e0
  tombstones the matching pending-job entry
  builds the scene
  writes owner+0x68

WaitForMaterialLoad @ 0x00d80070
  returns immediately if owner+0x68 is non-null
  if owner+0x68 is null, looks up the pending job and waits/yields until filled
```

Important lesson: `owner+0x68` is not an independent cache slot. It is the
completion result of an object loader job.

## Unsafe Paths

Do not null `owner+0x68` directly from F12.

Test result:

```text
F12 set owner+0x68 to 0 for a captured codex object.
Opening the editor object menu afterward froze the game/editor.
```

Interpretation:

```text
WaitForMaterialLoad expects a matching pending ObjectLoaderJob when owner+0x68
is null. Clearing the pointer without also queuing/scheduling a valid job can
leave the editor waiting for a scene that will never be repopulated.
```

The current working path writes only a non-null newly parsed resource into
`owner+0x68`.

## Runtime Hook Implementation

Current hooks:

```text
LoadObjectCollectionAndInitializeGameplay
LoadObjectCollectionXML
LoadAndCacheTextureFromFile
LoadAndCacheGfxResource
LoadWorldMeshScene
ObjectLoaderJob_Constructor
InsertHashTable
PackageApi20
PackageApi24
ResourceStreamReadAt
ResourceStreamReadCurrent
```

Current F12 group-reload behavior:

```text
1. Track custom/codex .gfx targets by path.
2. For each target, store:
   - GfxResource cache manager
   - cache entry/hash/index/capacity/count address
   - LoadAndCacheGfxResource param2
   - WorldMeshScene input
   - scene owner
   - owner key
   - last scene/resource pointer
3. On F12, if captured targets have both cache and scene info:
   - toggle the runtime scale once
   - tombstone each captured GFX cache row
   - decrement the relevant table count
   - force `LoadAndCacheGfxResource`
   - patch stream-read buffers while that reload is active
   - publish parsed resource to that target's owner+0x68
```

Useful expected logs:

```text
[PakRuntime] Reloading captured codex GFX target group count=...
[PakRuntime] Evicted captured codex GFX path=...
[PakRuntime/GfxPatch] path=... mesh=... name_abs=... scale=...
[PakRuntime] Forced captured codex GFX reload path=... parsed_resource=...
[PakRuntime] Captured codex GFX target group complete success=... attempted=...
```

## Proven Runtime Reload Pipeline

Minimal successful path:

```text
F12
  -> evict parsed GFX cache row
  -> force LoadAndCacheGfxResource(cacheManager, path, param2)
  -> PackageApi24 opens the .gfx
  -> ResourceStreamRead* patches bytes in the read buffer
  -> parser returns a new parsed_resource
  -> write parsed_resource to owner+0x68
  -> newly placed editor object uses changed visual/scene resource
```

Representative successful log shape:

```text
[PakRuntime] Evicted captured codex GFX path=<evo2_devfiles>...codex_custom_log.gfx
[PakRuntime/Pkg24] ... path=<evo2_devfiles>...codex_custom_log.gfx result=...
[PakRuntime/GfxPatch] ... mesh=codex_mesh_log1 ... scale=5
[PakRuntime] Forced captured codex GFX reload path=... parsed_resource=...
  runtime_patch=yes runtime_scale=5.0
  publish_reloaded_gfx=yes scene_after_publish=...
```

## What Did Not Work

Top-level objectcollection replay:

```text
Calling LoadObjectCollectionXML naked after startup missed required context and
could throw 0xc0000005. TFPayload initializes after the natural startup context
has already passed, so the useful live-reload rung is lower-level resource/cache
handling.
```

GFX cache eviction alone:

```text
Evicting/reloading the parsed GFX cache produced a new parsed_resource, but the
editor object owner kept pointing at its old owner+0x68 resource. New placements
still used the stale scene until owner+0x68 was updated.
```

Null invalidation:

```text
Clearing owner+0x68 froze the editor because no valid pending ObjectLoaderJob was
queued to refill it.
```

Latest-target-only F12:

```text
The editor/object menu may load several codex objects. The latest captured path
can be `codex_custom_log.gfx` even if the user just placed the half-ramp. Group
targeting fixed this by applying F12 to all captured custom/codex GFX targets.
```

## Next Good Directions

1. Generalize the GFX patcher.

   Instead of hardcoding mesh names and offsets, parse enough GFX structure to
   locate transform matrices/scales robustly, or load patch instructions from a
   small sidecar/debug config.

2. Add explicit target controls.

   Group reload works for the current custom test set. A future UI/hotkey could
   cycle/select a single captured path, print the active target, or reload all.

3. Explore safe object-owner refresh.

   The direct non-null publish works for new placements. A cleaner engine-native
   path would recreate the ObjectLoaderJob insertion/start sequence or rerun the
   XML object-resource creation path.

4. Investigate `.mdl` and physics/collision reload.

   Current proof changes GFX visuals. A complete runtime object editor may need
   matching MDL/collision/surface/resource-owner invalidation.

5. Preserve package remount knowledge.

   Replacing files inside `data6.pak` while the game is running may still be
   blocked by mounted package/file-table state. The current runtime byte patch
   avoids that by modifying stream buffers after the game opens the resource.

## Practical Test Recipe

Use this when validating a new payload:

```text
1. Start the game/editor.
2. Open/place #codex_custom_log# and #codex_custom_half_ramp# once.
3. Press F12.
4. Confirm logs:
   [PakRuntime] Reloading captured codex GFX target group count=...
   [PakRuntime] Forced captured codex GFX reload path=...codex_custom_log.gfx
   [PakRuntime] Forced captured codex GFX reload path=...codex_custom_half_ramp.gfx
5. Place both objects again.
6. Expected: both toggle between normal scale and 5x scale on each F12.
```

## Build/Install Notes

Known working build command:

```text
MSBuild ProxyDLL.sln /p:Configuration=RELEASE_AUTOLOAD /p:Platform=x86 /m:1
```

The release-autoload build outputs:

```text
RELEASE_AUTOLOAD/dbgcore.dll
RELEASE_AUTOLOAD/TFPayload.dll
```

These are copied into:

```text
F:/Trials Fusion/datapack/
F:/SteamLibrary/steamapps/common/Trials Fusion/datapack/
```

The build may warn:

```text
LNK4098: defaultlib 'LIBCMT' conflicts with use of other libs
```

That warning has not blocked the runtime reload work.
