#pragma once
#include <stdint.h>
#include "domains.h"

#define EQUIP_SLOTS 8

typedef enum {
    SLOT_WEAPON = 0,
    SLOT_ARMOR  = 1,
    EQUIP_SLOT_COUNT = 2
} EquipSlot;

typedef struct {
    uint16_t          maxHp;
    uint8_t           attack;
    uint8_t           defense;
    uint8_t           equipped[EQUIP_SLOTS]; /* indexed by EquipSlot; 0xFF = empty */
    /* Character stats */
    uint8_t           stamina;
    uint8_t           intelligence;
    uint8_t           perception;
    uint8_t           charisma;
    uint8_t           agility;
    /* Resources */
    uint16_t          hp;
    uint16_t          gold;
    /* Domain progression — replaces level/xp/skillPoints/skills/playstyle */
    PlayerDomainState domains[DOMAIN_COUNT];
    uint8_t           focusedDomain; /* DOMAIN_* or DOMAIN_NONE if no domain is in focus */
} PlayerData;

extern PlayerData player;

void playerInit(void);
void enterDeath(void);
