#pragma once
#include <stdint.h>

/* Map file format v2 — shared by the engine, the map editor and the migrator.
 * Dependency-free on purpose: host tools include this directly.
 *
 * Layout:
 *   "TMP2"                                  magic
 *   [uint8 w][uint8 h][uint8 spawnX][uint8 spawnY]
 *   [uint8 nameCount][nameCount × char[24]] tile asset names (basename+ext)
 *   [uint8 palCount][palCount × MapTile]    per-map palette, 16 bytes each
 *   [w*h uint8]                             palette index; MAP_TILE_EMPTY = void
 *   [uint8 eventCount][eventCount × MapEvent]
 *
 * Tile assets live under assets/tiles/. Names are stored bare ("grass.til")
 * and the prefix is added at load time.
 */

#define MAP_MAGIC       "TMP2"
#define MAP_NAMES_MAX   64
#define MAP_NAME_LEN    24
#define MAP_PAL_MAX     16
#define MAP_TILE_NAMES  8      /* names one palette slot may reference */
#define MAP_TILE_EMPTY  0xFF   /* tile byte meaning "nothing here"     */
#define MAP_NAME_NONE   0xFF   /* name index meaning "unset"           */
#define MAX_MAP_TILES   (256 * 256)
#define MAX_MAP_EVENTS  255

/* MapTile.color carries an ncurses colour pair 1-7, optionally bolded. */
#define MAP_COLOR_BOLD  0x80
#define MAP_COLOR_MASK  0x0F

/* MapTile.flags */
#define TF_PASSABLE     0x01   /* player may walk onto it              */
#define TF_ROT_RANDOM   0x02   /* pick rotation from tile position     */
#define TF_AUTOTILE     0x04   /* shape from neighbours; excludes ROT_RANDOM */

/* A palette slot. `name` holds indices into the map's name table:
 *   plain      — nameCount 1
 *   pool       — nameCount >1, one picked per tile by position hash
 *   autotile   — nameCount 6, ordered as the MAP_SHAPE_* list below,
 *                rotated to suit the neighbour mask
 */
typedef struct {
    uint8_t name[MAP_TILE_NAMES];
    uint8_t nameCount;
    uint8_t flags;
    uint8_t family;        /* autotile connection group; 0 = connects to nothing */
    uint8_t glyph, color;  /* how the editor draws this slot; color | MAP_COLOR_BOLD */
    uint8_t _pad[3];
} MapTile;                 /* 16 bytes */

/* Autotile shape order. Sprites are authored in tile space — "north" is the
 * up-left screen edge of the diamond — and rotated 90° clockwise per step. */
#define MAP_SHAPE_ISOLATED 0   /* no connections                  */
#define MAP_SHAPE_END      1   /* one, connecting north           */
#define MAP_SHAPE_STRAIGHT 2   /* two opposite, north–south       */
#define MAP_SHAPE_CORNER   3   /* two adjacent, north+east        */
#define MAP_SHAPE_TEE      4   /* three, all but south            */
#define MAP_SHAPE_CROSS    5   /* four                            */
#define MAP_SHAPE_COUNT    6

/* Neighbour mask bits, in tile space. */
#define MAP_CONN_N 0x1
#define MAP_CONN_E 0x2
#define MAP_CONN_S 0x4
#define MAP_CONN_W 0x8

/* Neighbour mask -> {shape, rotation}, rotation in 90° clockwise steps. */
typedef struct { uint8_t shape, rot; } MapAutoTile;

static inline MapAutoTile mapAutoTile(int mask) {
    static const MapAutoTile t[16] = {
        /* ----  */ { MAP_SHAPE_ISOLATED, 0 },
        /* N     */ { MAP_SHAPE_END,      0 },
        /* E     */ { MAP_SHAPE_END,      1 },
        /* NE    */ { MAP_SHAPE_CORNER,   0 },
        /* S     */ { MAP_SHAPE_END,      2 },
        /* NS    */ { MAP_SHAPE_STRAIGHT, 0 },
        /* ES    */ { MAP_SHAPE_CORNER,   1 },
        /* NES   */ { MAP_SHAPE_TEE,      1 },
        /* W     */ { MAP_SHAPE_END,      3 },
        /* NW    */ { MAP_SHAPE_CORNER,   3 },
        /* EW    */ { MAP_SHAPE_STRAIGHT, 1 },
        /* NEW   */ { MAP_SHAPE_TEE,      0 },
        /* SW    */ { MAP_SHAPE_CORNER,   2 },
        /* NSW   */ { MAP_SHAPE_TEE,      3 },
        /* ESW   */ { MAP_SHAPE_TEE,      2 },
        /* NESW  */ { MAP_SHAPE_CROSS,    0 },
    };
    return t[mask & 15];
}

/* Map events — sparse list of interactive tiles. */
typedef enum {
    MAP_EV_ENEMY   = 0,
    MAP_EV_TOWN    = 1,
    MAP_EV_DUNGEON = 2,
    MAP_EV_PORTAL  = 3,
} MapEventType;

typedef struct {
    uint8_t x, y;
    uint8_t type;
    uint8_t id;           /* pool ID for ENEMY; zone ID for quest triggers */
    uint8_t destX, destY;
    char    destMap[32];
    uint8_t tileName;     /* name index drawn instead of terrain; MAP_NAME_NONE = none */
    uint8_t _pad;
} MapEvent;               /* 40 bytes */
