# Eternal Night

A dark fantasy retro RPG optimized for distribution on floppies

## The Game

The Kingdom of Lucernia is in turmoil. The Queen has been kidnapped and the only one who can save her is the mysterious vampire knight, the Daywalker. Will he be able to save the day, what other mysteries will he uncover, can he prepare for a final confrontation with his arch-nemesis.

## The Constraint

This project is designed to fit on a single floppy disk and the be playable on a Windows system with no additional dependencies or requirements. 
Current size: 85 KB exe + 300 KB data.pak out of 1.44 MB

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

Combats are round based. Each turn the player can choose from a selection of 4 actions, which are determined semi-eandomly based on their equipment, skills, enemy and surrounding. 

---

## Skills

Skills unlock new abilities and increase the power of Azrael.

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
  gameplay/   combat, actions, items, skills, player, enemies, loot
  world/      world map, town
  ui/         menus, inventory, shop, quests, dialog, npcs
  data/       embedded font and tile data
tools/        all editors and converters (host-compiled)
assets/       maps, sprites, tiles, music, data files
```

---

*Built with no external runtime dependencies. Ships as `game.exe` + `data.pak`.*
