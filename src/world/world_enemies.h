#pragma once
#include <stdint.h>

#define MAX_WORLD_ENEMIES 64
#define ENEMY_SLIDE_MS    200   /* matches player slide duration */

typedef enum {
    MOVE_STATIC = 0,
    MOVE_WANDER,
    MOVE_PATROL,
    MOVE_RANDOM,
    MOVE_ROUTE,
} MoveStyle;

typedef struct {
    uint8_t   home_x, home_y;   /* spawn position */
    uint8_t   x, y;             /* current (logical) position */
    uint8_t   prev_x, prev_y;  /* position before last move, for animation */
    uint8_t   pool_id;          /* MapEvent.id — which enemy pool to use */
    uint8_t   wander_radius;    /* max tiles from home allowed */
    uint8_t   is_moving;        /* non-zero while slide animation is in progress */
    MoveStyle style;
    uint32_t  next_move;        /* GetTickCount() timestamp of next movement */
    uint32_t  move_start;       /* GetTickCount() when last move began */
} WorldEnemy;

extern WorldEnemy worldEnemies[MAX_WORLD_ENEMIES];
extern int        worldEnemyCount;

void              worldEnemiesInit(void);
int               worldEnemiesUpdate(uint32_t now); /* returns pool_id if enemy stepped on player, else -1 */
const WorldEnemy *worldEnemyAt(int x, int y);
void              worldEnemySlideOffset(const WorldEnemy *we, uint32_t now, int *offX, int *offY);
