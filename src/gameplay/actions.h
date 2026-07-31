#pragma once
#include <stdint.h>
#include "pak.h"
#include "effects.h"

#define ACTION_MAX 64
#define ACT_TRANSITIONS 3

/* Context flags — conditions required for an action to appear in the draw pool */
#define ACT_CTX_FIRST_TURN   (1<<0)
#define ACT_CTX_ENEMY_WEAPON (1<<1)
#define ACT_CTX_EXECUTABLE   (1<<2)
#define ACT_CTX_CAN_STUN     (1<<3)
#define ACT_CTX_PLAYER_HURT  (1<<4)
#define ACT_CTX_REQUIRES_DARK (1<<5) /* only offered when ENCOUNTER_MOD_DARK is active */
#define ACT_CTX_BLOCKED_HOLY  (1<<6) /* suppressed on ENCOUNTER_MOD_HOLY_GROUND */
#define ACT_CTX_ROUTED        (1<<7) /* hunt only: group must be Terrified/Broken */

/* Action flags — stored in ActionDef.actionFlags */
#define ACT_FLAG_STARTER     (1<<0) /* in base pool without any unlock; data-controlled */
#define ACT_FLAG_ALL_TARGETS (1<<1) /* combat: resolve against every living enemy */

/* Encounter category bitmask — stored in ActionDef.encounterCat.
   An action may belong to multiple categories (bitwise OR them together).
   ACT_CAT_ANY bypasses the filter and is used by non-combat UI panels. */
#define ACT_CAT_COMBAT        (1<<0)
#define ACT_CAT_SOCIAL        (1<<1)
#define ACT_CAT_INVESTIGATION (1<<2)
#define ACT_CAT_HUNT          (1<<3)
#define ACT_CAT_ENVIRONMENTAL (1<<4)
#define ACT_CAT_ANY           0xFF

/* Canonical action IDs — values must match id fields in actions.dat */
typedef enum {
    ACTION_ATTACK   = 0,
    ACTION_STRONG   = 1,
    ACTION_HEAL     = 2,
    ACTION_DEFEND   = 3,
    ACTION_DISARM   = 4,
    ACTION_BACKSTAB = 5,
    ACTION_STUN     = 6,
    ACTION_CALM     = 7,
    ACTION_HIDE     = 8,
    ACTION_EXECUTE  = 9,
    /* Domain: Combat */
    ACTION_INTIMIDATE    = 10,
    ACTION_COUNTER       = 11,
    ACTION_WAR_CRY       = 12,
    ACTION_THREATEN      = 13,
    ACTION_AMBUSH        = 14,
    /* Domain: Trickery */
    ACTION_VANISH        = 15,
    ACTION_POISON_BLADE  = 16,
    ACTION_SET_TRAP      = 17,
    ACTION_DECEIVE       = 18,
    ACTION_PICKPOCKET    = 19,
    ACTION_INSPECT       = 20,
    ACTION_TRACK         = 21,
    /* Domain: Blood */
    ACTION_BLOOD_DRAIN   = 22,
    ACTION_LETHAL_BITE   = 23,
    ACTION_BLOOD_HOWL    = 24,
    ACTION_BLOOD_SURGE   = 25,
    ACTION_BLOOD_SCENT   = 26,
    /* Domain: Charm */
    ACTION_DOMINATE      = 27,
    ACTION_MESMERIZE     = 28,
    ACTION_BRIBE         = 29,
    ACTION_SILVER_TONGUE = 30,
    ACTION_INTERROGATE   = 31,
    /* Environmental */
    ACTION_FEED          = 32,
    ACTION_BLEND_IN      = 33,
    ACTION_RALLY         = 34,
    /* Special */
    ACTION_DEMAND        = 35,
    /* Hunt */
    ACTION_MASSACRE      = 36,
    ACTION_COUNT         = 37
} ActionId;

/* 98 bytes — pak-friendly, no pointers. All resolution lives in the
 * state-graph payload below; there is no damage/power number left. */
typedef struct {
    uint8_t  id;
    uint8_t  contextFlags;
    uint8_t  baseWeight;
    char     name[16];
    char     imgName[8];
    char     desc[32];
    uint8_t  domain;          /* DOMAIN_* constant; 0xFF = unaffiliated */
    uint8_t  encounterCat;    /* ACT_CAT_* bitmask; 0 = universal (always included) */
    uint8_t  actionFlags;     /* ACT_FLAG_* */
    /* State-graph engine */
    Transition transitions[ACT_TRANSITIONS]; /* grouped by type, see tCounts */
    Effect     onPlay;        /* fires every time the card is played */
    Effect     fallback;      /* fires only on a structural whiff (wrong
                                 from-state or target's stateMask blocks it) */
    uint16_t   tCounts;       /* 3 bits per encounter-type index (0..4):
                                 how many of transitions[] belong to that
                                 type's graph, in canonical type order */
} ActionDef;

typedef char _check_actiondef_size[(sizeof(ActionDef) == 98) ? 1 : -1];

/* Per-type triplet count packed in tCounts — typeIdx is ENC_IDX_* (0..4) */
#define ACT_TCOUNT(def, typeIdx) ((uint8_t)(((def)->tCounts >> ((typeIdx) * 3)) & 7u))

extern ActionDef actionDefs[ACTION_MAX];
extern int       actionDefCount;

int              loadActions(PakData data);
const ActionDef *getActionDef(uint8_t id);
/* Triplets of `def` belonging to encounter type `typeIdx` (ENC_IDX_*).
   Points *first at the group's start; returns its count (0 if none). */
int              actionTransitionsFor(const ActionDef *def, int typeIdx,
                                      const Transition **first);
int              buildActionPool(uint8_t out[ACTION_MAX], uint8_t encounterCat);
void             renderActionPanel(const char *title, const uint8_t *ids, int count, int sel);
uint8_t          actionGetDomain(uint8_t id); /* returns DOMAIN_* or 0xFF if unaffiliated */
