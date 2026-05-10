#include <stdlib.h>
#include <windows.h>
#include "world_enemies.h"
#include "world.h"

#define WANDER_MS     2000u   /* base ms between wander moves */
#define WANDER_JITTER 1500u   /* random extra ms added to avoid synchronised movement */

WorldEnemy worldEnemies[MAX_WORLD_ENEMIES];
int        worldEnemyCount = 0;

void worldEnemiesInit(void) {
    worldEnemyCount = 0;
    for (int i = 0; i < mapEventCount && worldEnemyCount < MAX_WORLD_ENEMIES; i++) {
        if (mapEvents[i].type != MAP_EV_ENEMY) continue;
        WorldEnemy *we    = &worldEnemies[worldEnemyCount++];
        we->home_x        = mapEvents[i].x;
        we->home_y        = mapEvents[i].y;
        we->x             = mapEvents[i].x;
        we->y             = mapEvents[i].y;
        we->pool_id       = mapEvents[i].id;
        we->wander_radius = 1;
        we->style         = MOVE_WANDER;
        we->is_moving     = 0;
        we->move_start    = 0;
        we->prev_x        = we->x;
        we->prev_y        = we->y;
        we->next_move     = (uint32_t)GetTickCount() + (uint32_t)(rand() % WANDER_MS);
    }
}

void worldEnemyRemoveAt(int x, int y) {
    for (int i = 0; i < worldEnemyCount; i++) {
        if (worldEnemies[i].x == (uint8_t)x && worldEnemies[i].y == (uint8_t)y) {
            worldEnemies[i] = worldEnemies[--worldEnemyCount];
            return;
        }
    }
}

const WorldEnemy *worldEnemyAt(int x, int y) {
    if (x < 0 || y < 0) return NULL;
    for (int i = 0; i < worldEnemyCount; i++)
        if (worldEnemies[i].x == (uint8_t)x && worldEnemies[i].y == (uint8_t)y)
            return &worldEnemies[i];
    return NULL;
}

/* Eight cardinal + diagonal directions */
static const int s_dx[8] = { -1, 0, 1, -1, 1, -1, 0,  1 };
static const int s_dy[8] = { -1, -1, -1, 0, 0,  1, 1,  1 };

static void wanderUpdate(WorldEnemy *we, uint32_t now) {
    if (now < we->next_move) return;

    /* Fisher-Yates shuffle of direction indices for unbiased random exploration */
    int order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    for (int i = 7; i > 0; i--) {
        int j   = rand() % (i + 1);
        int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
    }

    for (int i = 0; i < 8; i++) {
        int nx = we->x + s_dx[order[i]];
        int ny = we->y + s_dy[order[i]];

        if (nx < 0 || nx >= mapWidth || ny < 0 || ny >= mapHeight) continue;
        if (abs(nx - (int)we->home_x) > (int)we->wander_radius)    continue;
        if (abs(ny - (int)we->home_y) > (int)we->wander_radius)    continue;
        if (!IS_GFX_PASSABLE(mapGfx[ny * mapWidth + nx]))          continue;
        if (worldEnemyAt(nx, ny))                                   continue;

        we->prev_x     = we->x;
        we->prev_y     = we->y;
        we->x          = (uint8_t)nx;
        we->y          = (uint8_t)ny;
        we->is_moving  = 1;
        we->move_start = now;
        break;
    }

    we->next_move = now + WANDER_MS + (uint32_t)(rand() % WANDER_JITTER);
}

int worldEnemiesUpdate(uint32_t now) {
    for (int i = 0; i < worldEnemyCount; i++) {
        WorldEnemy *we = &worldEnemies[i];
        if (we->is_moving && now - we->move_start >= ENEMY_SLIDE_MS)
            we->is_moving = 0;
        switch (we->style) {
            case MOVE_WANDER: wanderUpdate(we, now); break;
            case MOVE_STATIC: break;
            default:          break;
        }
        if (we->x == (uint8_t)worldPlayerX && we->y == (uint8_t)worldPlayerY)
            return we->pool_id;
    }
    return -1;
}

void worldEnemySlideOffset(const WorldEnemy *we, uint32_t now, int *offX, int *offY) {
    *offX = *offY = 0;
    if (!we->is_moving) return;
    uint32_t elapsed = now - we->move_start;
    if (elapsed >= ENEMY_SLIDE_MS) return;
    int rem  = (int)(ENEMY_SLIDE_MS - elapsed);
    /* Camera terms cancel; offset is purely a function of tile delta */
    *offX = ((int)we->prev_x - (int)we->prev_y - (int)we->x + (int)we->y)
            * (TILE_W / 2) * rem / (int)ENEMY_SLIDE_MS;
    *offY = ((int)we->prev_x + (int)we->prev_y - (int)we->x - (int)we->y)
            * (TILE_H / 2) * rem / (int)ENEMY_SLIDE_MS;
}
