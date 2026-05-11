#pragma once
#include <stdint.h>
#include "pak.h"

/* Enemy capability flags — same bits as ENEMY_* in combat.h */
#define EDEF_HAS_WEAPON  (1<<0)
#define EDEF_EXECUTABLE  (1<<1)
#define EDEF_BLOCKABLE   (1<<2)
#define EDEF_STUNNABLE   (1<<3)

#define ENEMY_DEF_MAX    32
#define ENEMY_POOL_MAX   15
#define ENEMY_POOL_SIZE   4

/* 48 bytes — fixed stats, no per-level scaling */
typedef struct {
    char    name[16];
    uint8_t hp;
    uint8_t attack;
    uint8_t defense;
    uint8_t size;           /* 1=tiny .. 5=huge */
    uint8_t speed;
    uint8_t intelligence;
    uint8_t perception;
    uint8_t flags;          /* EDEF_* bitfield */
    uint8_t xpReward;
    uint8_t goldDrop;        /* max gold dropped; actual = rand(1..goldDrop) */
    uint8_t lootTableId;     /* index into lootTables[]; 0xFF = no loot table */
    char    imgName[16];     /* base name of .bin sprite, e.g. "goblin" → assets/sprites/goblin.bin */
    uint8_t _pad[5];
} EnemyDef;                  /* 48 bytes */

/* 24 bytes — maps a pool ID (loc tile value 0x01-0x0F) to a set of enemy types */
typedef struct {
    uint8_t enemyIds[ENEMY_POOL_SIZE]; /* indices into enemyDefs[], 0xFF = empty */
    uint8_t count;
    uint8_t _pad[3];
    char    tileName[16];   /* world map tile base name, e.g. "enemy_goblin" → assets/tiles/enemy_goblin.til */
} EnemyPool;                /* 24 bytes */

/* File format (enemies.dat):
 *   [1]      enemy count
 *   [N×32]   EnemyDef array
 *   [1]      pool count
 *   [M×8]    EnemyPool array  (pool index 0 = loc tile 0x01, etc.)
 */

extern EnemyDef  enemyDefs[ENEMY_DEF_MAX];
extern int       enemyDefCount;
extern EnemyPool enemyPools[ENEMY_POOL_MAX];
extern int       enemyPoolCount;

int  loadEnemies(PakData data);
void startCombatFromPool(uint8_t poolId);  /* poolId = loc tile value 0x01-0x0F */
