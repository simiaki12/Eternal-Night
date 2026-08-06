# Eternal Night

A dark fantasy retro RPG optimized for distribution on floppies

## The Game

The Kingdom of Lucerot is in turmoil. The Queen has been kidnapped, and the only one who can save her is Azrael, the Daywalker.

Eternal Night is a dark fantasy RPG with an isometric overworld to navigate, towns to visit, and a cast of NPCs with their own agendas. Exploration is real-time: enemies roam the world and react to your presence. Combat triggers when you engage and plays out as a turn-based exchange of actions.

Azrael grows not through levelling up, but through how he fights. The more you rely on a fighting style, the deeper you go into that domain, unlocking new capabilities that reflect the kind of warrior you're becoming.

## The Constraint

This project is designed to fit on a single floppy disk and the be playable on a Windows system with no additional dependencies or requirements. 
Current size: 119 KB exe + 309 KB data.pak (29% of 1.44 MB floppy)

This heavily impacts many design descisions: simple graphics, synthetized music and sound effect. Focus on efficent data storage and compression, using a Win32/GDI renderer with no external runtime dependencies.

## Stack

The game is written in pure C11 for Windows platform. Data is stored in binary form. The game is developed on Linux, and then cross compiled for Windows.


## Building

**Requirements:** `x86_64-w64-mingw32-gcc`, `make`, `g++` (for the packer), `wine` (to run debug builds on Linux), `ncurses` (for editors).

```sh
# Debug build + run under Wine
make debug
wine build/game.exe

# Release build (optimized, stripped)
make release
```

The packer runs automatically as part of the build and bundles everything with appropriate extensions under `assets/` into `data.pak`.

---

## Encounters

Eternal Night has five kinds of encounter, each with its own mechanics and its own pool of actions to draw from.

**Combat** plays out in turns. Each round you choose from a pool of actions filtered to the moment and weighted by your domain levels, your equipment, the enemy in front of you, and your surroundings; the enemies then act in sequence. Up to three can be active at once, and each carries its own action set, so a group feels like distinct threats rather than one enemy copied three times.

**Social** encounters are negotiations. Each side plays moves that push the NPC's disposition up or down, and you win by getting them friendly enough before their patience runs out. They start from dialog options and are gated behind domain requirements.

**Investigation** drops Azrael into a scene full of clues with a limited number of turns to work it. Each action searches with odds weighted by his domains; some clues are essential to reaching the truth, while red herrings lead you astray. The outcome updates the journal and can award items or move a quest forward.

**Environmental** encounters are scripted set-pieces — traps, ambushes, hazards. They run as a small graph of states, and your actions move you between them: some states wound you, others open up new options. Resolving one sets a world flag and can leave a reward behind.

**Hunt** encounters begin when Azrael steps into a camp zone on the world map. The target group works its way through a chain of states — unaware, alert, scattered, broken — as you stalk it with hunt-specific actions like Ambush, Track, Set Trap, Blood Howl, and Massacre, picking enemies off one at a time. Run out of turns or get spotted and the hunt collapses into open combat against whoever is left.

Across every encounter, actions are browsed in a single scrollable panel split into tabs — All, Combat, Social, Investigation, Environment, and Hunt — so the same interface carries you from one kind of encounter to the next.

Outside of encounters, enemies move in real time. Most are slower than Azrael, so you can pick your fights. Each follows a behaviour archetype: some hunt you from range, some ignore you until you cross into their territory, some flee and call for allies. Break line of sight for long enough and pursuers give up the chase.

---

## Domain System

Azrael has no base level or XP. Progression is entirely domain-based: using actions in combat earns XP in the relevant domain, levelling it up and unlocking nodes in its tree. Nodes grant stat boosts, new actions, action upgrades, or passive scaling.

There are 14 domains in three tiers:

- **4 base domains** — always available: Direct Combat, Trickery/Environment, Blood/Vampire, Charm/Diplomacy
- **story-gated and fusion domains** — revealed by significant story beats or when Azrael reaches high proficiency in multiple base domains.

Each domain tree has ~25 nodes. Nodes give stat boosts, action unlocks,upgrade existing actions or even passive abilities. Players can also opt into RNG variance via nodes — choosing `5–10 damage` over a flat `7` if they want it.

HP growth is domain-driven: Direct Combat grants more, Charm/Diplomacy less. Blood may grant vampiric HP variants.

Combat action selection is weighted toward Azrael's most-levelled domain, adjusted for circumstance and a small random factor.

---

## Tools

All editors run in the terminal via ncurses. Run from the repo root so they can read/write `assets/data/`.

| Command | Purpose | Data file |
|---|---|---|
| `make editor_hub` | Hub launcher — select and open any editor | — |
| `make map_editor` | Tile map editor | `assets/maps/*.bin` |
| `make npc_editor` | NPC placement and social stats | `assets/data/npcs.dat` |
| `make dialog_editor` | NPC dialog trees and option effects | `assets/data/dialog.dat` |
| `make social_encounter_editor` | Social encounter definitions | `assets/data/social_encounters.dat` |
| `make enemy_editor` | Enemy stats, flags, loot tables | `assets/data/enemies.dat` |
| `make action_editor` | Combat action definitions | `assets/data/actions.dat` |
| `make item_editor` | Items and stat bonuses | `assets/data/items.dat` |
| `make shop_editor` | Shop definitions and stock | `assets/data/shops.dat` |
| `make loottable_editor` | Loot table composition | `assets/data/loottables.dat` |
| `make quest_editor` | Quest definitions and objectives | `assets/data/quests.dat` |
| `make logmessage_editor` | Reactive combat log messages | `assets/data/log_messages.dat` |
| `make ambient_editor` | Map-scoped ambient text | `assets/data/ambient.dat` |
| `make player_editor` | Starting player stats | `assets/data/player.dat` |
| `make clue_editor` | Clue definitions for investigation encounters | `assets/data/clues.dat` |
| `make investigation_editor` | Investigation scene definitions | `assets/data/investigations.dat` |
| `make env_encounter_editor` | Environmental encounter state graphs | `assets/data/env_encounters.dat` |
| `make hunt_encounter_editor` | Hunt encounter state graphs | `assets/data/hunt_encounters.dat` |
| `make camp_zone_editor` | Camp zone definitions (world-map trigger areas) | `assets/data/camp_zones.dat` |
| `make music_editor` | Software synth sequencer (ncurses) | `assets/music/*.mus` |
| `make music_editor_gui` | Software synth sequencer (Win32 GUI) | `assets/music/*.mus` |
| `make img_conv` | PNG → `.bin` sprite converter | `assets/sprites/*.bin` |

To reseed a data file from its defaults:

```sh
make seed_items
make seed_shops
make seed_enemies
make seed_actions
make seed_npcs
make seed_dialogs
make seed_social_encounters
# etc.
```

---

## Editor Guides

### Navigation conventions

All ncurses editors share the same controls:

- **Up / Down** — move selection or cycle through fields
- **Enter** — open the selected item or confirm
- **Backspace / Q** — go up one level; Q at the top level quits
- **A / N** — add / new entry
- **D** — delete selected entry
- **S** — save to disk
- **+  /  -** — increment / decrement a numeric field
- **E** — open an inline text editor for the selected string field
- **T** — toggle a flag (where applicable)

All editors save to `assets/data/` relative to the repo root.

---

### editor_hub

`make editor_hub` opens a menu-driven launcher for all editors. Editors are grouped by category:

- **World** — map editor
- **Combat** — enemy, action, log message editors
- **Items & Loot** — item, shop, loot table editors
- **NPCs & Story** — NPC, dialog, social encounter, quest, ambient editors
- **Investigations** — clue editor, investigation editor
- **Environmental Encounters** — env encounter, hunt encounter, camp zone editors
- **Player** — player starting stats editor
- **Audio** — music editor

Up/Down to move, Enter to launch the selected editor. The hub suspends ncurses, hands control to the editor, and resumes when you quit it. The data file for each entry is shown on the right.

Editors must be built before use — the hub launches whatever binary is already in `build/`. Build all at once with `make <editor_name>`, or just build the ones you need.

---

### npc_editor

Edits `assets/data/npcs.dat`. Two screens: list and edit.

Each NPC is a 40-byte `NpcDef`:

| Field | Notes |
|---|---|
| `name` | Display name (up to 15 chars) |
| `imgName` | 2-char sprite key — loads `assets/sprites/<key>.bin` |
| `treeId` | Index of this NPC's dialog tree; `0xFF` = no dialog |
| `x`, `y` | Tile coordinates on the map |
| `mapId` | Base filename of the map this NPC appears on (e.g. `map1`) |
| `resistance` | How hard it is to shift their disposition during a social encounter |
| `patience` | How many player moves before the NPC disengages |
| `social_power` | Strength of NPC counter-moves |
| `move_mask` | Which counter-moves are available: `DISMISS` `THREATEN` `LEAVE` |
| `tags` | Personality tags: `FEARFUL` `NOBLE` `CRIMINAL` `CONNECTED` `FEARLESS` |
| `base_standing` | Long-term standing with the player (0–100); modifies starting disposition |

---

### dialog_editor

Edits `assets/data/dialog.dat`. Four screens: trees → nodes → node → option.

**Trees** — each tree belongs to one NPC (linked via `treeId` in `npc_editor`).

**Nodes** — each node is one NPC line of speech plus up to 4 player responses.

**Options** — each option has six fields:

| Field | Notes |
|---|---|
| `Player text` | What the player says (up to 40 chars). `E` to edit |
| `Req. domain` | Domain required to unlock this option; `None` = always visible |
| `Req. level` | Minimum level in that domain |
| `Next node` | Node index to jump to; `-1` closes the dialog |
| `Effect` | `None` or `Social Encounter` |
| `Effect arg` | For `Social Encounter`: the `se_idx` in `social_encounters.dat` |

Effects fire **after** navigation — if `Next node` is `-1` the dialog closes first, then the encounter starts.

---

### social_encounter_editor

Edits `assets/data/social_encounters.dat`. Two screens: list and edit.

Each encounter is an 8-byte `SocialEncounterDef`:

| Field | Notes |
|---|---|
| `NPC id` | Index of the NPC in `npcs.dat` |
| `Reward partial` | Reward granted on partial success |
| `Reward full` | Reward granted on full success |
| `Disp start` | Starting disposition (0 = hostile, 100 = friendly). Modified by NPC standing unless `DISP_OVERRIDE` is set |
| `ONE_SHOT` | Encounter can only succeed/fail once per save. **T** to toggle |
| `DISP_OVERRIDE` | Ignore standing; always use `disp_start`. **T** to toggle |
| `HIDDEN` | Not listed in the journal until discovered. **T** to toggle |

---

### enemy_editor

Edits `assets/data/enemies.dat`. Manages two types: individual `EnemyDef` entries and `EnemyPool` groups.

**EnemyDef fields:** name, hp, attack, defense, size, speed, intelligence, perception, flags, xpReward, goldDrop, lootTableId, imgName (2-char sprite key).

**EnemyPool:** groups of up to 4 enemy IDs that can spawn together, with a tile name for the spawning area.

---

### action_editor

Edits `assets/data/actions.dat`. Two screens: list and edit.

Each `ActionDef` (64 bytes):

| Field | Notes |
|---|---|
| `name` | Display name (up to 15 chars) |
| `desc` | Short description (up to 31 chars) |
| `domain` | Which domain this action belongs to |
| `power` | Base damage/effect magnitude |
| `baseWeight` | Base probability weight in the action pool |
| `contextFlags` | Bitmask restricting when this action appears |
| `encounterCategory` | Which encounter types this action is valid for |
| `imgName` | Sprite key for the action icon |

---

### item_editor

Edits `assets/data/items.dat`. Two screens: list and edit.

Each `ItemDef` (64 bytes): name, type (weapon / outfit / consumable / undergarment / accessory / relic), stat bonuses (attack, defense, intelligence, perception, stamina, hp), price, description, and up to 4 action IDs the item grants on equip (`0xFF` = empty slot).

---

### shop_editor

Edits `assets/data/shops.dat`. Two screens: list and edit.

Each `ShopDef` (48 bytes): name (up to 23 chars) and up to 16 item slots. Shops are referenced by index — shop 0 is the default shop opened by the B key in the world.

In the edit screen, Enter on a stock slot opens an item picker loaded from `items.dat`. D removes the selected slot. The `(add item...)` entry at the bottom appends a new slot.

To open a specific shop from code: `enterShop(shopId, returnState)`.

---

### loottable_editor

Edits `assets/data/loottables.dat`. Three screens: tables → entries → entry.

Each loot table holds up to 8 `LootEntry` records:

| Field | Notes |
|---|---|
| `itemId` | Item to drop |
| `chance` | Drop probability (0–255, shown as a percentage) |
| `questId` | If set, only drops when this quest is at the given status |
| `questStatus` | Required quest status for the conditional drop |

---

### quest_editor

Edits `assets/data/quests.dat`. Three screens: quests → quest → objective.

Each `QuestDef` (48 bytes): name, up to 4 objectives, rewardXp, rewardItemId, flags, startType and startId (what triggers the quest).

Each objective: type, targetId, required count.

---

### logmessage_editor

Edits `assets/data/log_messages.dat`. Two screens: list and edit.

Each `LogMessage` (40 bytes) defines a reactive combat event: a text string (up to 27 chars) that fires under specific conditions and optionally applies an effect.

| Field | Notes |
|---|---|
| `text` | Message displayed in the combat log |
| `trigger` | What fires this message (action used, enemy type, encounter type, etc.) |
| `actionId` / `enemyDefId` | Specific action or enemy to match |
| `chance` | Probability this message fires when triggered (0–255) |
| `modRequired` | Minimum modifier value required |
| `enemy condition` | Additional enemy-state condition |
| `effectType` / `effectTarget` / `effectValue` | Optional stat effect applied when the message fires |

---

### ambient_editor

Edits `assets/data/ambient.dat`. Two screens: list and entry.

Each `AmbientEntry` (80 bytes) is a text snippet that appears when the player is within a bounding box on a specific map. Can be gated on quest state.

| Field | Notes |
|---|---|
| `mapId` | Map this applies to |
| `leftX`, `rightX`, `topY`, `bottomY` | Tile-coordinate bounding box |
| `text` | Ambient text to display (up to 63 chars) |
| `questIdx` / `questStatus` | Optional quest gate |
| `flags` | Display flags |

---

### player_editor

Edits `assets/data/player.dat`. Single-screen editor — no list, just the one player record.

Fields: maxHp, attack, defense, weaponId, armorId, 16 skill slots, level, xp, skillPoints. Use this to set starting conditions for a new game.

---

### clue_editor

Edits `assets/data/clues.dat`. Two screens: list and edit.

Clues are global — they are referenced by ID from investigation scenes. Each `ClueDef` (80 bytes):

| Field | Notes |
|---|---|
| `text` | Log entry shown when the clue is discovered (up to 31 chars). **Enter** to edit |
| `Item hint` | Flavour hint shown if a required item is missing (up to 31 chars). **Enter** to edit |
| `Difficulty` | Discovery difficulty 0–255; lower is easier. **+/-** |
| `Domain` | Domain affinity; matching domain level adds a discovery bonus. **+/-**, `0xFF` = none |
| `Affinity[0–3]` | Up to 4 action IDs that boost discovery odds. **+/-**, `0xFF` = unused |
| `Required item` | (Reserved, unused in current logic.) **+/-**, `0xFF` = none |
| `Invalidates` | ID of another clue this grays out in the journal when discovered. **+/-**, `0xFF` = none |
| `Flag: KEY` | This clue must be found for the investigation to succeed. **T** to toggle |
| `Flag: MISLEADING` | Red herring — points toward a wrong conclusion. **T** to toggle |
| `Flag: LINGERING` | Shown in the journal even if the investigation times out. **T** to toggle |

Clue IDs are global and stable — deleting a clue shifts IDs of everything after it, so avoid deleting from the middle of a live dataset. Add new clues at the end.

---

### investigation_editor

Edits `assets/data/investigations.dat`. Two screens: list and edit.

Each `InvestigationDef` (80 bytes) is a scene: a named set of clues with a turn limit, a success condition, and optional rewards.

| Field | Notes |
|---|---|
| `Name` | Journal group heading (up to 23 chars). **Enter** to edit |
| `Pressure text` | Fictional reason the scene has a time limit (up to 35 chars). **Enter** to edit |
| `Clue slot 0–7` | Global clue ID in each slot. **+/-**, `0xFF` = empty. **K** on a slot toggles whether it is a key clue |
| `Clue count` | Number of active clue slots. Set to match filled slots; the editor shows the auto-count for reference. **+/-** |
| `Turn limit` | How many action turns before the scene times out. **+/-** |
| `Key mask` | Bitmask of which clue positions are mandatory for success. Managed via **K** on clue slots; can also be adjusted directly with **+/-** |
| `Reward item` | Item ID granted on success. **+/-**, `0xFF` = none |
| `Reward quest` | Quest index advanced on success. **+/-**, `0xFF` = none |
| `Pressure type` | Flavour category: `Weather`, `Crowd`, `Darkness`, `Decay`. **+/-** |
| `Flag: STAGED` | Scene contains misleading clues (sets player expectation). **T** to toggle |
| `Flag: RETURNABLE` | Player can exit and re-enter the scene without losing progress. **T** to toggle |

The key mask determines victory: the player must find all clues whose slot position has its bit set. For example, if clue slots 0 and 2 are mandatory, key mask is `0x05`.

---

### env_encounter_editor

Edits `assets/data/env_encounters.dat`. Four screens in a drill-down hierarchy: **List -> Encounter -> State -> Edge**.

**Tab** switches focus between the field panel (left) and the sub-list panel (right) on the Encounter and State screens. All other keys act on whichever panel is focused.

#### Encounter screen (SCR_ENC)

| Field | Notes |
|---|---|
| `Name` | Display name shown in the encounter panel (up to 23 chars). **Enter** to edit |
| `Progress goal` | Total progress needed to end the encounter. **+/-** |
| `Start state` | Index of the state the encounter begins in. **+/-** |
| `REPEATABLE` | Player can enter this encounter again after resolution. **T** to toggle |
| `STORY` | Marks this as a story encounter; allows up to 8 states. **T** to toggle |

The right panel lists states. **N** adds a state, **D** deletes, **Enter** opens it.

#### State screen (SCR_STATE)

| Field | Notes |
|---|---|
| `Description` | Log text shown when the player enters this state (up to 23 chars). **Enter** to edit |
| `Turn budget` | Turns before the timeout fires; `0` = no timeout. **+/-** |
| `Timeout next` | State index to advance to when turn budget expires; `0xFF` = stay. **+/-** |
| `Damage` | HP deducted from the player on entering this state. **+/-** |
| `Prog gain` | Progress added each time an edge fires from this state. **+/-** |
| `TERMINAL` | No further turns after entering; only finishing-touch edges are shown. **T** to toggle |
| `SUCCESS` | Determines a good outcome when the encounter ends in this state. **T** to toggle |

The right panel lists edges. **N** adds an edge, **D** deletes, **Enter** opens it.

#### Edge screen (SCR_EDGE)

An edge is a connection in the state graph: one or more actions that trigger it, where they lead, and what they grant on resolution.

| Field | Notes |
|---|---|
| `Action ID 0–2` | Up to 3 action IDs that fire this edge. **+/-**, `0xFF` = unused |
| `Next state` | Destination state index. `0xFF` = **RESOLVE** (end encounter here). **+/-** |
| `Set world flag` | World flag ID (0–127) to set when this edge resolves. Only applies when `Next state` is `0xFF`. **+/-**, `0xFF` = none |
| `Reward item` | Item ID to grant on resolve. Only applies when `Next state` is `0xFF`. **+/-**, `0xFF` = none |

All numeric fields cycle through their range and wrap to/from `0xFF` (none/resolve) at the boundaries.

---

### music_editor

Edits `.mus` files under `assets/music/`. The ncurses version (`make music_editor`) and the Win32 GUI version (`make music_editor_gui`) both drive the same software synthesizer used in-game. Sequences are authored as a list of instruments and note events.

---

## Workflow: Adding a Social Encounter

Social encounters are triggered from dialog options, not from direct NPC interaction. The full setup takes four steps.

**Step 1 — Define the NPC** (`make npc_editor`)

Create or edit an NPC entry. The social fields that matter:
- `resistance` — how hard it is to shift their disposition
- `patience` — how many player moves before they disengage
- `social_power` — strength of their counter-moves
- `move_mask` — which counter-moves are available (DISMISS / THREATEN / LEAVE)
- `tags` — personality tags that domain unlock checks use
- `base_standing` — long-term standing; modifies starting disposition

Note the NPC's **index** in the list.

**Step 2 — Define the encounter** (`make social_encounter_editor`)

Add a new entry (N). Set:
- `NPC id` → the NPC's index from step 1
- `Disp start` → starting disposition (0 = hostile, 100 = friendly)
- `Reward partial` / `Reward full` → reward indices for each outcome
- Toggle `ONE_SHOT` if this encounter should only complete once

Note the encounter's **index** in the list — this is `se_idx`.

**Step 3 — Wire it into a dialog option** (`make dialog_editor`)

Open the NPC's dialog tree. On the node that should lead to the encounter, add or edit an option:
- `Next node` → `-1` (closes the dialog)
- `Effect` → `Social Encounter`
- `Effect arg` → `se_idx` from step 2
- Optionally gate the option behind a domain/level requirement

**Step 4 — Link the NPC to the tree** (back in `make npc_editor`)

Set the NPC's `treeId` to the dialog tree's index. The NPC will now offer that dialog when the player interacts with them, and the encounter starts when they select the wired option.

The Village Elder in `assets/data/dialog.dat` (tree 1, node 1, option 0) and the first entry in `assets/data/social_encounters.dat` are a working example of this setup.

---

## Data formats

All formats are raw binary structs, `memcpy`-loadable with no parsing step.

| File | Format |
|---|---|
| `assets/data/*.dat` | `[1 byte count][N × fixed-size struct]` |
| `assets/maps/*.bin` | `[2 byte w][2 byte h][mapGfx layer][mapLoc layer]` |
| `assets/sprites/*.bin` | `[1 byte w][1 byte h][w×h RGBA pixels]` |
| `assets/data/*.mus` | Custom sequencer format (instruments + notes) |
| `data.pak` | `PAK0` magic + entry table + raw file data |

---

## Project structure

```
src/
  core/       main loop, gfx, audio, pak loader, save
  gameplay/   combat, actions, items, domains, player, enemies, loot
  world/      world map, town, dialog system
  ui/         menus, inventory, shop, quests, npcs
  data/       embedded font and tile data
tools/        all editors and converters (host-compiled)
assets/       maps, sprites, tiles, music, data files
```

---

*Built with no external runtime dependencies. Ships as `game.exe` + `data.pak`.*
