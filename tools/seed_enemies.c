/* tools/seed_enemies.c — writes the initial assets/enemies.dat
 * Run once: make seed_enemies
 * After that, edit content here and re-run to regenerate. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Mirror of EDEF_* flags from enemies.h */
#define EDEF_HAS_WEAPON  (1<<0)
#define EDEF_EXECUTABLE  (1<<1)
#define EDEF_BLOCKABLE   (1<<2)
#define EDEF_STUNNABLE   (1<<3)

typedef struct { uint8_t type, value, chance; } Effect;
typedef struct { uint8_t stateId; Effect fx; } EnemyBehavior;

typedef struct {
    char    name[16];
    uint8_t hp, attack, defense;   /* hp/attack/defense LEGACY */
    uint8_t size, speed, intelligence, perception;
    uint8_t flags;
    uint8_t xpReward;
    uint8_t goldDrop;
    uint8_t lootTableId; /* 0xFF = none */
    char    imgName[16]; /* sprite base name, e.g. "goblin" → assets/sprites/goblin.bin */
    uint8_t stateMask;   /* combat-graph states the enemy can enter */
    uint8_t damage;      /* pressure added per turn */
    uint8_t tenacity;    /* % progress scaling; 0 = 100 */
    EnemyBehavior behaviors[2];
    uint8_t _pad[2];
} EnemyDef; /* 56 bytes */

typedef char _check_size[(sizeof(EnemyDef) == 56) ? 1 : -1];

/* Combat graph state bits (S0..S4) + effect ids */
#define S(n) (1u << (n))
#define BASE_STATES (S(0)|S(1)|S(2)|S(4))     /* no Frenzy */
#define ALL_STATES  (S(0)|S(1)|S(2)|S(3)|S(4))

#define EFX_DAMAGE_HP    4
#define EFX_APPLY_STATUS 6
#define STATUS_POISONED  2
#define CS_TRADE  1
#define CS_FRENZY 3

#define NOB  { 0, {0,0,0} }                    /* no behavior */
#define B(st, t, v, c) { st, { t, v, c } }

typedef struct {
    uint8_t enemyIds[4];
    uint8_t count;
    uint8_t _pad[3];
    char    tileName[16];
} EnemyPool;

static EnemyDef defs[] = {
    /* name           hp  atk def siz spd int per  flags                                              xp gold loot  imgName        states       dmg ten  behaviors */
    { "Goblin",       12,  4,  1,  1,  3,  1,  2,  EDEF_EXECUTABLE | EDEF_STUNNABLE,                 8,  1,  0,  "goblin",      BASE_STATES,  3, 100, { NOB, NOB }, {0} },
    { "Wolf",         10,  5,  0,  2,  4,  1,  3,  EDEF_STUNNABLE,                                   7,  1, 0xFF,"wolf",        ALL_STATES,   4, 100,
        { B(CS_FRENZY, EFX_DAMAGE_HP, 3, 50), NOB }, {0} },
    { "Skeleton",     20,  6,  2,  2,  2,  1,  1,  EDEF_HAS_WEAPON|EDEF_BLOCKABLE|EDEF_EXECUTABLE,  14,  3, 0xFF,"skeleton",    BASE_STATES,  5,  80, { NOB, NOB }, {0} },
    { "Bandit",       18,  7,  2,  2,  3,  3,  3,  EDEF_HAS_WEAPON|EDEF_EXECUTABLE|EDEF_STUNNABLE,  16,  5, 0xFF,"bandit",      ALL_STATES,   6, 100,
        { B(CS_FRENZY, EFX_DAMAGE_HP, 4, 40), NOB }, {0} },
    { "Giant Spider",  16,  5,  1,  2,  3,  1,  4,  EDEF_EXECUTABLE | EDEF_STUNNABLE,                10,  2, 0xFF,"giant_spider",ALL_STATES,   4,  90,
        { B(CS_TRADE, EFX_APPLY_STATUS, STATUS_POISONED, 20), NOB }, {0} },
    { "Dark Mage",    25,  8,  1,  1,  2,  5,  2,  EDEF_EXECUTABLE,                                 18,  4, 0xFF,"dark_mage",   BASE_STATES,  7,  70,
        { B(CS_TRADE, EFX_DAMAGE_HP, 4, 30), NOB }, {0} },
};

/* Pool 1 (loc 0x01): outdoor / forest */
/* Pool 2 (loc 0x02): dungeon / underground */
/* Pool 3 (loc 0x03): cave / spider nests  */
static EnemyPool pools[] = {
    { {0, 1, 0xFF, 0xFF}, 2, {0}, "enemy_forest"  },  /* pool 1: Goblin, Wolf         */
    { {2, 3, 0xFF, 0xFF}, 2, {0}, "enemy_dungeon" },  /* pool 2: Skeleton, Bandit     */
    { {4, 0, 0xFF, 0xFF}, 2, {0}, "enemy_cave"    },  /* pool 3: Giant Spider, Goblin */
    { {5, 3, 0xFF, 0xFF}, 2, {0}, "enemy_mage"    },  /* pool 4: Dark Mage, Bandit    */
};

int main(void) {
    FILE *f = fopen("assets/data/enemies.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot write assets/enemies.dat\n"); return 1; }

    int nDefs  = (int)(sizeof(defs)  / sizeof(defs[0]));
    int nPools = (int)(sizeof(pools) / sizeof(pools[0]));

    fwrite(&(uint8_t){(uint8_t)nDefs},  1, 1, f);
    fwrite(defs,  sizeof(EnemyDef),  nDefs,  f);
    fwrite(&(uint8_t){(uint8_t)nPools}, 1, 1, f);
    fwrite(pools, sizeof(EnemyPool), nPools, f);

    fclose(f);
    printf("Written assets/enemies.dat  (%d enemies, %d pools, %d bytes)\n",
        nDefs, nPools,
        1 + nDefs * (int)sizeof(EnemyDef) + 1 + nPools * (int)sizeof(EnemyPool));
    return 0;
}
