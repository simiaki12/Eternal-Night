#pragma once
#include <stdint.h>
#include "pak.h"
#include "map_format.h"

#define TILE_SIZE     64
#define TILE_W        64          /* iso diamond screen width  */
#define TILE_H        32          /* iso diamond screen height */
#define WALL_H        TILE_H      /* wall front-face height    */

extern int      g_enemyWallTransparency;
extern int      worldPlayerX;
extern int      worldPlayerY;
extern int      camX;
extern int      camY;
extern int      mapWidth;
extern int      mapHeight;
extern uint8_t  mapTiles[MAX_MAP_TILES];   /* palette index per tile */
extern MapEvent mapEvents[MAX_MAP_EVENTS];
extern int      mapEventCount;
extern char     currentMapName[64];

/* Per-map palette, loaded with the map. */
extern MapTile  mapPalette[MAP_PAL_MAX];
extern int      mapPaletteCount;

/* Walkable test for a tile byte — empty and out-of-range tiles are solid. */
int  tilePassable(uint8_t tile);

int  worldLoadNamed(const char *name);
void worldUpdateCamera(void);
void updateWorld(void);
void handleWorldInput(int key);
void renderWorld(void);
void returnToTown(void);
void ambientShow(const char *msg); /* show narrative text at bottom for 5 seconds */
