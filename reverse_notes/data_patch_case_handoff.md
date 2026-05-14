# Trials Fusion `data_patch.pak` Case Handoff

This document summarizes the reverse-engineering work completed so far on `data_patch.pak`, the runtime hook work used to observe package loading, and the current blocker for extracting the remaining assets.

## Scope

Primary target:

- `F:\Trials Fusion\build\data_pc\data_patch.pak`

Primary goal:

- Understand how the game loads `.pak` entries
- Determine where protection/decryption/decompression is removed
- Correlate runtime reads back to pak entries
- Dump usable plaintext assets by name

## What We Learned About `data_patch.pak`

We inspected the pak structure and found:

- File magic: `0x12345678`
- Total entries: `544`
- Entry record size: `17 bytes`

Entry layout:

```text
u32 hash
u32 storedSize
u32 expandedSize
u8  flags
u32 offset
```

Observed flag behavior:

- `0x11` = protected + compressed
- `0x10` = protected raw
- `0x00` = plaintext

Additional observations:

- For `0x10` entries, `storedSize = expandedSize + 128`
- There is one plaintext manifest-like entry at the end of the pak
- That manifest maps pak entry indices to filenames

This was used to generate:

- [data_patch_inventory.csv](F:/VSProjects/Trials-Fusion-Mod/reverse_notes/data_patch_inventory.csv)

That inventory gives:

- pak index
- asset path
- pak offset
- stored size
- expanded size
- flags

## Ghidra Findings

Important anchors identified in Ghidra:

- `LoadGameDataPackages @ 00ab2ad0`
- `file_read_inflate @ 00ec14f0`
- `FUN_00ec0ef0` appears related to cached stream creation/return
- package manager singleton around `DAT_01755298`

The major takeaway is that `file_read_inflate` sits downstream of the pak protection layer. By the time data reaches that function, the content is already usable.

## Runtime Hook Work

We added a runtime hook module to the mod project:

- [pak-runtime-hook.cpp](F:/VSProjects/Trials-Fusion-Mod/TFPayload/pak-runtime-hook.cpp)
- [pak-runtime-hook.h](F:/VSProjects/Trials-Fusion-Mod/TFPayload/pak-runtime-hook.h)

And registered it from:

- [dllmain.cpp](F:/VSProjects/Trials-Fusion-Mod/TFPayload/dllmain.cpp)

This was built and deployed through the existing Trials Fusion mod injection pipeline so the hook could run inside the live game.

## What the Runtime Hook Proved

Hooking `file_read_inflate` showed that the buffers reaching that layer are not opaque encrypted data.

Observed runtime signatures included:

- `DSIG`
- `GDEF`
- `FWS`
- `CWS`
- `FSB5`

That means the game is already doing the important decode/protection/decompression work before our read hook sees the bytes.

This is the central proof that runtime plaintext extraction is viable.

## Correlating Runtime Streams Back to Pak Entries

The hook was extended to log stream metadata such as:

- stream base offset
- stream size
- internal stream fields
- source offset

We compared that against `data_patch_inventory.csv` and produced strong matches between runtime streams and `data_patch.pak` entries.

Output:

- [data_patch_stream_matches.csv](F:/VSProjects/Trials-Fusion-Mod/reverse_notes/data_patch_stream_matches.csv)

Strong matches included:

- `bikes/bike_donkey.xml`
- `bikes/bike_unicorn.xml`
- `pack20/items.xml`
- `pack20/tracks.xml`
- `objectcollection20.xml`
- `patch20/characters/podium_rider.gfx`
- `vtex/vtex_objects0.raw.hdr`

Important result:

- runtime `stream_base` matches pak entry offset
- runtime `stream_size` matches plaintext/expanded payload size

This gives us a reliable identity bridge from runtime stream objects back to named pak entries.

## Named Dump Pipeline

Once stream identity correlation was working, the hook was upgraded to dump named payloads.

Output directory:

- `F:\Trials Fusion\datapack\pak_runtime\data_patch_named`

This successfully produced named extracted files including:

- `.xml`
- `.gfx`
- `.csv`
- `.hdr`

Spot checks confirmed valid plaintext content:

- XML files were readable config/XML
- CSV files were readable semicolon-delimited data
- GFX dumps had plausible headers such as `GFX2`

## Coverage Analysis

We added analysis scripts:

- [analyze_pak_runtime.ps1](F:/VSProjects/Trials-Fusion-Mod/reverse_notes/analyze_pak_runtime.ps1)
- [check_data_patch_dump_coverage.ps1](F:/VSProjects/Trials-Fusion-Mod/reverse_notes/check_data_patch_dump_coverage.ps1)

Coverage report:

- [data_patch_dump_coverage.csv](F:/VSProjects/Trials-Fusion-Mod/reverse_notes/data_patch_dump_coverage.csv)

Latest known coverage:

- `250` entries `ok`
- `39` entries `partial`
- `254` entries `missing`

By type:

- `.gfx`: mostly recovered
- `.xml`: many recovered, some still missing
- `.csv`: recovered
- `.hdr`: recovered
- `.mdl`: primary unresolved category
- `.tex`: unresolved
- `.trk`: unresolved

## Higher-Level Package Manager Hooking

We also hooked higher-level package-manager vtable slots, especially the natural open-by-name path referred to in notes as `vtable_24`.

This allowed logging of actual asset requests made by the game, including:

- raw names such as `bikes/bike_donkey.xml`
- prefixed names such as `<evo2_devfiles>patch20/...`

This confirmed that the game naturally requests DLC/cosmetic assets by virtual name and that we are intercepting the right loading path.

Related runtime output:

- `F:\Trials Fusion\datapack\pak_runtime\pak_package_api.csv`
- `F:\Trials Fusion\datapack\tfpayload_log.txt`

## Current Blocker

We now have a precise blocker:

- The package-open hook can see missing assets being requested by name
- We can match those names back to `data_patch.pak`
- But the object returned by the package-open function is not directly readable using our current `file_read_inflate` call

Observed symptom:

```text
Natural stream matched ... attempting full dump
Natural stream read mismatch ... 0 / expected_size
```

Interpretation:

- the object returned by `vtable_24` is not the final read-stream object, or
- it wraps another object that later becomes the true `file_read_inflate` stream, or
- another method or transformation is required before it can be read through the current hook path

So the problem is no longer “how is the pak protected?” The problem is now “how does the package-open return object relate to the actual readable stream object?”

## Dapper Gent Test Case

We specifically checked whether the in-game “dapper gent” assets were being touched.

Observed natural opens:

- `objects/pack20/dapper_head_722249001.mdl`
- `objects/pack20/dapper_torso_2302193317.mdl`
- `objects/pack20/dapper_legs_3202365285.mdl`

Latest dump status:

- `dapper_torso_2302193317.mdl` = `partial`
  - expected size: `436,527`
  - dumped size: `160,165`
- `dapper_head_722249001.mdl` = `missing`
- `dapper_legs_3202365285.mdl` = `missing`
- `dapper_pug_3112972161.mdl` = `missing`

This is useful because it proves:

- the game is naturally loading the dapper assets
- our hook is seeing those opens
- the remaining failure is at the object-to-stream handoff, not at asset discovery

## Best Current Conclusion

At this point, the core case has been established:

1. `data_patch.pak` becomes readable plaintext at runtime
2. The game performs protection/decompression before data reaches `file_read_inflate`
3. Runtime extraction is real and already working for many entries
4. Runtime stream metadata can be correlated back to concrete pak filenames
5. Named extraction works for many assets
6. The remaining challenge is extracting complete `.mdl`, `.tex`, and `.trk` content by understanding the relationship between package-open returns and the actual read-stream object

## Most Useful Files For Another Researcher

Start here:

- [data_patch_inventory.csv](F:/VSProjects/Trials-Fusion-Mod/reverse_notes/data_patch_inventory.csv)
- [data_patch_dump_coverage.csv](F:/VSProjects/Trials-Fusion-Mod/reverse_notes/data_patch_dump_coverage.csv)
- [data_patch_stream_matches.csv](F:/VSProjects/Trials-Fusion-Mod/reverse_notes/data_patch_stream_matches.csv)
- [data_patch_findings.md](F:/VSProjects/Trials-Fusion-Mod/reverse_notes/data_patch_findings.md)
- [pak-runtime-hook.cpp](F:/VSProjects/Trials-Fusion-Mod/TFPayload/pak-runtime-hook.cpp)

Runtime artifacts:

- `F:\Trials Fusion\datapack\tfpayload_log.txt`
- `F:\Trials Fusion\datapack\pak_runtime\pak_package_api.csv`
- `F:\Trials Fusion\datapack\pak_runtime\data_patch_named\`

## Recommended Next Step

The clearest next step is:

- instrument the object returned by package-manager `vtable_24`
- compare it to the `this_ptr` later observed in `file_read_inflate`
- determine whether the open-return object contains, transforms into, or references the true readable stream object

That is the missing link for converting the currently touched `.mdl` assets from `partial` or `missing` into full dumps.
