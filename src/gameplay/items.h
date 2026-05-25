#pragma once
#include <stdint.h>
#include "pak.h"

typedef enum {
    ITEM_WEAPON      = 0,
    ITEM_OUTFIT      = 1,  /* main armour or role disguise */
    ITEM_CONSUMABLE  = 2,
    ITEM_UNDERGARMENT= 3,  /* worn beneath outfit, stacks */
    ITEM_ACCESSORY   = 4,  /* ring/watch/badge/pendant — two slots available */
    ITEM_RELIC       = 5,  /* story-gated powerful item */
} ItemType;

#define ITEM_FLAG_HEAL        0x01
#define ITEM_FLAG_BUFF_ATTACK 0x02

#define ITEM_UNEQUIPPED 0xFF

/* 64 bytes — pak-friendly, no pointers */
typedef struct {
    char     name[16];
    uint8_t  type;
    int8_t   attackBonus;
    int8_t   defenseBonus;
    int8_t   intelligenceBonus;
    int8_t   perceptionBonus;
    int8_t   staminaBonus;
    int8_t   hpBonus;
    uint8_t  flags;
    uint16_t price;
    char     description[24];
    uint8_t  actions[4];  /* action IDs granted by this item; 0xFF = none */
    uint8_t  _pad[10];
} ItemDef;

typedef struct {
    uint8_t items[16];
    uint8_t count;
    int8_t  selected;
} Inventory;

extern ItemDef   itemDefs[64];
extern int       itemDefCount;
extern Inventory inventory;

int         loadItems(PakData data);
const ItemDef *itemGetDef(uint8_t id);
const char *itemName(uint8_t id);
const char *itemDesc(uint8_t id);
typedef enum {
    STAT_ATK = 0,
    STAT_DEF,
    STAT_MHP,
    STAT_INT,
    STAT_PER,
    STAT_STA,
    STAT_COUNT
} StatType;

int         itemSlot(const ItemDef *d);            /* EquipSlot index, or -1 if not equippable */
int         isEquipped(uint8_t id);
int         playerHasItem(uint8_t id);            /* 1 if in inventory or equipped */
int         getStat(StatType stat);                /* base + all equipped bonuses */
int         getStatPreview(StatType stat, uint8_t itemId); /* getStat after equipping/toggling itemId */

/* Convenience wrappers */
int         getAttack(void);
int         getDefense(void);
int         getMaxHp(void);
int         getIntelligence(void);
int         getPerception(void);
int         getStamina(void);

int         addItem(uint8_t id);
void        useOrEquipItem(int index);
void        removeItem(int index);
