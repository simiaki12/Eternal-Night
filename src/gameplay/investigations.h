#pragma once
#include <stdint.h>
#include "pak.h"

#define CLUE_DEF_MAX     64
#define INV_DEF_MAX      32
#define INV_CLUE_SLOTS    8   /* max clues per investigation */

/* ClueDef flags */
#define CLUE_FLAG_KEY        (1<<0)  /* required for investigation success */
#define CLUE_FLAG_MISLEADING (1<<1)  /* red herring — points toward wrong conclusion */
#define CLUE_FLAG_LINGERING  (1<<2)  /* shown in journal even on partial/failed investigation */

/* InvestigationDef flags */
#define INV_FLAG_STAGED      (1<<0)  /* scene contains misleading clues */
#define INV_FLAG_RETURNABLE  (1<<1)  /* player can re-enter after abandoning */

/* Pressure types — why the turn limit exists */
#define INV_PRESSURE_WEATHER  0
#define INV_PRESSURE_CROWD    1
#define INV_PRESSURE_DARKNESS 2
#define INV_PRESSURE_DECAY    3

/* 80 bytes */
typedef struct {
    uint8_t id;
    char    text[32];           /* atmospheric log text when found */
    char    itemHint[32];       /* hint shown when required item is missing */
    uint8_t difficulty;         /* 0–255: lower = easier to find */
    uint8_t domain;             /* DOMAIN_* affinity; bonus scales with player level */
    uint8_t actionAffinity[4]; /* action IDs with the best reveal odds */
    uint8_t requiredItem;       /* hard gate: 0xFF = no item needed */
    uint8_t invalidates;        /* clue ID this grays out on discovery; 0xFF = none */
    uint8_t flags;              /* CLUE_FLAG_* */
    uint8_t minCaseState;       /* only findable once the case has reached this
                                   arc state (0 = always); the "you notice more
                                   as the case deepens" gate */
    uint8_t _pad[5];
} ClueDef;

typedef char _check_cluedef[(sizeof(ClueDef) == 80) ? 1 : -1];

/* 80 bytes */
typedef struct {
    uint8_t id;
    char    name[24];           /* journal group heading */
    char    pressureText[36];   /* fictional reason turns are limited */
    uint8_t clueIds[INV_CLUE_SLOTS]; /* global clue IDs in this scene */
    uint8_t clueCount;
    uint8_t turnLimit;
    uint8_t keyMask;            /* bitmask: which positions in clueIds[] are mandatory */
    uint8_t rewardItem;         /* 0xFF = none */
    uint8_t rewardQuest;        /* 0xFF = none */
    uint8_t pressureType;       /* INV_PRESSURE_* */
    uint8_t flags;              /* INV_FLAG_* */
    uint8_t caseId;             /* case this scene belongs to; 0xFF = standalone */
    uint8_t _pad[3];
} InvestigationDef;

typedef char _check_invdef[(sizeof(InvestigationDef) == 80) ? 1 : -1];

extern ClueDef         clueDefs[CLUE_DEF_MAX];
extern int             clueDefCount;
extern InvestigationDef invDefs[INV_DEF_MAX];
extern int             invDefCount;

int loadClues(PakData data);
int loadInvestigations(PakData data);

const ClueDef         *clueGetDef(uint8_t id);
const InvestigationDef *invGetDef(uint8_t id);

/* Persistent clue state — backed by PlayerData.clueFound bitmask */
int  clueIsFound(uint8_t clueId);
void clueMarkFound(uint8_t clueId);

/* Returns 1 if any currently-found clue invalidates this clue ID */
int  clueIsInvalidated(uint8_t clueId);

/* Begin an investigation encounter */
void investigationStart(uint8_t invId, uint32_t mods);

/* Called from encounter.c when the player plays an investigation action */
void investigationDoAction(uint8_t actionId);
