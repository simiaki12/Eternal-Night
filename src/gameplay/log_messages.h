#pragma once
#include <stdint.h>
#include "pak.h"

#define LOG_MSG_MAX 128

typedef enum {
    LOGTRIG_ACTION        = 0, /* after the player's action resolves  */
    LOGTRIG_ENEMY_KILLED  = 1, /* when any enemy drops to 0 HP        */
    LOGTRIG_COMBAT_START  = 2, /* on startCombat / startEncounter     */
    LOGTRIG_TURN_END      = 3, /* after all counter-attacks resolve   */
    LOGTRIG_PLAYER_HURT   = 4, /* when player takes damage this turn  */
    LOGTRIG_PLAYER_LOW_HP = 5, /* when player HP falls below 33%      */
} LogTrigger;

typedef enum {
    ENEMYCOND_NONE       = 0,
    ENEMYCOND_SIZE_GTE   = 1, /* any alive enemy size >= val         */
    ENEMYCOND_HP_LTE_PCT = 2, /* any alive enemy HP% <= val          */
    ENEMYCOND_HP_GTE_PCT = 3, /* any alive enemy HP% >= val          */
    ENEMYCOND_HAS_FLAG   = 4, /* any alive enemy has flag bit val    */
    ENEMYCOND_COUNT_GTE  = 5, /* alive enemy count >= val            */
} EnemyCond;

typedef enum {
    LOGFX_NONE          = 0,
    LOGFX_ADD_MOD       = 1, /* add ENCOUNTER_MOD_* bit             */
    LOGFX_REMOVE_MOD    = 2, /* remove a modifier                   */
    LOGFX_ENEMY_FLEE    = 3, /* remove target enemy, no loot        */
    LOGFX_ENEMY_STUN    = 4, /* target skips counter-attack         */
    LOGFX_ENEMY_ATK_DOWN= 5, /* reduce target attack by val         */
    LOGFX_HEAL_PLAYER   = 6, /* restore val HP                      */
    LOGFX_DAMAGE_PLAYER = 7, /* deal val damage to player           */
} LogFxType;

typedef enum {
    LOGFX_TARGET_FOCUSED = 0, /* currently targeted enemy            */
    LOGFX_TARGET_WEAKEST = 1, /* lowest current HP among alive       */
    LOGFX_TARGET_BIGGEST = 2, /* highest size stat                   */
    LOGFX_TARGET_RANDOM  = 3, /* random alive enemy                  */
    LOGFX_TARGET_ALL     = 4, /* all alive enemies                   */
} LogFxTarget;

/* 40 bytes — pak-friendly, no pointers */
typedef struct {
    char    text[28];
    uint8_t trigger;       /* LogTrigger                            */
    uint8_t actionId;      /* ACTION_* or 0xFF = any               */
    uint8_t enemyDefId;    /* enemy def index or 0xFF = any        */
    uint8_t encounterType; /* ENCOUNTER_* value or 0xFF = any      */
    uint8_t modRequired;   /* ENCOUNTER_MOD_* bits that must be set, 0 = any */
    uint8_t chance;        /* 0-100                                 */
    uint8_t enemyCond;     /* EnemyCond                             */
    uint8_t enemyCondVal;  /* threshold / flag value for enemyCond */
    uint8_t effectType;    /* LogFxType                             */
    uint8_t effectTarget;  /* LogFxTarget                           */
    uint8_t effectValue;   /* argument for the effect               */
    uint8_t _pad;
} LogMessage; /* 40 bytes */

typedef char _check_logmsg_size[(sizeof(LogMessage) == 40) ? 1 : -1];

extern LogMessage logMessages[LOG_MSG_MAX];
extern int        logMessageCount;

int  loadLogMessages(PakData data);
void fireLogMessages(LogTrigger trigger, uint8_t actionId, int slotHint);
