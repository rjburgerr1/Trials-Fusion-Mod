# `FUN_00A70910` analysis

## Working rename

`BuildCareerStatsSnapshot`

Alternative names:

- `RefreshCareerStatsSummary`
- `PopulateCareerStatsViewModel`
- `RebuildPlayerStatsSummary`

`BuildCareerStatsSnapshot` is the least committal good name: the function is clearly assembling a derived stats block, but we do not yet know whether the destination is UI-only, leaderboard-facing, or reused elsewhere.

## Signature

```c
void __thiscall BuildCareerStatsSnapshot(void *this, char useAlternateSlot)
```

## High-level behavior

```text
target = useAlternateSlot ? this + 0xB4 : this + 0x50

initialize target strings
read CareerStats from *(g_pGameManager + 0x120) + 0x04
populate:
  - medal counts
  - weighted medal total
  - completion percent
  - compact per-difficulty summary
read MoneyStats from *(g_pGameManager + 0x120) + 0x08
populate:
  - formatted net money string
read SecondaryStats from *(g_pGameManager + 0x120) + 0x0C
populate:
  - several totals
  - several float ratios/percentages
if primary slot and callback exists:
  invoke callback
```

## Important source objects

### `playerStatsHub = *(g_pGameManager + 0x120)`

| Offset | Proposed name | Evidence |
|---|---|---|
| `+0x04` | `careerStats` | source of medal counts, completion %, difficulty summary |
| `+0x08` | `moneyStats` | source of earned/spent totals used to format net money |
| `+0x0C` | `secondaryStats` | source of totals and float metrics |

## Destination block layout

The function writes to either:

- `this + 0x50` when `param_1 == 0`
- `this + 0xB4` when `param_1 != 0`

That destination block appears to be a summary/view-model struct:

| Offset inside block | Proposed meaning |
|---|---|
| `+0x00` | string field A |
| `+0x10` | string field B |
| `+0x18` | formatted net-money string |
| `+0x20` | weighted medal total |
| `+0x24` | vector of 4 medal counts |
| `+0x34` | vector of 5 compact category values |
| `+0x44` | career/progression level byte copied from `careerStats + 0x104` |
| `+0x48` | completion percent |
| `+0x4C` | secondary stat from `secondaryStats + 0x14` |
| `+0x50` | secondary stat from `secondaryStats + 0x10` |
| `+0x54` | secondary stat from `secondaryStats + 0x18` |
| `+0x58` | float metric `FUN_00A0A1C0(..., 1)` |
| `+0x5C` | float metric `FUN_00A0A1C0(..., 0)` |
| `+0x60` | float metric `FUN_00A0A1C0(..., 2)` |

## `careerStats` observations

`careerStats = *(playerStatsHub + 0x04)`

### Known fields used directly

| Offset | Proposed meaning |
|---|---|
| `+0x104` | progression tier / license tier / career stage byte |
| `+0x110..+0x11C` | medal counts or derived medal buckets |

### Helper functions

#### `FUN_009F8570(careerStats, index, includeLocked)`

This returns medal-count-like values.

- Normal case: `*(careerStats + 0x110 + index * 4)`
- If `careerStats + 0x104 < 6`, `includeLocked == 0`, and `index` is 2 or 3:
  - index 2 returns `field_0x118 + field_0x11C`
  - index 3 returns `0`

That strongly suggests a medal tier that is hidden or collapsed before a late-career unlock.

#### `FUN_009F9750(careerStats, filter)`

Builds a percent score from all cached tracks:

- gets all tracks from `*(g_pGameManager + 0x110)`
- adds full weight for unlocked/completed tracks
- divides achieved weight by total weight

Most likely: overall career completion percentage.

#### `FUN_009F95A0(careerStats, filter)`

Builds a packed 5-value summary by scanning track metadata and extracting medal/grade state.

Most likely: a per-difficulty or per-category “best medal obtained” compact encoding.

## `moneyStats` observations

The function computes:

```c
netMoney = totalEarned - totalSpent
```

using:

- `moneyStats + 0x88 / 0x8C` -> total earned
- `moneyStats + 0x90 / 0x94` -> total spent

Then it formats the result into the destination block string at `+0x18`.

## `secondaryStats` observations

`secondaryStats = *(playerStatsHub + 0x0C)`

Directly copied fields:

- `+0x10`
- `+0x14`
- `+0x18`

Helper-derived float metrics:

- `FUN_00A0A1C0(obj, 0)`
- `FUN_00A0A1C0(obj, 1)`
- `FUN_00A0A1C0(obj, 2)`

Without the helper body, the cleanest current label is `secondaryStats`, not something more specific.

## Why this function matters for `g_pGameManager`

It confirms that `g_pGameManager + 0x120` is not just “some stats pointer”; it is a hub object with at least three semantically distinct children:

```text
playerStatsHub
  +0x04 -> careerStats
  +0x08 -> moneyStats
  +0x0C -> secondaryStats
```

That gives us a much more stable child structure to apply in Ghidra than a flat sea of `undefined4 *`.
