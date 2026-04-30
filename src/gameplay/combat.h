#pragma once
#include <stdint.h>
#include "enemies.h"
#include "pak.h"
#include "actions.h"

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
    int         gainedXp;
    int         gainedGold;
    int         leveledUp;
    uint8_t     droppedItems[4];
    int         droppedCount;
    PakData     enemyImg;
} CombatState;

extern CombatState combat;

void startCombat(const EnemyDef *def);
void handleCombatInput(int key);
void renderCombat(void);
void returnToTown(void);
