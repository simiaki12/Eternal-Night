#pragma once
#include <stdint.h>
#include "pak.h"

#define TILE_SIZE     64
#define TILE_W        64          /* iso diamond screen width  */
#define TILE_H        32          /* iso diamond screen height */
#define WALL_H        TILE_H      /* wall front-face height    */
#define MAX_MAP_TILES (256 * 256)

/* Tile type constants — determines appearance and passability */
#define GFX_GRASS          0
#define GFX_WALL           1
#define GFX_TREE           2
#define GFX_RIVER          3
#define GFX_BRIDGE         4
#define GFX_ROAD           5
#define GFX_BUILDING_FLOOR 6
#define GFX_HILLS          7
#define GFX_MOUNTAINS      8
#define GFX_CAVE_FLOOR     9
#define GFX_CAVE_WALL      10
#define GFX_TAVERN_WALL    11

#define IS_GFX_PASSABLE(g) ((g)==GFX_GRASS||(g)==GFX_BRIDGE||(g)==GFX_ROAD|| \
                            (g)==GFX_BUILDING_FLOOR||(g)==GFX_HILLS||(g)==GFX_CAVE_FLOOR)

/* Map events — sparse list of interactive tiles.
 *
 * Binary format (per event, 40 bytes):
 *   uint8  x, y        — tile coordinates
 *   uint8  type        — MapEventType
 *   uint8  id          — pool ID for ENEMY; zone ID for quest triggers; 0 otherwise
 *   uint8  destX, destY — spawn point in destination map (PORTAL / DUNGEON)
 *   char   destMap[32] — pak path of destination map; empty = none
 *   uint8  _pad[2]
 *
 * Map file layout:
 *   [uint8 w][uint8 h][uint8 spawnX][uint8 spawnY]
 *   [w*h uint8 tiles]
 *   [uint8 eventCount][eventCount × MapEvent]
 */
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
    uint8_t _pad[2];
} MapEvent;               /* 40 bytes */

#define MAX_MAP_EVENTS 255

extern int      worldPlayerX;
extern int      worldPlayerY;
extern int      camX;
extern int      camY;
extern int      mapWidth;
extern int      mapHeight;
extern uint8_t  mapGfx[MAX_MAP_TILES];
extern MapEvent mapEvents[MAX_MAP_EVENTS];
extern int      mapEventCount;
extern char     currentMapName[64];

int  worldLoadNamed(const char *name);
void worldUpdateCamera(void);
void updateWorld(void);
void handleWorldInput(int key);
void renderWorld(void);
void returnToTown(void);
void ambientShow(const char *msg); /* show narrative text at bottom for 5 seconds */
