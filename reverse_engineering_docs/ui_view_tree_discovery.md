# UI View Tree Discovery

Goal: enumerate Trials Fusion Flash/AVM1 UI objects, find the display tree root, and eventually expose a safe show/hide tree in the mod.

## Current anchors

- Ghidra program matches the Uplay-style image base for this running build.
- Runtime module base observed from PID 6304: `0x00640000`.
- Live movieclip candidate from PID 6304:
  - `[[trials_fusion.exe+0x0104b308] + 0x1dc]` -> UI/input object `0x35d96f70`
  - `[0x35d96f70 + 0x04]` -> movieclip wrapper `0x23d5ef00`
  - `[0x23d5ef00]` -> movieclip object candidate `0x3342d6a0`
  - This object is passed as `this` to Ghidra `FUN_00cf2390`, which traverses children and filters `movieclip` nodes.
- `trialsEvo2.ViewManager.showLoadingScreen`
  - Runtime address from Cheat Engine AOB scan: `0x013ff598`
  - Ghidra address: `0x014bf598`
  - Xrefs: `ShowLoadingScreen` at `0x00b9fd2d`, `HideLoadingScreen` at `0x00b9fe15`
- `InitializeFlashPropertyNames` registers built-in AVM1/Flash property names.

## Built-in property ids

These ids are useful because AVM1 bytecode and native property access can refer to properties by id instead of only by string.

| Property | Id |
| --- | ---: |
| `_x` | `0x00` |
| `_y` | `0x01` |
| `_xscale` | `0x02` |
| `_yscale` | `0x03` |
| `_currentframe` | `0x04` |
| `_totalframes` | `0x05` |
| `_alpha` | `0x06` |
| `_visible` | `0x07` |
| `_width` | `0x08` |
| `_height` | `0x09` |
| `_rotation` | `0x0a` |
| `_target` | `0x0b` |
| `_framesloaded` | `0x0c` |
| `_name` | `0x0d` |
| `_xmouse` | `0x13` |
| `_ymouse` | `0x14` |
| `_parent` | `0x15` |
| `_this` | `0x1e` |
| `this` | `0x1f` |
| `_root` | `0x20` |
| `_level0` | `0x23` |
| `_global` | `0x24` |

## Mod instrumentation

`TFPayload/ui-view-explorer.*` adds a DEVELOPMENT_MODE-only `Mod -> UI View Explorer -> Live Object Probe` panel.

The first version is intentionally read-only:

- paste a candidate live object address from Cheat Engine
- scan the first `0x100` bytes for pointer-like fields
- try recursive parent-pointer offsets
- dump raw bytes to the mod log
- use the current ActionScript message handler as a known live UI-adjacent address
- resolve the current movieclip candidate from the game-manager chain
- render a guarded movieclip tree through vtable methods:
  - `vtable+0x18`: type-name getter
  - `vtable+0x1c`: property setter
  - `vtable+0x20`: property getter
  - `vtable+0x74`: child getter by index
  - `vtable+0x04`: property presence probe
- render `_visible` checkboxes using the engine's initialized `_visible` string object and the 0x34-byte bool variant layout.

## Next reverse step

Test `_visible` toggles in-game against low-risk UI nodes first. The current implementation uses the Uplay-addressed `_visible` string object; if the Steam build needs this feature, map the Steam address for that global string object before enabling writes there.
