#pragma once
#include <stdint.h>
#include "pak.h"
#include "effects.h"

/* Enemy capability flags — same bits as ENEMY_* in encounter.h */
#define EDEF_HAS_WEAPON  (1<<0)
#define EDEF_EXECUTABLE  (1<<1)
#define EDEF_BLOCKABLE   (1<<2)
#define EDEF_STUNNABLE   (1<<3)

#define ENEMY_DEF_MAX    32
#define ENEMY_POOL_MAX   15
#define ENEMY_POOL_SIZE   4

/* State-keyed enemy behavior: while the enemy occupies stateId, fx is
   applied to the player each turn (chance rolled inside applyEffect). */
typedef struct {
    uint8_t stateId;
    Effect  fx;
} EnemyBehavior; /* 4 bytes */

/* 56 bytes — fixed stats, no per-level scaling */
typedef struct {
    char    name[16];
    uint8_t hp;             /* LEGACY — combat runs on the state graph  */
    uint8_t attack;         /* LEGACY — combat pressure uses damage      */
    uint8_t defense;        /* LEGACY */
    uint8_t size;           /* 1=tiny .. 5=huge */
    uint8_t speed;
    uint8_t intelligence;
    uint8_t perception;
    uint8_t flags;          /* EDEF_* bitfield */
    uint8_t xpReward;
    uint8_t goldDrop;        /* max gold dropped; actual = rand(1..goldDrop) */
    uint8_t lootTableId;     /* index into lootTables[]; 0xFF = no loot table */
    char    imgName[16];     /* base name of .bin sprite, e.g. "goblin" → assets/sprites/goblin.bin */
    uint8_t stateMask;       /* bit per combat-graph state the enemy can enter */
    uint8_t damage;          /* added to its state's pressure each turn */
    uint8_t tenacity;        /* % scaling of progress banked against it; 0 = 100 */
    EnemyBehavior behaviors[2]; /* state-keyed effects on the player */
    uint8_t id;              /* stable id — what other data files reference */
    uint8_t _pad;
} EnemyDef;                  /* 56 bytes */

typedef char _check_enemydef_size[(sizeof(EnemyDef) == 56) ? 1 : -1];

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
const EnemyDef *enemyGetDef(uint8_t id);   /* by stable id; NULL if unknown */
int  enemyIndexById(uint8_t id);           /* slot in enemyDefs[]; -1 if unknown */
void encounterStartFromPool(uint8_t poolId, int triggerX, int triggerY);
void encounterStartFromPoolN(uint8_t poolId, int n); /* spawn exactly n enemies from pool */
