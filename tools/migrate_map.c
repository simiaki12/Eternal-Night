/* tools/migrate_map.c — convert v1 maps (global tile enum) to v2 (per-map palette).
 *
 * v1: [w][h][spawnX][spawnY][w*h gfx][eventCount][events × 40]
 * v2: ["TMP2"][w][h][spawnX][spawnY][nameCount][names × 24]
 *     [palCount][palette × 16][w*h tile][eventCount][events × 40]
 *
 * The emitted palette reproduces the hardcoded table the engine used to carry,
 * so migrated maps render identically: grass keeps its 3-variant pool and
 * random rotation, road keeps its rotation, and town/dungeon/portal events keep
 * their special sprites as per-event tile overrides.
 *
 * Files already in v2 are left alone, so re-running this is harmless.
 *
 * Usage: migrate_map <mapfile> [mapfile2 ...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../src/world/map_format.h"

/* The v1 global tile ids, in order. */
enum {
    GFX_GRASS = 0, GFX_WALL, GFX_TREE, GFX_RIVER, GFX_BRIDGE, GFX_ROAD,
    GFX_BUILDING_FLOOR, GFX_HILLS, GFX_MOUNTAINS, GFX_CAVE_FLOOR,
    GFX_CAVE_WALL, GFX_TAVERN_WALL, GFX_COUNT
};

/* Stock palette: one entry per v1 tile id, listing the assets the engine used
 * to hardcode. Names are resolved into the map's name table on the fly. */
typedef struct {
    const char *names[MAP_TILE_NAMES];
    uint8_t     nameCount;
    uint8_t     flags;
    uint8_t     glyph, color;
} StockTile;

static const StockTile STOCK[GFX_COUNT] = {
    /* GRASS          */ { {"grass.til", "grass_1.til", "grass_2.til"}, 3,
                           TF_PASSABLE | TF_ROT_RANDOM, '.', 5 },
    /* WALL           */ { {"map_wall.til"},     1, 0,           '#', 1 },
    /* TREE           */ { {"tree.bin"},         1, 0,           '^', 5 | MAP_COLOR_BOLD },
    /* RIVER          */ { {"water.til"},        1, 0,           '~', 7 },
    /* BRIDGE         */ { {"bridge.til"},       1, TF_PASSABLE, '=', 3 },
    /* ROAD           */ { {"road.til"},         1, TF_PASSABLE | TF_ROT_RANDOM, ':', 1 },
    /* BUILDING_FLOOR */ { {"bldg_floor.bin"},   1, TF_PASSABLE, '+', 6 },
    /* HILLS          */ { {"hills.til"},        1, TF_PASSABLE, 'n', 5 },
    /* MOUNTAINS      */ { {"mountain.til"},     1, 0,           'M', 1 | MAP_COLOR_BOLD },
    /* CAVE_FLOOR     */ { {"cave.til"},         1, TF_PASSABLE, ',', 4 },
    /* CAVE_WALL      */ { {"cave_wall.til"},    1, 0,           '%', 4 | MAP_COLOR_BOLD },
    /* TAVERN_WALL    */ { {"tavern_wall.til"},  1, 0,           '|', 3 | MAP_COLOR_BOLD },
};

/* Sprites the engine used to substitute for non-enemy events. */
static const char *EVENT_TILE[4] = {
    NULL,                   /* ENEMY   — icon drawn over terrain, no override */
    "house.til",            /* TOWN    */
    "grass_dungeon.bin",    /* DUNGEON */
    "portal.til",           /* PORTAL  */
};

/* --- name table ---------------------------------------------------- */

static char names[MAP_NAMES_MAX][MAP_NAME_LEN];
static int  nameCount = 0;

static uint8_t nameIntern(const char *n) {
    for (int i = 0; i < nameCount; i++)
        if (strcmp(names[i], n) == 0) return (uint8_t)i;
    if (nameCount >= MAP_NAMES_MAX) return MAP_NAME_NONE;
    snprintf(names[nameCount], MAP_NAME_LEN, "%s", n);
    return (uint8_t)nameCount++;
}

/* --- migration ----------------------------------------------------- */

static int migrateFile(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open: %s\n", path); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    if (sz < 5) { fprintf(stderr, "%s: too small\n", path); fclose(f); return 1; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return 1; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "%s: short read\n", path);
        free(buf); fclose(f); return 1;
    }
    fclose(f);

    if (memcmp(buf, MAP_MAGIC, 4) == 0) {
        printf("%s: already v2, skipped\n", path);
        free(buf);
        return 0;
    }

    int w = buf[0], h = buf[1];
    int spawnX = buf[2], spawnY = buf[3];
    int n = w * h;
    if (sz < 4 + n + 1) {
        fprintf(stderr, "%s: truncated (need %d bytes, got %ld)\n", path, 4 + n + 1, sz);
        free(buf);
        return 1;
    }

    uint8_t *tiles = buf + 4;

    int      evCount = buf[4 + n];
    MapEvent events[MAX_MAP_EVENTS];
    memset(events, 0, sizeof(events));
    if (evCount > MAX_MAP_EVENTS) evCount = MAX_MAP_EVENTS;
    if (sz < 4 + n + 1 + evCount * (long)sizeof(MapEvent)) {
        fprintf(stderr, "%s: event block truncated, dropping events\n", path);
        evCount = 0;
    } else {
        memcpy(events, buf + 4 + n + 1, (size_t)evCount * sizeof(MapEvent));
    }

    /* Build the name table and palette from the tiles this map actually uses.
       Palette indices stay equal to the old gfx ids so tile bytes need no
       remapping, and unused stock entries are dropped from the tail only. */
    nameCount = 0;
    int highest = -1;
    for (int i = 0; i < n; i++)
        if (tiles[i] < GFX_COUNT && tiles[i] > highest) highest = tiles[i];
    int palCount = highest + 1;

    MapTile palette[MAP_PAL_MAX];
    memset(palette, 0, sizeof(palette));
    for (int i = 0; i < palCount; i++) {
        const StockTile *st = &STOCK[i];
        MapTile *mt = &palette[i];
        for (int k = 0; k < st->nameCount; k++)
            mt->name[k] = nameIntern(st->names[k]);
        mt->nameCount = st->nameCount;
        mt->flags     = st->flags;
        mt->family    = 0;
        mt->glyph     = st->glyph;
        mt->color     = st->color;
    }

    /* Out-of-range tile bytes become empty rather than silently grass. */
    int stray = 0;
    for (int i = 0; i < n; i++)
        if (tiles[i] >= palCount) { tiles[i] = MAP_TILE_EMPTY; stray++; }

    /* Per-event tile overrides replace the old hardcoded substitutions. */
    for (int i = 0; i < evCount; i++) {
        events[i]._pad = 0;
        const char *tn = (events[i].type < 4) ? EVENT_TILE[events[i].type] : NULL;
        events[i].tileName = tn ? nameIntern(tn) : MAP_NAME_NONE;
    }

    FILE *o = fopen(path, "wb");
    if (!o) { fprintf(stderr, "Cannot write: %s\n", path); free(buf); return 1; }

    uint8_t hdr[8] = { 'T','M','P','2',
                       (uint8_t)w, (uint8_t)h, (uint8_t)spawnX, (uint8_t)spawnY };
    fwrite(hdr, 1, 8, o);
    uint8_t nc = (uint8_t)nameCount;
    fwrite(&nc, 1, 1, o);
    fwrite(names, MAP_NAME_LEN, (size_t)nameCount, o);
    uint8_t pc = (uint8_t)palCount;
    fwrite(&pc, 1, 1, o);
    fwrite(palette, sizeof(MapTile), (size_t)palCount, o);
    fwrite(tiles, 1, (size_t)n, o);
    uint8_t ec = (uint8_t)evCount;
    fwrite(&ec, 1, 1, o);
    fwrite(events, sizeof(MapEvent), (size_t)evCount, o);
    fclose(o);

    printf("%s: %dx%d, %d names, %d palette slots, %d events%s\n",
           path, w, h, nameCount, palCount, evCount,
           stray ? " (stray tiles cleared)" : "");
    free(buf);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mapfile> [mapfile2 ...]\n", argv[0]);
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++)
        rc |= migrateFile(argv[i]);
    return rc;
}
