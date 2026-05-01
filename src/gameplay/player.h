#pragma once
#include <stdint.h>
#include "skills.h"

#define EQUIP_SLOTS 8   /* total equipment array size; expand slot types as needed */

typedef enum {
    SLOT_WEAPON = 0,
    SLOT_ARMOR  = 1,
    EQUIP_SLOT_COUNT = 2   /* number of defined slot types */
} EquipSlot;

typedef struct {
    uint16_t maxHp;
    uint8_t  attack;
    uint8_t  defense;
    uint8_t  equipped[EQUIP_SLOTS]; /* indexed by EquipSlot; 0xFF = empty */
    uint8_t  skills[SKILL_MAX];
    uint8_t  level;
    uint16_t xp;
    uint8_t  skillPoints;
    uint16_t hp;
    uint8_t  intelligence;
    uint8_t  perception;
    uint8_t  stamina;
    uint16_t gold;
} PlayerData;

extern PlayerData player;

void playerInit(void);
int  xpToNext(void);      /* XP needed to reach the next level */
int  awardXp(int amount);  /* returns number of levels gained */
void enterDeath(void);     /* lose XP progress, restore HP, go to STATE_DEATH */
