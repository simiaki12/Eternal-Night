#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include "enemies.h"
#include "encounter.h"
#include "world_enemies.h"

EnemyDef  enemyDefs[ENEMY_DEF_MAX];

const EnemyDef *enemyGetDef(uint8_t id) {
    for (int i = 0; i < enemyDefCount; i++)
        if (enemyDefs[i].id == id) return &enemyDefs[i];
    return NULL;
}

int enemyIndexById(uint8_t id) {
    for (int i = 0; i < enemyDefCount; i++)
        if (enemyDefs[i].id == id) return i;
    return -1;
}

int       enemyDefCount = 0;
EnemyPool enemyPools[ENEMY_POOL_MAX];
int       enemyPoolCount = 0;

/* Fallback used when enemies.dat is absent from the pak */
static void initBuiltinEnemies(void) {
    /* --- defs --- */
    /* stateMask bits = combat graph S0..S4; 0x17 = no Frenzy, 0x1F = all */
    static const EnemyDef builtins[] = {
        /*  name          hp  atk def siz spd int per flags                                              xp gold loot  imgName     states dmg ten  behaviors            id */
        { "Goblin",       12,  4,  1,  1,  3,  1,  2, EDEF_EXECUTABLE | EDEF_STUNNABLE,                 8,  1,  0,  "goblin",   0x17,  3, 100, {{0,{0,0,0}},{0,{0,0,0}}}, 0, 0 },
        { "Wolf",         10,  5,  0,  2,  4,  1,  3, EDEF_STUNNABLE,                                   7,  1, 0xFF,"wolf",     0x1F,  4, 100, {{0,{0,0,0}},{0,{0,0,0}}}, 1, 0 },
        { "Skeleton",     20,  6,  2,  2,  2,  1,  1, EDEF_HAS_WEAPON|EDEF_BLOCKABLE|EDEF_EXECUTABLE,  14,  3, 0xFF,"skeleton", 0x17,  5,  80, {{0,{0,0,0}},{0,{0,0,0}}}, 2, 0 },
        { "Bandit",       18,  7,  2,  2,  3,  3,  3, EDEF_HAS_WEAPON|EDEF_EXECUTABLE|EDEF_STUNNABLE,  16,  5, 0xFF,"bandit",   0x1F,  6, 100, {{0,{0,0,0}},{0,{0,0,0}}}, 3, 0 },
    };
    int n = (int)(sizeof(builtins) / sizeof(builtins[0]));
    memcpy(enemyDefs, builtins, (size_t)n * sizeof(EnemyDef));
    enemyDefCount = n;

    /* --- pools --- */
    /* Pool 1 (loc 0x01): outdoor / forest */
    enemyPools[0].enemyIds[0] = 0; /* Goblin */
    enemyPools[0].enemyIds[1] = 1; /* Wolf   */
    enemyPools[0].count = 2;

    /* Pool 2 (loc 0x02): dungeon / ruins */
    enemyPools[1].enemyIds[0] = 2; /* Skeleton */
    enemyPools[1].enemyIds[1] = 3; /* Bandit   */
    enemyPools[1].count = 2;

    enemyPoolCount = 2;
}

int loadEnemies(PakData data) {
    if (!data.data || data.size < 2) { initBuiltinEnemies(); return 0; }

    const uint8_t *d   = (const uint8_t *)data.data;
    uint32_t       pos = 0;

    uint8_t dc = d[pos++];
    if (dc > ENEMY_DEF_MAX) dc = ENEMY_DEF_MAX;
    if (pos + (uint32_t)dc * sizeof(EnemyDef) > data.size) { initBuiltinEnemies(); return 0; }
    memcpy(enemyDefs, d + pos, (size_t)dc * sizeof(EnemyDef));
    enemyDefCount = dc;
    pos += (uint32_t)dc * sizeof(EnemyDef);

    if (pos >= data.size) { enemyPoolCount = 0; return dc; }
    uint8_t pc = d[pos++];
    if (pc > ENEMY_POOL_MAX) pc = ENEMY_POOL_MAX;
    if (pos + (uint32_t)pc * sizeof(EnemyPool) > data.size) { enemyPoolCount = 0; return dc; }
    memcpy(enemyPools, d + pos, (size_t)pc * sizeof(EnemyPool));
    enemyPoolCount = pc;

    return dc;
}

static const int s_ndx[8] = { -1, 0, 1, -1, 1, -1, 0,  1 };
static const int s_ndy[8] = { -1,-1,-1,  0, 0,  1, 1,  1 };

void encounterStartFromPool(uint8_t poolId, int triggerX, int triggerY) {
    int idx = (int)poolId - 1;
    if (idx < 0 || idx >= enemyPoolCount || enemyPools[idx].count == 0) {
        if (enemyDefCount > 0) encounterStartCombat(&enemyDefs[0]);
        else return;
        encounter.fromWorldEnemy[0] = 1;
        encounter.worldEnemyX[0]    = (uint8_t)triggerX;
        encounter.worldEnemyY[0]    = (uint8_t)triggerY;
        return;
    }
    EnemyPool *pool = &enemyPools[idx];
    const EnemyDef *def = enemyGetDef(pool->enemyIds[rand() % pool->count]);
    if (!def) def = &enemyDefs[0];
    encounterStartCombat(def);
    encounter.fromWorldEnemy[0] = 1;
    encounter.worldEnemyX[0]    = (uint8_t)triggerX;
    encounter.worldEnemyY[0]    = (uint8_t)triggerY;

    /* Pull neighboring world enemies (up to ENCOUNTER_MAX_ENEMIES total) */
    for (int d = 0; d < 8 && encounter.enemyCount < ENCOUNTER_MAX_ENEMIES; d++) {
        int nx = triggerX + s_ndx[d];
        int ny = triggerY + s_ndy[d];
        const WorldEnemy *nwe = worldEnemyAt(nx, ny);
        if (!nwe) continue;
        int nidx = (int)nwe->pool_id - 1;
        if (nidx < 0 || nidx >= enemyPoolCount || enemyPools[nidx].count == 0) continue;
        EnemyPool *np  = &enemyPools[nidx];
        const EnemyDef *ndef = enemyGetDef(np->enemyIds[rand() % np->count]);
        if (!ndef) continue;
        encounterAddEnemy(ndef, (uint8_t)nx, (uint8_t)ny);
    }
}

void encounterStartFromPoolN(uint8_t poolId, int n) {
    if (n <= 0) return;
    if (n > ENCOUNTER_MAX_ENEMIES) n = ENCOUNTER_MAX_ENEMIES;

    int idx = (int)poolId - 1;
    if (idx < 0 || idx >= enemyPoolCount || enemyPools[idx].count == 0) {
        if (enemyDefCount > 0) {
            encounterStartCombat(&enemyDefs[0]);
            for (int i = 1; i < n; i++)
                encounterAddEnemy(&enemyDefs[0], 0, 0);
        }
        return;
    }
    EnemyPool *pool = &enemyPools[idx];
    const EnemyDef *def = enemyGetDef(pool->enemyIds[rand() % pool->count]);
    if (!def) def = &enemyDefs[0];
    encounterStartCombat(def);
    for (int i = 1; i < n; i++) {
        const EnemyDef *d2 = enemyGetDef(pool->enemyIds[rand() % pool->count]);
        encounterAddEnemy(d2 ? d2 : &enemyDefs[0], 0, 0);
    }
}
