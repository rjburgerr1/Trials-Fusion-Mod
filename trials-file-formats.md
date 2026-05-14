# Trials Fusion File Formats

This is a working reference for proprietary Trials Fusion file formats observed
while investigating custom object loading. It is intentionally byte-oriented:
fields are written in file order, with known signatures, stream behavior, and
open questions called out explicitly.

Unless noted otherwise, multi-byte integers are little-endian.

## Current Coverage

| Format | Extension | Status | Notes |
| --- | --- | --- | --- |
| Package archive | `.pak` | Mostly mapped | Header, entry table, flags, offsets, filename table, path hashing, protected `data_patch.pak` AES-GCM wrapper, and official 128-byte footer are known. |
| Binary property tree | `.xml` payloads such as `objectcollection*.xml` | Partially mapped | These are not text XML after extraction. They use a compact binary node/attribute tree. |
| Graphics resource | `.gfx` | Partially mapped | Enough is known to patch selected mesh transform floats in stream buffers. Full container structure is not mapped yet. |
| Model resource | `.mdl` | Early notes only | Used by object entries and `.gfx` references. Full structure still needs a dedicated pass. |

## `.pak` Package Archive

Trials Fusion `.pak` files are archive containers used for config and data
packs. The game mounts base packages and DLC packages through the package
manager, then serves individual resource streams out of mounted archives.

Confirmed examples include generated `reverse_notes/out/data6_*.pak` and
`reverse_notes/out/data_loc6_*.pak` files. The same layout is also documented
from inspected official packs, with the important exception that
`build/data_pc/data_patch.pak` contains protected entries.

### File Header

Every inspected normal archive starts with this 12-byte header:

| Offset | Size | Type | Name | Meaning |
| ---: | ---: | --- | --- | --- |
| `0x00` | 4 | `u32` | `magic` | Constant `0x12345678`. In bytes: `78 56 34 12`. |
| `0x04` | 4 | `u32` | `data_start` | Absolute file offset where payload data begins. For normal packs this equals `12 + entry_count * 17`. |
| `0x08` | 4 | `u32` | `entry_count` | Number of 17-byte table records. This count includes the filename-list pseudo-entry at the end. |

Header pseudocode:

```text
pak:
  u32 magic       ; 0x12345678
  u32 data_start  ; 12 + entry_count * 17
  u32 entry_count
  entry[entry_count]
  payload bytes
  optional official_footer[128]
```

### Entry Table

Each table record is exactly 17 bytes. There is no padding between records.

| Relative offset | Size | Type | Name | Meaning |
| ---: | ---: | --- | --- | --- |
| `0x00` | 4 | `u32` | `path_hash` | G4-family hash of the resource path. The table does not store the path string here. |
| `0x04` | 4 | `u32` | `stored_size` | Number of bytes stored in the archive payload. For compressed entries this is compressed size. |
| `0x08` | 4 | `u32` | `unpacked_size` | Expected size after decompression/unprotection. For raw entries this usually matches `stored_size`. |
| `0x0c` | 1 | `u8` | `flags` | Storage/protection mode. See flags table below. |
| `0x0d` | 4 | `u32` | `data_offset` | Absolute file offset of this entry's payload bytes. |

Entry pseudocode:

```text
entry:
  u32 path_hash
  u32 stored_size
  u32 unpacked_size
  u8  flags
  u32 data_offset
```

The payload range for an entry is:

```text
[data_offset, data_offset + stored_size)
```

For normal files, `data_offset` points into or after `data_start`, and the
payload range must stay inside the archive file.

### Official Footer

Every inspected official `.pak` has 128 signature bytes after the final payload
range. This is not referenced by the table.

| Corpus | Footer behavior |
| --- | --- |
| Official base packs under `build/data_pc` | All inspected files have exactly 128 trailing bytes after `max(data_offset + stored_size)`. |
| Official DLC packs under `datapack/DLCContent` | All inspected files have exactly 128 trailing bytes after `max(data_offset + stored_size)`. |
| Generated `reverse_notes/out/*.pak` files | Generated packs currently omit the footer and end exactly at the final payload byte. |

The footer is a 1024-bit RSA/PKCS#1 type-1 signature over the archive bytes
before the footer:

```text
footer_plain = RSA_public_decrypt(footer[0:128], embedded_pem, PKCS1_type_1)
digest       = pkcs1_unpad(footer_plain)  ; 32 bytes
digest == SHA256(pak_bytes[0:max_payload_end])
```

This uses the same embedded public key as protected `data_patch.pak` entries.
Generated footerless packs have worked for the current custom `data6.pak`
injection workflow, so this signature is not enforced on the tested DLC
replacement path. Treat it as part of the official signed format, but not
required for current generated-pack tooling unless targeting a mount path that
verifies package signatures.

### Storage Flags

| Flag | Meaning | Observed behavior |
| ---: | --- | --- |
| `0x00` | Raw/stored | Payload bytes are consumed directly. `stored_size` normally equals `unpacked_size`. |
| `0x01` | zlib-compressed | Payload begins with a zlib stream, commonly `78 DA ...` in generated data6 packs. Inflate to `unpacked_size`. |
| `0x10` | Protected/encrypted raw payload | Observed only in `build/data_pc/data_patch.pak` so far. For every inspected `0x10` entry, `stored_size == unpacked_size + 128`. Runtime path: unprotect with the pak protection provider, then copy raw bytes. |
| `0x11` | Protected/encrypted compressed payload | Observed only in `data_patch.pak` so far. Runtime path: unprotect with the pak protection provider, then decompress because `flags & 0x07` is non-zero. It is not plain zlib before unprotection. |

Generated pack samples only use `0x00` and `0x01`. For example,
`data6_codex_clone.pak` has 258 table records: 102 compressed entries and 156
raw entries, where one of the raw entries is the filename list.

Official corpus observations:

| Corpus | Result |
| --- | --- |
| `build/data_pc/data.pak`, `data_loc.pak`, `data_misc.pak`, `data_shaders.pak`, `data_tex.pak` | Only `0x00` and `0x01`; all `0x01` entries inflate cleanly. |
| DLC `pack1`, `pack2`, `pack3`, `pack4`, `pack5`, `pack6`, `pack8` paks | Only `0x00` and `0x01`; all `0x01` entries inflate cleanly. |
| `build/data_pc/data_patch.pak` | 544 table records: 270 `0x11`, 273 `0x10`, and one raw filename-list entry. |

### Protected Entry Pipeline

Ghidra pass on 2026-05-13 traced protected entry reads through:

| Address | Working name | Role |
| ---: | --- | --- |
| `0x00ec1a10` | `PakArchive_OpenEntryByHash` | Looks up a runtime 0x18-byte entry by hash and creates a stream for it. |
| `0x00ec1780` | `PakEntry_CreateDecodedStream` | Materializes an entry. This is the key branch for raw, compressed, and protected entries. |
| `0x00ec13f0` | `PakBackingReadAtCached` | Reads stored bytes from the backing `.pak` stream with a per-thread cache. |
| `0x00ec5a30` | `CreatePakProtectionProvider` | Creates the protection provider. Protected pak reads request provider type `2`. |
| `0x00ec56e0` | `BuildPakProtectionPublicKeyPem` | Reconstructs the embedded PEM public key used by the protection provider. |
| `0x00ec6080` | `CreatePakProtectionDecryptor` | Creates a decrypt/unprotect object from the PEM, protected payload, and payload size. |
| `0x00ec5d00` | `PakProtectionDecryptor_InitWithPem` | Initializes an OpenSSL EVP AES-256-GCM context for the protected payload. |
| `0x00ec5bc0` | `PakProtectionDecryptor_Read` | Produces the unprotected byte buffer through the OpenSSL EVP wrapper. |
| `0x00ff05b0` | `EVP_aes_256_gcm_descriptor` | Returns the OpenSSL `EVP_aes_256_gcm` descriptor at `DAT_01569f24`. |

Runtime entry materialization, simplified:

```text
stored = read(data_offset, stored_size)

if flags == 0:
  stream raw stored bytes

else:
  working = stored

  if flags & 0x10:
    pem = BuildPakProtectionPublicKeyPem()
    key = RSA_public_unwrap_32_byte_key(pem, stored[0:128])
    working = AES_256_GCM_update(key, "5484258461056496\0", stored[128:])

  if flags & 0x07:
    output = decompress(working, unpacked_size)
  else:
    output = working[0:unpacked_size]
```

This explains why `0x11` entries do not inflate directly: the compressed stream
is inside the protected wrapper.

The embedded PEM is obfuscated in the binary with a repeating `dark` XOR key.
After reconstruction it is:

```text
-----BEGIN PUBLIC KEY-----
MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCse7vjbt+e1141E1adrS2JCB6z
PZz7Ai7Kvh05RLXibUPk06K2xGalx/yTmPR514lHxX8hxmE57wDUvZ6LZazl8xWB
jQoDBpVyXF/02Ym7BNPedLjzOCOmYgKsnA1E1yhFcTqzpAucFBoN44QvD0Ulj/1X
F8P5e6UOwdJKyvPycQIDAQAB
-----END PUBLIC KEY-----
```

Protected payload layout:

| Relative offset | Size | Meaning |
| ---: | ---: | --- |
| `0x00` | `128` | RSA block. Interpreting it with the embedded public key yields a PKCS#1 v1.5 type-1 block whose final payload is a 32-byte AES key. |
| `0x80` | `stored_size - 128` | AES-256-GCM ciphertext. The game consumes the EVP update output directly; no separate authentication tag has been identified in the pak entry. |

Protected transform:

```text
rsa_plain = RSA_public_decrypt(stored[0:128], embedded_pem, PKCS1_type_1)
aes_key   = pkcs1_unpad(rsa_plain)        ; exactly 32 bytes
iv        = b"5484258461056496\x00"       ; 17 bytes, includes C-string NUL
working   = AES-256-GCM-update(aes_key, iv, stored[128:])

if flags & 0x07:
  output = zlib_inflate(working)
else:
  output = working
```

Important detail: `PakProtectionDecryptor_InitWithPem` calls
`EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 0x11, NULL)` before passing
the static string into `EVP_CipherInit_ex`. That makes the IV length 17 bytes,
so the IV is the 16 ASCII digits plus the terminating NUL byte.

Verified samples:

| Entry | Flag | AES key source | Result after AES-256-GCM |
| ---: | ---: | --- | --- |
| `1` | `0x10` | RSA block unwraps to `5a6f...90e7` | Begins with text XML: `<audiocollection...`. |
| `30` | `0x10` | RSA block unwraps to `0e37...c228` | Begins with `OBJ\x01VER01...`. |
| `0` | `0x11` | RSA block unwraps to `3669...d4c6` | Begins with zlib `78 DA`; inflates to `<areavolumes collection...`. |
| `310` | `0x11` | RSA block unwraps to `ac5e...7cc0` | Begins with zlib `78 DA`; inflates to `GFX2...`. |

Additional observations:

| Observation | Detail |
| --- | --- |
| EVP primitive | `DAT_01569f24` is the OpenSSL `EVP_aes_256_gcm` descriptor. Its init path matches OpenSSL 1.0.1h `aes_gcm_init_key`, and its update path matches `aes_gcm_cipher`. |
| RSA block role | The first 128 stored bytes are not a detached signature for the rest of the entry. They are the per-entry AES-256 key wrapped as a public-key-verifiable PKCS#1 type-1 block. |
| Static EVP parameter | `5484258461056496` is the fixed AES-GCM IV material. Because the runtime sets IV length to `0x11`, the NUL terminator is part of the IV. |

### Filename List Entry

The last table record is a pseudo-entry containing a filename list. It maps
path strings back to the preceding resource table entries by order.

Filename-list payload:

| Offset | Size | Type | Name | Meaning |
| ---: | ---: | --- | --- | --- |
| `0x00` | 4 | `u32` | `count` | Number of resource path strings. This is normally `entry_count - 1`. |
| `0x04` | variable | repeated | `paths` | Repeated `u16 byte_len`, then `byte[byte_len] utf8_path`. |

Path record:

```text
path_record:
  u16 byte_len
  byte[byte_len] utf8_path
```

Observed behavior:

| Observation | Detail |
| --- | --- |
| Ordering | Filename `paths[i]` describes table `entry[i]`. |
| Pseudo-entry | The filename-list entry itself is not named by the list. |
| Compression | In every inspected official and generated pack, the filename-list entry is raw (`flags = 0x00`). |
| Path style | Paths are slash-separated and usually relative to the mounted content root, for example `contentpack6/characters/frontier_helmet/frontier_helmet.gfx`. |

### Path Hash

The `path_hash` field is not CRC32. It matches the G4 hash family. Pak resource
paths are hashed with runtime normalization:

| Input detail | Runtime behavior |
| --- | --- |
| Leading `./` | Stripped before hashing. |
| Leading `/` or `\` | One leading slash is stripped before hashing. |
| Backslashes inside path | Converted to `/`. |
| ASCII lowercase `a-z` | Converted to uppercase. |
| Mount prefix | Not added by the pak lookup hash path; callers pass the archive-relative resource path. |

```python
def g4_hash_path(path: str) -> int:
    if path.startswith("./"):
        path = path[2:]
    elif path.startswith("/") or path.startswith("\\"):
        path = path[1:]
    b = path.replace("\\", "/").upper().encode("ascii")
    seed = len(b)
    h = 0
    for ch in b:
        h = (h + ch * seed) & 0xffffffff
        seed = ((seed & 0xffff) * 18000 + (seed >> 16)) & 0xffffffff
    return h
```

Verified path hashes:

| Path | Hash |
| --- | ---: |
| `AREAVOLUMES.XML` | `0x721a1da8` |
| `OBJECTCATEGORY.XML` | `0xef6ea347` |
| `OBJECTCOLLECTION.XML` | `0x6b108937` |
| `OBJECTCOLLECTION20.XML` | `0xe583a04e` |
| `PACK20/OBJECTS.XML` | `0x18630d86` |
| `OBJECTS/PACK20/AIDEN_BOTTOM_1669118754.MDL` | `0xc8711195` |

Path normalization is now verified across 17,554 official filename-list paths:

| Variant tested | Matches |
| --- | ---: |
| Stored filename-list path, uppercased | `17,554 / 17,554` |
| Stored filename-list path as-is | `0 / 17,554` |
| Leading slash plus uppercased path | `0 / 17,554` |
| Slash-to-backslash replacement for paths containing `/` | `0 / 17,554` |
| `<evo2_devfiles>` prefix plus uppercased path | `0 / 543` tested against `data_patch.pak` |

So the archive hash input is the filename-list path after the same slash and
uppercase normalization used at lookup time. The stored official paths already
use `/`, no leading slash, and no mount token.

Collision behavior from `PakArchive_OpenEntryByHash`:

| Case | Behavior |
| --- | --- |
| Same archive, same 32-bit hash | Entries are stored in bucket order matching table order. Lookup compares only the hash and returns the first matching table entry. |
| Same path/hash across mounted archives | Resolved by higher-level mount/archive order, not by this per-archive lookup function. |
| Distinct paths with deliberate same hash in one archive | Not disambiguated by stored path; the later colliding entry is unreachable through normal hash lookup. |

Property names in binary property trees appear to use the same hash function
without uppercasing. Known examples:

| Property | Hash |
| --- | ---: |
| `name` | `0xca82e140` |
| `id` | `0x0036ef52` |
| `category` | `0xf71d1550` |
| `subcategory` | `0x7335dcd5` |
| `package` | `0x6a582b65` |
| `directory` | `0xeb739b94` |
| `packageID` | `0xa4016384` |
| `surface` | `0xd955d183` |
| `meshes` | `0xa4512b1b` |
| `source` | `0x3f427ca3` |

### Layout Example

A generated `data6` package with 258 entries has:

| Field | Value |
| --- | ---: |
| `magic` | `0x12345678` |
| `entry_count` | `258` |
| `data_start` | `4398` (`12 + 258 * 17`) |
| compressed entries | `102` |
| raw entries | `156` |
| filename-list paths | `257` |
| trailing footer | none in generated sample; official `data6.pak` has a 128-byte footer |

The first table record in generated data6 samples:

| Field | Value |
| --- | ---: |
| `path_hash` | `0x7ad9e0b1` |
| `stored_size` | `128` |
| `unpacked_size` | `433` |
| `flags` | `0x01` |
| first payload bytes | `78 DA 72 77 ...` |

### DLC Mount Layout

Official DLC packs are normally laid out like this:

```text
datapack/DLCContent/packN/data/configs.pak
datapack/DLCContent/packN/data/dataN.pak
```

Typical config-side resources:

```text
objectcategoryN.xml
packN/objectgroups.xml
packN/objects.xml
packinfo/pack.xml
packinfo/packN.xml
```

Typical data-side resources:

```text
objectcollectionN.xml
objects/packN/*.mdl
```

Important observed behavior:

| Area | Detail |
| --- | --- |
| `packN/objects.xml` | Official DLC files are mostly empty `ObjectList` shells. The editor/game object definitions are in `objectcollectionN.xml`. |
| Pack 20 | Pack20 is special-cased in `build/data_pc/data_patch.pak`, not in a normal `datapack/DLCContent/pack20` directory. |
| Custom injection | Pack6/contentpack6 has been the successful normal-DLC test target because it avoids pack20 protection. |

### Read Algorithm

Normal unprotected entries can be read with:

```text
read header
validate magic == 0x12345678
read entry_count records of 17 bytes each
for each resource entry:
  seek data_offset
  read stored_size bytes
  if flags == 0x00:
    data = payload
  if flags == 0x01:
    data = zlib_inflate(payload)
  validate len(data) == unpacked_size
read final filename-list entry
map filename[i] to entry[i]
ignore or preserve any trailing 128-byte official footer
```

Protected entries require the AES-GCM/RSA transform described above before
normal use:

```text
read protected stored bytes
unwrap stored[0:128] with embedded RSA public key to get 32-byte AES key
AES-256-GCM update stored[128:] with IV b"5484258461056496\x00"
if flags & 0x07:
  zlib inflate the AES output
else:
  copy the AES output directly
```

### Write Algorithm for Normal Packs

Known-good generated packs follow this conservative layout:

```text
build all resource payloads
compress selected entries with zlib
append filename-list pseudo-entry as the final table entry
compute entry_count = resource_count + 1
compute data_start = 12 + entry_count * 17
assign absolute data_offset values by concatenating payloads after data_start
write header
write packed 17-byte records
write payload bytes
optionally preserve an existing 128-byte footer only if bytes before it are unchanged
```

When adding files, update all of the following together:

| Item | Required update |
| --- | --- |
| `entry_count` | Increase by the number of added resource entries. |
| `data_start` | Recompute as `12 + entry_count * 17`. |
| Table offsets | Recompute every `data_offset`, because table growth shifts payloads. |
| Filename list | Add paths in the same order as resource entries. |
| `path_hash` | Compute from the normalized archive path: strip one leading `./`, `/`, or `\`, convert `\` to `/`, then uppercase ASCII. |

Generated packs that change bytes cannot produce a valid official footer
without the RSA private key. Omit the footer for the currently tested custom
DLC replacement workflow, or preserve it only when the signed archive bytes are
unchanged.

### Open Questions

| Topic | Unknown |
| --- | --- |
| Protected writes | Read pipeline is mapped: the first 128 bytes unwrap to the per-entry AES-256 key, payload bytes after that are AES-256-GCM with IV `5484258461056496\0`, then `0x11` entries zlib-inflate. Remaining write-side gap: generating accepted protected entries requires the private key or a replacement/hook path because both the per-entry AES key block and official footer use RSA signatures. |
| Signature enforcement | Footer format is known: RSA/PKCS#1 type-1 over `SHA256(pak_without_footer)`. Generated footerless packs work in the tested custom DLC replacement path, so which mount/load paths enforce this signature is still unknown. |

## Binary `objectcollection*.xml`

Despite the `.xml` extension, decompressed `objectcollection*.xml` files are
binary property trees, not text XML. Real files begin with binary tokens such as:

```text
01 00 54 C7 C4 68 00 01 ...
```

Current parser model:

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
    byte[value_len] value

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

| Type | Meaning |
| ---: | --- |
| `3` | `int32` |
| `4` | `int32`-like enum/value |
| `5` | `float32` |
| `6` | string |

Custom object entries are more than a visible editor record. Working entries
also involve generated child records for surfaces/LODs/bounds, mesh offsets,
material IDs, atlas/icon coordinates, container metadata derived from
`.mdl`/`.gfx`, and content-pack/category registration.

## `.gfx` Resource Notes

The full `.gfx` format is not mapped yet. Current practical knowledge comes
from runtime resource-stream patching:

| Observation | Detail |
| --- | --- |
| Resource path form | Custom test paths use `<evo2_devfiles>contentpack6/.../*.gfx`. |
| Runtime patch point | The stream hook patches bytes after the game opens the resource and before parsing completes. |
| Mesh-name anchors | Current patches search stream buffers for `codex_mesh_log1` and `codex_mesh_0001`. |
| Scale fields | Three `float32` scale values are located at `mesh_name_offset - 48`, `mesh_name_offset - 44`, and `mesh_name_offset - 40` for the tested resources. |
| Confirmed values | Runtime toggle has successfully alternated those floats between `1.0` and `5.0`. |

Current custom targets:

```text
<evo2_devfiles>contentpack6/manmade/props/codex/codex_custom_log.gfx
<evo2_devfiles>contentpack6/manmade/props/codex/codex_custom_half_ramp.gfx
```

## `.mdl` Resource Notes

`.mdl` resources are referenced by objectcollection records and `.gfx` files.
The investigation has produced round-trip and mutation samples, but the format
still needs a dedicated field map.

Known sample outputs live under:

```text
reverse_notes/out/*.mdl
```

Open `.mdl` questions:

| Topic | Unknown |
| --- | --- |
| Header/signature | Needs confirmation across official and generated samples. |
| Mesh buffers | Vertex/index buffer layout and section tables are not documented yet. |
| Materials | Material ID and surface binding structure needs mapping. |
| Collision | Relationship between visual mesh data and collision/bounds records needs mapping. |
