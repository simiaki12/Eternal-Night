#pragma once
#include <stdint.h>
#include "enemies.h"
#include "pak.h"
#include "actions.h"

/* Encounter type — values are ACT_CAT_* bits so they can be passed directly
   to buildActionPool() as the category filter. */
typedef enum {
    ENCOUNTER_COMBAT        = ACT_CAT_COMBAT,
    ENCOUNTER_SOCIAL        = ACT_CAT_SOCIAL,
    ENCOUNTER_INVESTIGATION = ACT_CAT_INVESTIGATION,
    ENCOUNTER_HUNT          = ACT_CAT_HUNT,
    ENCOUNTER_ENVIRONMENTAL = ACT_CAT_ENVIRONMENTAL,
} EncounterType;

/* Encounter modifiers — environmental conditions that shift action weights
   and gate modifier-dependent context flags. */
#define ENCOUNTER_MOD_DARK        (1<<0)
#define ENCOUNTER_MOD_RAINING     (1<<1)
#define ENCOUNTER_MOD_HOLY_GROUND (1<<2)
#define ENCOUNTER_MOD_CROWDED     (1<<3)
#define ENCOUNTER_MOD_BURNING     (1<<4)

/* --- Enemy capability flags --- */
#define ENEMY_HAS_WEAPON  (1<<0)
#define ENEMY_EXECUTABLE  (1<<1)
#define ENEMY_BLOCKABLE   (1<<2)
#define ENEMY_STUNNABLE   (1<<3)

/* Live combat instance — separate from EnemyDef (the template in enemies.h) */
typedef struct {
    char    name[16];
    int     hp;
    int     maxHp;
    uint8_t attack;
    uint8_t defense;
    uint8_t size;
    uint8_t speed;
    uint8_t intelligence;
    uint8_t perception;
    uint8_t flags;
    uint8_t xpReward;
    uint8_t goldDrop;
    uint8_t lootTableId;
} Enemy;

typedef struct { ActionId type; uint8_t power; } Action;

typedef enum { COMBAT_PHASE_ACTIVE, COMBAT_PHASE_VICTORY } CombatPhase;

typedef struct {
    Enemy       enemy;
    uint8_t     enemyDefId;
    Action      actions[4];
    int         actionCount;
    int         selectedIndex;
    int         isFirstTurn;
    int         skipEnemyAttack;
    CombatPhase phase;
    int         gainedGold;
    uint8_t     gainedDomainXp[14]; /* indexed by DOMAIN_* — XP earned this fight */
    uint8_t     droppedItems[4];
    int         droppedCount;
    PakData     enemyImg;
    uint8_t     fromWorldEnemy; /* 1 if triggered by a WorldEnemy on the map */
    uint8_t     worldEnemyX, worldEnemyY;
    EncounterType encounterType;
    uint32_t    modifiers;     /* ENCOUNTER_MOD_* bitmask */
    char        log[8][28];   /* encounter log — newest at highest index */
    int         logCount;
} CombatState;

extern CombatState combat;

void startCombat(const EnemyDef *def);
void handleCombatInput(int key);
void renderCombat(void);
void returnToTown(void);
