# data_patch.pak findings

Source file: `F:\Trials Fusion\build\data_pc\data_patch.pak`

## Container layout

- Magic: `0x12345678`
- Table end: `0x242c` / `9260`
- Table entries: `544`
- Entry size: `17` bytes
- Final 128 bytes are an archive-level signature block.

Each table entry is:

```text
u32 hash
u32 storedSize
u32 expandedSize
u8  flags
u32 offset
```

## Flags

- `0x11`: protected and compressed
- `0x10`: protected and not compressed
- `0x00`: unprotected manifest entry

There are 270 `0x11` entries, 273 `0x10` entries, and one `0x00` entry.

The `0x10` entries always have `storedSize = expandedSize + 128`, which suggests
the protection layer adds a fixed 128-byte signature/authentication block to
raw payloads. The `0x11` entries are protected compressed payloads; unlike
unprotected archives, their stored bytes do not begin with a zlib header.

## Manifest

Entry `543` is plaintext and unprotected:

- Hash: `0xDD9D93E8`
- Size: `28310`
- First dword: `543`, matching the protected-entry count
- Body: length-prefixed ASCII file names

The manifest order matches table entries `0..542`. The extracted inventory is
saved at `reverse_notes/data_patch_inventory.csv`.

## Content summary

- 262 `.mdl`
- 227 `.gfx`
- 42 `.xml`
- 7 `.tex`
- 2 `.trk`
- 2 `.csv`
- 1 `.hdr`

The paths are largely DLC/patch pack 20 content, for example
`objects/pack20/...`, `patch20/...`, `localization/dlc20_...`, and
`config20.xml`.

## Ghidra anchors

- `LoadGameDataPackages @ 00ab2ad0` mounts `data_patch.pak`.
- Package manager singleton: `DAT_01755298`.
- `file_read_inflate @ 00ec14f0` handles package read/decompression.
- `FUN_00ec3dd0` reads a 128-byte signature block from a supplied offset.
- `FUN_00ec3bc0` hashes package bytes up to that signature offset.
- `FUN_00ec4080` creates `PackFilesSignVerifier`.

Current interpretation: `data_patch.pak` is not merely compressed like
`data.pak`; payload entries are behind an additional protected transform marked
by flag bit `0x10`. The plaintext manifest gives names and ordering, so the next
step is to hook the package-manager read path after the protection layer and
before/after inflate to dump resolved payload bytes by manifest name.
