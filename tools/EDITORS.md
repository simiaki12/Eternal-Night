# Editor Cross-Reference Map

> Launch all editors from one place: `make editor_hub && build/editor_hub`

All ncurses editors are standalone binaries that read/write their own `.dat` file.
Cross-references between files are stored as numeric IDs on disk, but no editor
shows them that way: `tools/refs.h` reads the referenced `.dat` files and every
reference field is rendered and edited by name. This document maps those fields.

---

## Cross-references

Fields that point at another record are edited **by name, never by number**.
They render the referenced record's name (`Bandit`, not `3`), and:

| Key | Action |
|-----|--------|
| `+` / `-` (and `←` / `→` where the editor binds them) | step to the previous / next record (wraps; stops on `(none)`) |
| `Enter`   | open a picker list of every record, cursor already on the current value |
| `Up`/`Down`, `PgUp`/`PgDn` (in the picker) | move / page through the list |
| `N` (in the picker) | clear the reference to `(none)` |
| `Esc` (in the picker) | cancel, keep the old value |

A reference pointing at a record that no longer exists shows **`?? missing 12`**,
so broken data is visible instead of silently looking valid.

This is implemented once in `tools/refs.h`, which reads the referenced `.dat`
files directly — so a name changed in one editor shows up in every other
editor. When adding a new reference field, use `refLabel` / `refCycle` /
`refPick` rather than a raw numeric field.

**Type-dependent references.** A few fields mean different files depending on a
sibling field — a quest objective's `targetId` is an enemy, an item, a dialog
tree, a location or a bare map tile according to its `type`. Those call sites map
the sibling to a `RefKind` and go through `refLabelOr` / `refCycleOr` /
`refPickOr`, which accept `REF_RAW` for the cases that really are a plain number.
**Changing the discriminator clears the id**, since the old number would silently
point at an unrelated record in a different file.

**Stable IDs.** `EnemyDef` and `NpcDef` carry an explicit `id` (they used to be
referenced by array position). Other files store that id, so records can be
reordered or inserted without silently repointing every reference at the wrong
record. Lookups go through `enemyGetDef()` / `npcGetDef()`.

## Dependency Graph

```
actions.dat  <──  items.dat          (ItemDef.actions[4])
                  log_messages.dat   (LogMessage.actionId)

enemies.dat  <──  enemies.dat        (EnemyPool.enemyIds[4]  — self-reference, pools into defs)
                  log_messages.dat   (LogMessage.enemyDefId)
                  quests.dat         (QuestDef.startId when startType=TRIG_KILL)
                  quests.dat         (QuestObjective.targetId when type=OBJ_KILL)

items.dat    <──  loottables.dat     (LootEntry.itemId)
                  player.dat         (PlayerData.equipped[])
                  quests.dat         (QuestDef.rewardItemId)
                  quests.dat         (QuestDef.startId when startType=TRIG_ITEM)
                  quests.dat         (QuestObjective.targetId when type=OBJ_GET_ITEM)

loottables.dat <── enemies.dat       (EnemyDef.lootTableId)

statuses.dat <──  actions.dat        (ActionDef.onPlay/fallback value when type=EFX_APPLY_STATUS)
                  statuses.dat       (StatusDef.onExpire value — self-reference, chaining)

states.dat   <──  actions.dat        (TransMatrix row/column indices — graph-local state
                                      ids, one matrix per encounter type named by
                                      ActionDef.graphMask; states are seed-only,
                                      edited via seed_states.c)

dialog.dat   <──  npcs.dat           (NpcDef.treeId)
                  quests.dat         (QuestDef.startId when startType=TRIG_DIALOG)
                  quests.dat         (QuestObjective.targetId when type=OBJ_TALK_NPC)

npcs.dat     <──  social_encounters.dat  (SocialEncounterDef.npc_id)

social_encounters.dat <── dialog.dat (DialogOption.effect_arg when effect=DLGFX_SOCIAL_ENCOUNTER)

quests.dat   <──  loottables.dat     (LootEntry.questId)
                  ambient.dat        (AmbientEntry.questIdx)
```

---

## Per-Editor Reference Fields

### enemy_editor — `assets/data/enemies.dat`

| Field | Type | References |
|---|---|---|
| `EnemyDef.lootTableId` | `uint8_t` | `loottables.dat` index; `0xFF` = no loot |
| `EnemyPool.enemyIds[4]` | `uint8_t[4]` | `enemyDefs[]` indices in same file; `0xFF` = empty slot |

**Implemented cross-reference:** Pool slot picker shows full enemy list (name, hp, atk, def, flags) when pressing Enter on a slot field.

---

### item_editor — `assets/data/items.dat`

| Field | Type | References |
|---|---|---|
| `ItemDef.actions[4]` | `uint8_t[4]` | `actions.dat` index; `0xFF` = none |

---

### loottable_editor — `assets/data/loottables.dat`

| Field | Type | References |
|---|---|---|
| `LootEntry.itemId` | `uint8_t` | `items.dat` index |
| `LootEntry.questId` | `uint8_t` | `quests.dat` index; `0xFF` = no requirement |

---

### npc_editor — `assets/data/npcs.dat`

| Field | Type | References |
|---|---|---|
| `NpcDef.treeId` | `uint8_t` | `dialog.dat` tree index; `0xFF` = no dialog |

---

### social_encounter_editor — `assets/data/social_encounters.dat`

| Field | Type | References |
|---|---|---|
| `SocialEncounterDef.npc_id` | `uint8_t` | `npcs.dat` index |
| `SocialEncounterDef.reward_partial` | `uint8_t` | reward table (TBD) |
| `SocialEncounterDef.reward_full` | `uint8_t` | reward table (TBD) |

---

### dialog_editor — `assets/data/dialog.dat`

| Field | Type | References |
|---|---|---|
| `DialogOption.effect_arg` | `uint8_t` | `social_encounters.dat` index when `effect_type=DLGFX_SOCIAL_ENCOUNTER` |

---

### quest_editor — `assets/data/quests.dat`

| Field | Type | References |
|---|---|---|
| `QuestDef.rewardItemId` | `uint8_t` | `items.dat` index; `0xFF` = no item reward |
| `QuestDef.startId` | `uint8_t` | by `startType`: `dialog.dat` / `items.dat` / `enemies.dat` / quest locations; raw tile for `ZONE`, unused for `ALWAYS` |
| `QuestObjective.targetId` | `uint8_t` | by `type`: `enemies.dat` / `items.dat` / `dialog.dat` / quest locations; raw tile for `VISIT_ZONE` |

Quest locations live in an optional second section of `quests.dat`
(`[1 locCount][N x 12]`). No editor creates them yet, so `Visit Location`
targets have an empty picker until that section is seeded — the quest editor
round-trips the section untouched rather than dropping it on save.

---

### logmessage_editor — `assets/data/log_messages.dat`

| Field | Type | References |
|---|---|---|
| `LogMessage.actionId` | `uint8_t` | `actions.dat` index; `0xFF` = any action |
| `LogMessage.enemyDefId` | `uint8_t` | `enemies.dat` index; `0xFF` = any enemy |

---

### hunt_encounter_editor — `assets/data/hunt_encounters.dat`

| Field | Type | References |
|---|---|---|
| `HuntEncounterDef.enemyPoolId` | `uint8_t` | enemy pool (positional, `pool 1`..`pool 15`); `0xFF` = none |

---

### env_encounter_editor — `assets/data/env_encounters.dat`

| Field | Type | References |
|---|---|---|
| `EnvEdge.actionIds[3]` | `uint8_t[3]` | `actions.dat` id; `0xFF` = empty slot |
| `EnvEdge.rewardItem` | `uint8_t` | `items.dat` index; `0xFF` = none |
| `EnvEdge.nextState` | `uint8_t` | state *within the same encounter* (not `states.dat`); `0xFF` = terminal |

---

### investigation_editor — `assets/data/investigations.dat`

| Field | Type | References |
|---|---|---|
| `InvestigationDef.clueIds[8]` | `uint8_t[8]` | `clues.dat` id; `0xFF` = empty slot |
| `InvestigationDef.rewardItem` | `uint8_t` | `items.dat` index; `0xFF` = none |
| `InvestigationDef.rewardQuest` | `uint8_t` | `quests.dat` index; `0xFF` = none |

---

### ambient_editor — `assets/data/ambient.dat`

| Field | Type | References |
|---|---|---|
| `AmbientEntry.questIdx` | `uint8_t` | `quests.dat` index |

---

### player_editor — `assets/data/player.dat`

| Field | Type | References |
|---|---|---|
| `PlayerData.equipped[]` | `uint8_t[]` | `items.dat` indices; `0xFF` = empty slot |

---

## Adding Cross-Reference Lookups to an Editor

Never hand-roll a loader or a picker — everything goes through `tools/refs.h`:

1. `#include "refs.h"` (after `<ncurses.h>`; `refPick` uses `LINES`).
2. Render the field with `refLabel(REF_XXX, v)` instead of `%d`. It already
   prints `(none)` for `0xFF` and `?? missing 12` for a dangling id, so drop any
   hand-written "none" branch.
3. On `+`/`-` (and `←`/`→` if the editor binds them), assign
   `v = refCycle(REF_XXX, v, dir)`.
4. On `Enter`, assign `v = refPick(REF_XXX, v)`.
5. If the editor references records from the file it *writes*, call
   `refInvalidate()` at the end of its `save()` so a picker opened later in the
   same session sees the new records.

To add a whole new reference *kind*: extend `RefKind`, then either add a
`RefSpec` row (for plain `[1 count][N x fixed record]` files) or a custom loader
dispatched from `refGet()` (for variable-length files — see `refLoadDialogs`).

Keep loaders read-only (open with `"rb"`, never write). The editor that owns the file is the only one that writes it.
