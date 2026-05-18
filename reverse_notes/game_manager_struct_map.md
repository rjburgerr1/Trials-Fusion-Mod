# `g_pGameManager` first-pass map

## Important naming note

`g_pGameManager` appears to be a global pointer to a broad root object, not only the in-race manager.

Several callsites then follow:

- `g_pGameManager + 0xDC` -> in-race/entity manager used by respawn, checkpoints, bike list, etc.
- `g_pGameManager + 0xFC` -> state manager object
- `g_pGameManager + 0x120` -> profile/stats hub

For clarity while reversing, it may be better to temporarily name the root object `GlobalGameContext` and reserve `RaceManager` / `EntityManager` for the child at `+0xDC`.

## Confirmed root offsets

| Offset | Proposed field name | Evidence |
|---|---|---|
| `0xD4` | `gameModeStateHolder` | `GameState = *(*(g_pGameManager + 0xD4) + 0x4)` |
| `0xDC` | `raceEntityManager` | `HandlePlayerRespawn`, `respawn.cpp`, `bike-swap.cpp` |
| `0xE0` | `respawnFxOrPlacementHelper` | `HandlePlayerRespawn` passes this to `InitializeWithTwoVec3AndTwoParams` |
| `0xEC` | `multiplayerStateHolder` | `*(*(g_pGameManager + 0xEC) + 0x24) == 2` |
| `0xFC` | `gameStateManager` | used by `HandlePlayerRespawn` and `FinishRaceDirect` |
| `0x100` | `messageBusOrTaskQueue` | `HandlePlayerRespawn` sends a message through this |
| `0x104` | `editorTrackSessionManager` | bike reload path uses this as `this` for `ReloadBikeFromSettings` |
| `0x118` | `bikeDataManager` | bike swap code |
| `0x120` | `playerStatsHub` | `GrantUplayReward`, money, stats, unlocks |
| `0x128` | `networkOrRewardDispatcher` | `AwardMoneyToPlayer` uses `+0x6DD64` from this object |
| `0x174` | `careerMoneyUiNotifier` | `AwardMoneyToPlayer` |
| `0x178` | `statsRefreshTarget` | `AwardMoneyToPlayer` calls `FUN_00A70910` on this |
| `0x1A0` | `localizedMessageProcessor` | `HandlePlayerRespawn` |
| `0x1A8` | `trackTypeHolder` | `TrackType = *(*(g_pGameManager + 0x1A8) + 0x14)` |
| `0x1B4` | `playerProfile` | `GrantUplayReward` -> `AddAcornsToBalance` |

## Child object observations

### `raceEntityManager = *(g_pGameManager + 0xDC)`

| Offset | Proposed field name | Evidence |
|---|---|---|
| `0x08` | `raceState` | `HandlePlayerRespawn` checks `!= 6` |
| `0x14` | `raceTimeEncrypted` | `respawn.cpp` reads/writes via async task helpers |
| `0x2F0` | `bikeList` | `respawn.cpp`, `bike-swap.cpp` |
| `0x938` | `checkpointList` | `HandlePlayerRespawn`, `respawn.cpp` |
| `0xB80` | `respawnAuxValue` | `HandlePlayerRespawn` |
| `0xE9C` | `entityForRespawnPlacement` | `HandlePlayerRespawn` |

### `playerStatsHub = *(g_pGameManager + 0x120)`

| Offset | Proposed field name | Evidence |
|---|---|---|
| `0x04` | `careerStats` | `FUN_00A70910` |
| `0x08` | `moneyStats` | `GrantUplayReward`, `AwardMoneyToPlayer` |
| `0x0C` | `secondaryStats` | `GrantUplayReward`, `FUN_00A70910` |
| `0x10` | `unlockInventory` | `GrantUplayReward` |

## Suggested Ghidra workflow

1. Rename `DAT_0174b308` to `g_pGameManager`.
2. Create a temporary struct named `GlobalGameContext`.
3. Apply it to the pointed-to object, not the global variable itself.
4. Add only the offsets you can support from callsites; leave padding between them.
5. When you hit a field that is itself a pointer, immediately create a second struct for that child and type the pointer field.
6. Re-run decompilation after each few fields. If casts collapse and expressions become readable, the model is improving.

## Useful decompiler anchors already identified

- `GrantUplayReward`
- `AwardMoneyToPlayer`
- `HandlePlayerRespawn`
- `FUN_00A70910`

These four functions alone give a surprisingly dense first pass over the root object's topology.
