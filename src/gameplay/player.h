#pragma once
#include <stdint.h>
#include "skills.h"

#define EQUIP_SLOTS 8

typedef enum {
    PLAYSTYLE_DAYWALKER = 0,
    PLAYSTYLE_KILLER    = 1,
    PLAYSTYLE_ARCANIST  = 2,
    PLAYSTYLE_CHARMER   = 3,
    PLAYSTYLE_COUNT     = 4
} Playstyle;

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
    uint8_t  playstyle;
    uint16_t playstyleXp[PLAYSTYLE_COUNT];
} PlayerData;

extern PlayerData player;

void        playerInit(void);
int         xpToNext(void);
int         awardXp(int amount);
void        enterDeath(void);
const char *playstyleName(int ps);
