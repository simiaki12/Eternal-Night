#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include "world.h"
#include "game.h"
#include "gfx.h"
#include "enemies.h"
#include "combat.h"
#include "town.h"
#include "player.h"
#include "quests.h"
#include "npcs.h"
#include "ambient.h"
#include "tiles8x8.h"
#include "pak.h"
#include "shop.h"
#include "world_enemies.h"

/* ── iso tile images, lazy-loaded on first render ── */
#define TIMG_GRASS       0
#define TIMG_GRASS_1     1
#define TIMG_GRASS_2     2
#define TIMG_GRASS_ENEMY 3
#define TIMG_GRASS_TOWN  4
#define TIMG_GRASS_DUNG  5
#define TIMG_GRASS_PORT  6
#define TIMG_RIVER       7
#define TIMG_ROAD        8
#define TIMG_BRIDGE      9
#define TIMG_BLDG_FLOOR  10
#define TIMG_CAVE_FLOOR  11
#define TIMG_HILLS       12
#define TIMG_WALL        13
#define TIMG_CAVE_WALL   14
#define TIMG_MOUNTAINS   15
#define TIMG_TREE        16
#define TIMG_TAVERN_WALL 17
#define N_TILE_IMGS      18

static PakData g_tileImgs[N_TILE_IMGS];
static int     g_tilesLoaded = 0;

/* Per-pool world map tiles — loaded lazily on first draw */
static PakData g_poolTiles[ENEMY_POOL_MAX];
static int     g_poolTileLoaded[ENEMY_POOL_MAX];

static void loadTileImgs(void) {
    static const char *paths[N_TILE_IMGS] = {
        "assets/tiles/grass.til",
        "assets/tiles/grass_1.til",
        "assets/tiles/grass_2.til",
        "assets/tiles/enemy.til",
        "assets/tiles/house.til",
        "assets/tiles/grass_dungeon.bin",
        "assets/tiles/portal.til",
        "assets/tiles/water.til",
        "assets/tiles/road.til",
        "assets/tiles/bridge.til",
        "assets/tiles/bldg_floor.bin",
        "assets/tiles/cave.til",
        "assets/tiles/hills.til",
        "assets/tiles/map_wall.til",
        "assets/tiles/cave_wall.til",
        "assets/tiles/mountain.til",
        "assets/tiles/tree.bin",
        "assets/tiles/tavern_wall.til",
    };
    for (int i = 0; i < N_TILE_IMGS; i++)
        g_tileImgs[i] = pakRead(paths[i]);
    g_tilesLoaded = 1;
}

static int tileHash(int x, int y, int n) {
    uint32_t h = (uint32_t)(x * 374761393u + y * 668265263u);
    h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
    return (int)(h % (uint32_t)n);
}

int g_enemyWallTransparency = 0; /* OFF by default — enemies hide behind walls */

#define AMBIENT_MS 5000
static char  g_ambientMsg[128] = {0};
static DWORD g_ambientExpiry   = 0;

void ambientShow(const char *msg) {
    strncpy(g_ambientMsg, msg, sizeof(g_ambientMsg) - 1);
    g_ambientMsg[sizeof(g_ambientMsg) - 1] = '\0';
    g_ambientExpiry = GetTickCount() + AMBIENT_MS;
}

static const MapEvent *findEvent(int x, int y); /* forward declaration */

/* --- Interaction target list --- */
#define MAX_TARGETS 8

typedef struct { int isNpc; int idx; } Target;

static Target g_targets[MAX_TARGETS];
static int    g_targetCount = 0;
static int    g_targetIdx   = 0;

static void buildTargets(void) {
    g_targetCount = 0;
    g_targetIdx   = 0;

    const MapEvent *ev = findEvent(worldPlayerX, worldPlayerY);
    if (ev && ev->type != MAP_EV_ENEMY)
        g_targets[g_targetCount++] = (Target){ 0, 0 };

    int npcIdxs[MAX_TARGETS];
    int nc = npcGetInteractable(npcIdxs, MAX_TARGETS - g_targetCount);
    for (int i = 0; i < nc && g_targetCount < MAX_TARGETS; i++)
        g_targets[g_targetCount++] = (Target){ 1, npcIdxs[i] };
}

int     worldPlayerX  = 2;
int     worldPlayerY  = 2;
static int   g_walkFrame      = 0;
static int   g_moving         = 0;
static int   g_camSlideStartX = 0;
static int   g_camSlideStartY = 0;
static int   g_camSlideX      = 0;
static int   g_camSlideY      = 0;
static int   g_plrOffStartX   = 0;
static int   g_plrOffStartY   = 0;
static int   g_plrOffX        = 0;
static int   g_plrOffY        = 0;
static DWORD g_moveStart      = 0;
static int   g_midToggled     = 0;
#define SLIDE_MS 200
int     camX         = 0;
int     camY         = 0;
int     mapWidth     = 0;
int     mapHeight    = 0;
uint8_t mapGfx[MAX_MAP_TILES];
MapEvent mapEvents[MAX_MAP_EVENTS];
int      mapEventCount = 0;
char    currentMapName[64] = {0};

static const MapEvent *findEvent(int x, int y) {
    for (int i = 0; i < mapEventCount; i++)
        if (mapEvents[i].x == (uint8_t)x && mapEvents[i].y == (uint8_t)y)
            return &mapEvents[i];
    return NULL;
}

void worldUpdateCamera(void) {
    camX = (worldPlayerX - worldPlayerY) * (TILE_W / 2);
    camY = (worldPlayerX + worldPlayerY) * (TILE_H / 2);
}

static int loadMap(PakData data) {
    if (data.size < 5) return 0;
    mapWidth  = data.data[0];
    mapHeight = data.data[1];
    int spawnX = data.data[2];
    int spawnY = data.data[3];
    int n = mapWidth * mapHeight;
    if ((int)data.size < 4 + n + 1) return 0;

    memcpy(mapGfx, data.data + 4, n);

    mapEventCount = data.data[4 + n];
    int evBytes = mapEventCount * (int)sizeof(MapEvent);
    if ((int)data.size < 4 + n + 1 + evBytes) {
        mapEventCount = 0;
    } else {
        memcpy(mapEvents, data.data + 4 + n + 1, evBytes);
    }

    worldPlayerX = spawnX;
    worldPlayerY = spawnY;
    return 1;
}

int worldLoadNamed(const char *name) {
    char safeName[64];
    strncpy(safeName, name, sizeof(safeName) - 1);
    safeName[sizeof(safeName) - 1] = '\0';

    PakData data = pakRead(safeName);
    if (!data.data) return 0;
    int ok = loadMap(data);
    free(data.data);
    if (ok) {
        strncpy(currentMapName, safeName, sizeof(currentMapName) - 1);
        currentMapName[sizeof(currentMapName) - 1] = '\0';
        worldUpdateCamera();
        g_moving       = 0;
        g_camSlideX    = g_camSlideStartX = 0;
        g_camSlideY    = g_camSlideStartY = 0;
        g_plrOffX      = g_plrOffStartX   = 0;
        g_plrOffY      = g_plrOffStartY   = 0;
        g_targetCount  = 0;
        g_targetIdx    = 0;
        worldEnemiesInit();
    }
    return ok;
}

void updateWorld(void) {
    if (!g_moving) {
        g_walkFrame = 0;
    } else {
        DWORD elapsed = GetTickCount() - g_moveStart;
        if (elapsed >= SLIDE_MS) {
            g_camSlideX = 0;
            g_camSlideY = 0;
            g_plrOffX   = 0;
            g_plrOffY   = 0;
            g_moving    = 0;
        } else {
            if (!g_midToggled && elapsed >= SLIDE_MS / 2) {
                g_walkFrame  ^= 1;
                g_midToggled  = 1;
            }
            int rem      = (int)(SLIDE_MS - elapsed);
            g_camSlideX  = g_camSlideStartX * rem / SLIDE_MS;
            g_camSlideY  = g_camSlideStartY * rem / SLIDE_MS;
            g_plrOffX    = g_plrOffStartX   * rem / SLIDE_MS;
            g_plrOffY    = g_plrOffStartY   * rem / SLIDE_MS;
        }
    }
    int poolId = worldEnemiesUpdate((uint32_t)GetTickCount());
    if (poolId >= 0) {
        startCombatFromPool((uint8_t)poolId);
        combat.fromWorldEnemy = 1;
        combat.worldEnemyX    = (uint8_t)worldPlayerX;
        combat.worldEnemyY    = (uint8_t)worldPlayerY;
    }
}

static void triggerMapEvent(const MapEvent *ev) {
    questOnZoneEntered(ev->id);
    switch (ev->type) {
        case MAP_EV_TOWN:
            startTown();
            break;
        case MAP_EV_DUNGEON:
            if (ev->destMap[0]) worldLoadNamed(ev->destMap);
            else                state = STATE_DUNGEON;
            break;
        case MAP_EV_PORTAL:
            if (ev->destMap[0]) {
                int dx = ev->destX, dy = ev->destY;
                worldLoadNamed(ev->destMap);
                if (dx < mapWidth && dy < mapHeight &&
                        IS_GFX_PASSABLE(mapGfx[dy * mapWidth + dx])) {
                    worldPlayerX = dx;
                    worldPlayerY = dy;
                    worldUpdateCamera();
                }
            }
            break;
        default: break;
    }
}

void handleWorldInput(int key) {
    if (key == 'I') { state = STATE_INVENTORY; return; }
    if (key == 'B') { enterShop(STATE_WORLD);  return; }
    if (key == VK_TAB) {
        if (g_targetCount > 1)
            g_targetIdx = (g_targetIdx + 1) % g_targetCount;
        return;
    }
    if (key == 'E') {
        if (g_targetCount > 0) {
            const Target *t = &g_targets[g_targetIdx];
            if (t->isNpc) {
                npcTriggerByIdx(t->idx);
            } else {
                const MapEvent *ev = findEvent(worldPlayerX, worldPlayerY);
                if (ev) triggerMapEvent(ev);
            }
        }
        return;
    }
    if (g_moving) return;

    int newX = worldPlayerX, newY = worldPlayerY;
    switch (key) {
        case VK_UP:    newY--; break;
        case VK_DOWN:  newY++; break;
        case VK_LEFT:  newX--; break;
        case VK_RIGHT: newX++; break;
        default: return;
    }

    if (newX >= 0 && newX < mapWidth &&
        newY >= 0 && newY < mapHeight &&
        IS_GFX_PASSABLE(mapGfx[newY * mapWidth + newX])) {
        int dx       = newX - worldPlayerX;
        int dy       = newY - worldPlayerY;
        int oldCamX  = camX;
        int oldCamY  = camY;
        worldPlayerX = newX;
        worldPlayerY = newY;
        g_walkFrame ^= 1;
        worldUpdateCamera();
        g_camSlideStartX = oldCamX - camX;
        g_camSlideStartY = oldCamY - camY;
        g_camSlideX      = g_camSlideStartX;
        g_camSlideY      = g_camSlideStartY;
        g_plrOffStartX   = -(dx - dy) * (TILE_W / 2);
        g_plrOffStartY   = -(dx + dy) * (TILE_H / 2);
        g_plrOffX        = g_plrOffStartX;
        g_plrOffY        = g_plrOffStartY;
        g_moveStart      = GetTickCount();
        g_midToggled     = 0;
        g_moving         = 1;

        buildTargets();
        questOnLocationReached(currentMapName, (uint8_t)newX, (uint8_t)newY);
        ambientCheckLocation(currentMapName, (uint8_t)newX, (uint8_t)newY);

        /* Enemies auto-trigger on step; town/dungeon/portal wait for E key. */
        const WorldEnemy *we = worldEnemyAt(newX, newY);
        if (we) {
            uint8_t ex = we->x, ey = we->y;
            questOnZoneEntered(we->pool_id);
            startCombatFromPool(we->pool_id);
            combat.fromWorldEnemy = 1;
            combat.worldEnemyX    = ex;
            combat.worldEnemyY    = ey;
        } else {
            const MapEvent *ev = findEvent(newX, newY);
            if (ev && ev->type != MAP_EV_ENEMY) questOnZoneEntered(ev->id);
        }
    }
}

void returnToTown(void) {
    worldPlayerX = 2;
    worldPlayerY = 2;
    worldUpdateCamera();
}

void renderWorld(void) {
    if (!g_tilesLoaded) loadTileImgs();

    uint32_t now  = (uint32_t)GetTickCount();
    int rCamX = camX + g_camSlideX;
    int rCamY = camY + g_camSlideY;

    fillRect(0, 0, gfxWidth, gfxHeight, rgb(10, 10, 15));

    int playerSum  = worldPlayerX + worldPlayerY;
    int plrScreenX = (worldPlayerX - worldPlayerY) * (TILE_W / 2) - rCamX + gfxWidth  / 2 + g_plrOffX;
    int plrScreenY = (worldPlayerX + worldPlayerY) * (TILE_H / 2) - rCamY + gfxHeight / 2 + g_plrOffY;

    /* Tall tiles are deferred within each sum pass so entities drawn in the
       flat-tile pass always end up behind walls/trees at the same depth. */
    typedef struct { int tx, ty, img_idx; } DeferredTall;
    DeferredTall deferred[128];
    int nDeferred = 0;

    for (int sum = 0; sum < mapWidth + mapHeight - 1; sum++) {
        nDeferred = 0;

        /* --- Sub-pass 1: flat tiles + NPCs + player + enemies --- */
        for (int tx = 0; tx <= sum; tx++) {
            int ty = sum - tx;
            if (tx < 0 || tx >= mapWidth || ty < 0 || ty >= mapHeight) continue;

            int cx = (tx - ty) * (TILE_W / 2) - rCamX + gfxWidth  / 2;
            int cy = (tx + ty) * (TILE_H / 2) - rCamY + gfxHeight / 2;

            if (cx + TILE_W / 2 < 0 || cx - TILE_W / 2 > gfxWidth)  continue;
            if (cy + 96 < 0 || cy > gfxHeight + 2 * TILE_H)            continue;

            uint8_t gfx = mapGfx[ty * mapWidth + tx];
            int img_idx;

            switch (gfx) {
                case GFX_WALL:           img_idx = TIMG_WALL;       break;
                case GFX_TREE:           img_idx = TIMG_TREE;       break;
                case GFX_RIVER:          img_idx = TIMG_RIVER;      break;
                case GFX_BRIDGE:         img_idx = TIMG_BRIDGE;     break;
                case GFX_ROAD:           img_idx = TIMG_ROAD;       break;
                case GFX_BUILDING_FLOOR: img_idx = TIMG_BLDG_FLOOR; break;
                case GFX_HILLS:          img_idx = TIMG_HILLS;       break;
                case GFX_MOUNTAINS:      img_idx = TIMG_MOUNTAINS;  break;
                case GFX_CAVE_FLOOR:     img_idx = TIMG_CAVE_FLOOR; break;
                case GFX_CAVE_WALL:      img_idx = TIMG_CAVE_WALL;   break;
                case GFX_TAVERN_WALL:    img_idx = TIMG_TAVERN_WALL; break;
                default: {
                    if (worldEnemyAt(tx, ty)) {
                        /* Enemy present — draw terrain only; icon drawn after tile below */
                        static const int g_evars[] = { TIMG_GRASS, TIMG_GRASS_1, TIMG_GRASS_2 };
                        img_idx = g_evars[tileHash(tx, ty, 3)];
                    } else {
                        const MapEvent *ev = findEvent(tx, ty);
                        if (ev && ev->type != MAP_EV_ENEMY) {
                            switch (ev->type) {
                                case MAP_EV_TOWN:    img_idx = TIMG_GRASS_TOWN; break;
                                case MAP_EV_DUNGEON: img_idx = TIMG_GRASS_DUNG; break;
                                case MAP_EV_PORTAL:  img_idx = TIMG_GRASS_PORT; break;
                                default:             img_idx = TIMG_GRASS;      break;
                            }
                        } else {
                            static const int g_vars[] = { TIMG_GRASS, TIMG_GRASS_1, TIMG_GRASS_2 };
                            img_idx = g_vars[tileHash(tx, ty, 3)];
                        }
                    }
                    break;
                }
            }

            int rotate = 0;
            if (img_idx == TIMG_GRASS || img_idx == TIMG_GRASS_1 || img_idx == TIMG_GRASS_2
                    || img_idx == TIMG_ROAD)
                rotate = tileHash(tx * 7 + 1, ty * 13 + 1, 4);

            PakData *td = &g_tileImgs[img_idx];
            if (td->data) {
                int tileH = (int)((const uint8_t *)td->data)[1];
                if (tileH > TILE_H / 2) {
                    /* Tall tile — defer until after entities at this sum level. */
                    if (nDeferred < 128)
                        deferred[nDeferred++] = (DeferredTall){tx, ty, img_idx};
                } else {
                    int draw_y = cy + TILE_H - tileH * 2 + (tileH >= TILE_H/2 ? 2 : 0);
                    drawBin(cx - TILE_W / 2, draw_y, (const uint8_t *)td->data, 2, rotate, 255);
                }
            }

            renderNpcs(tx, ty, rCamX, rCamY);
            if (tx == worldPlayerX && ty == worldPlayerY) {
                int px = cx + g_plrOffX;
                int py = cy + g_plrOffY;
                const uint8_t *spr = g_walkFrame ? SPRITE_PLAYER_2 : SPRITE_PLAYER;
                drawSprite8(px - 16, py + TILE_H / 2 - 32, spr, TILE_PAL, 4);
            }
            {
                for (int ei = 0; ei < worldEnemyCount; ei++) {
                    const WorldEnemy *we = &worldEnemies[ei];
                    /* Draw at the higher-sum tile so the source flat tile
                       (painted later in the loop) never overwrites the sprite. */
                    int draw_tx = we->x, draw_ty = we->y;
                    if (we->is_moving &&
                        (int)(we->prev_x + we->prev_y) > (int)(we->x + we->y)) {
                        draw_tx = we->prev_x;
                        draw_ty = we->prev_y;
                    }
                    if (tx != draw_tx || ty != draw_ty) continue;

                    /* Resolve pool tile — lazy load on first use. */
                    PakData *etd = &g_tileImgs[TIMG_GRASS_ENEMY];
                    int pi = (int)we->pool_id - 1;
                    if (pi >= 0 && pi < enemyPoolCount) {
                        if (!g_poolTileLoaded[pi]) {
                            g_poolTileLoaded[pi] = 1;
                            const char *tn = enemyPools[pi].tileName;
                            if (tn[0]) {
                                char path[48];
                                snprintf(path, sizeof(path), "assets/tiles/%s.til", tn);
                                g_poolTiles[pi] = pakRead(path);
                            }
                        }
                        if (g_poolTiles[pi].data) etd = &g_poolTiles[pi];
                    }

                    if (!etd->data) continue;
                    int eTileH = (int)((const uint8_t *)etd->data)[1];
                    int ex = (we->x - we->y) * (TILE_W / 2) - rCamX + gfxWidth  / 2;
                    int ey = (we->x + we->y) * (TILE_H / 2) - rCamY + gfxHeight / 2;
                    int eDrawY = ey + TILE_H - eTileH * 2 + (eTileH >= TILE_H/2 ? 2 : 0);
                    int offX = 0, offY = 0;
                    if (we->is_moving) worldEnemySlideOffset(we, now, &offX, &offY);
                    drawBin(ex - TILE_W / 2 + offX, eDrawY + offY,
                            (const uint8_t *)etd->data, 2, 0, 255);
                }
            }
        }

        /* --- Sub-pass 2: tall tiles, drawn after all entities at this sum level --- */
        for (int di = 0; di < nDeferred; di++) {
            int tx = deferred[di].tx;
            int ty = deferred[di].ty;
            int img_idx = deferred[di].img_idx;
            int cx = (tx - ty) * (TILE_W / 2) - rCamX + gfxWidth  / 2;
            int cy = (tx + ty) * (TILE_H / 2) - rCamY + gfxHeight / 2;
            PakData *td = &g_tileImgs[img_idx];
            if (!td->data) continue;
            int tileH  = (int)((const uint8_t *)td->data)[1];
            int draw_y = cy + TILE_H - tileH * 2 + (tileH >= TILE_H/2 ? 2 : 0);
            uint8_t alpha = 255;
            /* >= because tall tiles now render after player even at same sum level */
            if (sum >= playerSum) {
                if (abs(cx - plrScreenX) < TILE_W / 2 + 16 && draw_y < plrScreenY + 16)
                    alpha = 100;
            }
            if (g_enemyWallTransparency && alpha == 255) {
                for (int ei = 0; ei < worldEnemyCount; ei++) {
                    const WorldEnemy *we = &worldEnemies[ei];
                    if (sum < (int)(we->x + we->y)) continue;
                    int ex = (we->x - we->y) * (TILE_W / 2) - rCamX + gfxWidth  / 2;
                    int ey = (we->x + we->y) * (TILE_H / 2) - rCamY + gfxHeight / 2;
                    if (abs(cx - ex) < TILE_W / 2 + 16 && draw_y < ey + 16) {
                        alpha = 100;
                        break;
                    }
                }
            }
            drawBin(cx - TILE_W / 2, draw_y, (const uint8_t *)td->data, 2, 0, alpha);
        }
    }

    /* --- Interaction prompt above player --- */
    if (g_targetCount > 0) {
        char prompt[96] = {0};
        const Target *t = &g_targets[g_targetIdx];

        if (t->isNpc) {
            const NpcDef *n = &npcDefs[t->idx];
            snprintf(prompt, sizeof(prompt), "[E]: Talk with %s",
                     n->name[0] ? n->name : "Stranger");
        } else {
            const MapEvent *ev = findEvent(worldPlayerX, worldPlayerY);
            if (ev) switch (ev->type) {
                case MAP_EV_TOWN:    snprintf(prompt, sizeof(prompt), "[E]: Enter Town");    break;
                case MAP_EV_DUNGEON: snprintf(prompt, sizeof(prompt), "[E]: Enter Dungeon"); break;
                case MAP_EV_PORTAL:  snprintf(prompt, sizeof(prompt), "[E]: Enter Portal");  break;
                default: break;
            }
        }

        if (g_targetCount > 1) {
            size_t len = strlen(prompt);
            snprintf(prompt + len, sizeof(prompt) - len, "  [Tab]");
        }

        if (prompt[0]) {
            int tw = (int)strlen(prompt) * 8;
            drawTextOutlined(plrScreenX - tw / 2, plrScreenY - 30, prompt, rgb(255, 255, 180), 1);
        }
    }

    /* --- Ambient / narrative text at bottom of screen --- */
    if (g_ambientMsg[0] && GetTickCount() < g_ambientExpiry) {
        int tw = (int)strlen(g_ambientMsg) * 8;
        drawTextOutlined(gfxWidth / 2 - tw / 2, gfxHeight - 44, g_ambientMsg, rgb(210, 195, 155), 1);
    }
}
