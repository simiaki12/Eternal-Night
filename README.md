# Eternal Night

A dark fantasy retro RPG optimized for distribution on floppies

## The Game

The Kingdom of Lucernia is in turmoil. The Queen has been kidnapped, and the only one who can save her is Azrael — the Daywalker.

Eternal Night is a dark fantasy RPG with an isometric overworld to navigate, towns to visit, and a cast of NPCs with their own agendas. Exploration is real-time: enemies roam the world and react to your presence. Combat triggers when you engage and plays out as a turn-based exchange of actions.

Azrael grows not through levelling up, but through how he fights. The more you rely on a fighting style, the deeper you go into that domain, unlocking new capabilities that reflect the kind of warrior you're becoming.

## The Constraint

This project is designed to fit on a single floppy disk and the be playable on a Windows system with no additional dependencies or requirements. 
Current size: 113 KB exe + 306 KB data.pak (29% of 1.44 MB floppy)

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

## Combat

Combat is turn-based. Each round you are offered 4 actions drawn from your available repertoire. The chance of certain actions appearing is weighted by your domain levels, your equipment, the enemy type, and your surroundings. Pick an action, pick a target, and resolve. Then enemies act in sequence.

Encounters support up to 1 enemy at once, multiple is planned. Each enemy will have its own action set, so groups feel distinct rather than just multiplying the same threat.

Outside of combat, enemies move in real time. Most are slower than Azrael, so you can choose your fights. Enemies have behaviour archetypes: some hunt you at range, some ignore you until you cross their territory, some flee and try to call allies. Breaking line of sight will eventually cause them to give up the chase.

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

| Command | Purpose |
|---|---|
| `make map_editor` | Tile map editor |
| `make item_editor` | Items + action grants |
| `make enemy_editor` | Enemy stats, flags, loot tables |
| `make action_editor` | Combat action definitions |
| `make dialog_editor` | NPC dialog trees |
| `make quest_editor` | Quest definitions |
| `make npc_editor` | NPC placement |
| `make loottable_editor` | Loot table composition |
| `make music_editor` | Software synth sequencer (ncurses) |
| `make music_editor_gui` | Software synth sequencer (Win32 GUI) |
| `make img_conv` | PNG → `.bin` sprite converter |

To reseed a data file from its defaults:

```sh
make seed_items
make seed_enemies
make seed_actions
make seed_npcs
# etc.
```

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
  world/      world map, town
  ui/         menus, inventory, shop, quests, dialog, npcs
  data/       embedded font and tile data
tools/        all editors and converters (host-compiled)
assets/       maps, sprites, tiles, music, data files
```

---

*Built with no external runtime dependencies. Ships as `game.exe` + `data.pak`.*
