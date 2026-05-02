CC       = x86_64-w64-mingw32-gcc
CXX_HOST = g++
CC_HOST  = gcc
CFLAGS   = -Wall -Wextra -std=c11 -I src/core -I src/gameplay -I src/world -I src/ui -I src/data
DEBUGFLAGS   = -g
RELEASEFLAGS = -Os -flto
LDFLAGS        = -lgdi32 -lwinmm -lmsimg32 -mwindows
LDFLAGS_STATIC = $(LDFLAGS) -static-libgcc

SRC    = $(shell find src -name '*.c')
OUT    = build/game.exe
PACKER      = build/packer
MAP_EDITOR  = build/map_editor
PLR_EDITOR  = build/player_editor
DLG_EDITOR  = build/dialog_editor
QST_EDITOR  = build/quest_editor
ENM_EDITOR  = build/enemy_editor
NPC_EDITOR  = build/npc_editor
ACT_EDITOR  = build/action_editor
AMB_EDITOR  = build/ambient_editor
IMG_CONV    = build/img_conv
BW_CONV     = build/bw_conv
RLE         = build/rle
RES         = build/resources.o

$(RES): resources.rc icon.ico
	mkdir -p build
	x86_64-w64-mingw32-windres resources.rc -o $(RES)

debug: pack $(RES)
	$(CC) $(CFLAGS) $(DEBUGFLAGS) $(SRC) $(RES) -o $(OUT) $(LDFLAGS)

release: pack $(RES)
	$(CC) $(CFLAGS) $(RELEASEFLAGS) $(SRC) $(RES) -o $(OUT) $(LDFLAGS_STATIC) -s
	ls -lh $(OUT)

audio_demo:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os tools/audio_demo.c -o build/audio_demo
	./build/audio_demo

gen_world_music:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os tools/gen_world_music.c -o build/gen_world_music
	./build/gen_world_music

gen_iso_tiles:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os tools/gen_iso_tiles.c -o build/gen_iso_tiles
	./build/gen_iso_tiles

packer:
	mkdir -p build
	$(CXX_HOST) -std=c++17 -Os tools/packer.cpp -o $(PACKER)

pack: packer
	$(PACKER)

map_editor:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os -Wno-unused-result tools/map_editor.c -o $(MAP_EDITOR) -lncurses

player_editor:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os -Wno-unused-result tools/player_editor.c -o $(PLR_EDITOR) -lncurses

dialog_editor:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os -Wno-unused-result tools/dialog_editor.c -o $(DLG_EDITOR) -lncurses

quest_editor:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os -Wno-unused-result tools/quest_editor.c -o $(QST_EDITOR) -lncurses

seed_quests:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os tools/seed_quests.c -o build/seed_quests
	./build/seed_quests

seed_dialogs:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os tools/seed_dialogs.c -o build/seed_dialogs
	./build/seed_dialogs

seed_items:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os tools/seed_items.c -o build/seed_items
	./build/seed_items

seed_loottables:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os tools/seed_loottables.c -o build/seed_loottables
	./build/seed_loottables

seed_enemies:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os tools/seed_enemies.c -o build/seed_enemies
	./build/seed_enemies

migrate_maps:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os tools/migrate_map.c -o build/migrate_map
	./build/migrate_map assets/maps/*.bin

img_conv:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os tools/img_conv.c -o $(IMG_CONV) -lm

img_conv_ui:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os -Wno-unused-result tools/img_conv_ui.c -o build/img_conv_ui -lncurses

bw_conv:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os tools/bw_conv.c -o $(BW_CONV) -lm

rle:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os tools/rle.c -o $(RLE)

item_editor:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os -Wno-unused-result tools/item_editor.c -o build/item_editor -lncurses

loottable_editor:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os -Wno-unused-result tools/loottable_editor.c -o build/loottable_editor -lncurses

enemy_editor:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os -Wno-unused-result tools/enemy_editor.c -o $(ENM_EDITOR) -lncurses

npc_editor:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os -Wno-unused-result tools/npc_editor.c -o $(NPC_EDITOR) -lncurses

action_editor:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os -Wno-unused-result tools/action_editor.c -o $(ACT_EDITOR) -lncurses

ambient_editor:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os -Wno-unused-result tools/ambient_editor.c -o $(AMB_EDITOR) -lncurses

seed_npcs:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os tools/seed_npcs.c -o build/seed_npcs
	./build/seed_npcs

seed_actions:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os tools/seed_actions.c -o build/seed_actions
	./build/seed_actions

music_editor:
	mkdir -p build
	$(CC_HOST) -std=c11 -Os -Wno-unused-result tools/music_editor.c -o build/music_editor -lncurses

music_editor_gui:
	mkdir -p build
	$(CC) -std=c11 -Os tools/music_editor_gui.c -o build/music_editor_gui.exe -lgdi32 -lwinmm -lcomdlg32 -mwindows

tools: map_editor player_editor dialog_editor quest_editor item_editor loottable_editor enemy_editor npc_editor action_editor ambient_editor img_conv img_conv_ui bw_conv rle music_editor music_editor_gui

clean:
	rm -rf build data.pak
