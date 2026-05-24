# Editor Cross-Reference Map

> Launch all editors from one place: `make editor_hub && build/editor_hub`

All ncurses editors are standalone binaries that read/write their own `.dat` file.
None currently load other `.dat` files at runtime — cross-references are stored as
numeric IDs and resolved by the game engine. This document tracks those ID fields
so editors can be extended to show human-readable names from referenced files.

---

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
| `QuestDef.startId` | `uint8_t` | `dialog.dat` / `enemies.dat` / `items.dat` depending on `startType` |
| `QuestObjective.targetId` | `uint8_t` | `enemies.dat` / `items.dat` / `dialog.dat` depending on `type` |

---

### logmessage_editor — `assets/data/log_messages.dat`

| Field | Type | References |
|---|---|---|
| `LogMessage.actionId` | `uint8_t` | `actions.dat` index; `0xFF` = any action |
| `LogMessage.enemyDefId` | `uint8_t` | `enemies.dat` index; `0xFF` = any enemy |

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

When an editor needs to show human-readable names for a referenced ID:

1. Mirror the target file's struct at the top of the editor source (keep in sync with the game header).
2. Add a static array and count, and a `loadXxx(const char *path)` helper that `fread`s into it.
3. Call the loader at startup (before `initscr()`).
4. Wherever the ID field is rendered, look up `targetDefs[id].name` and display it alongside the raw ID.
5. For interactive selection, add a picker screen (see `pickEnemy()` in `enemy_editor.c` as the reference implementation).

Keep loaders read-only (open with `"rb"`, never write). The editor that owns the file is the only one that writes it.
