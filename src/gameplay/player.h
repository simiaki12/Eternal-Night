#pragma once
#include <stdint.h>
#include "domains.h"
#include "encounter.h"
#include "npcs.h"

#define EQUIP_SLOTS  8
#define FAVOUR_MAX   8

typedef enum {
    SLOT_WEAPON      = 0,
    SLOT_OUTFIT      = 1,  /* main armour or role disguise */
    SLOT_UNDER       = 2,  /* undergarment — stacks with outfit */
    SLOT_ACCESSORY_1 = 3,
    SLOT_ACCESSORY_2 = 4,
    SLOT_RELIC       = 5,
    EQUIP_SLOT_COUNT = 6
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
    /* Social encounter state */
    NpcSaveState             npcStates[NPC_DEF_MAX];
    SocialEncounterSaveState seStates[SE_DEF_MAX];
    /* Action preferences */
    uint8_t favouredActions[FAVOUR_MAX];
    uint8_t suppressedActions[FAVOUR_MAX];
    uint8_t favourCount;
    uint8_t suppressCount;
    /* Investigation clue state — one bit per clue ID (up to 64 clues) */
    uint8_t clueFound[8];
} PlayerData;

extern PlayerData player;

void playerInit(void);
void enterDeath(void);
void playerRemoveActionFromQueues(uint8_t id);
int  favourSlots(void);
int  isActionFavoured(uint8_t id);
int  isActionSuppressed(uint8_t id);
void toggleFavoured(uint8_t id);
void toggleSuppressed(uint8_t id);
