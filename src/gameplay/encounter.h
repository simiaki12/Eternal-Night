#pragma once
#include <stdint.h>
#include "enemies.h"
#include "pak.h"
#include "actions.h"

/* -----------------------------------------------------------------------
 * Social encounter data model
 * ----------------------------------------------------------------------- */

#define SE_DEF_MAX 64

/* NPC personality flags — stored in NpcDef.tags; permanent traits that
   override social action tiers during encounter resolution */
#define NPC_TAG_FEARFUL   (1<<0)  /* upgrades actions effective in FEARFUL from neutral to effective */
#define NPC_TAG_NOBLE     (1<<1)  /* downgrades actions effective in GREEDY to backfire */
#define NPC_TAG_CRIMINAL  (1<<2)  /* upgrades actions effective in SUSPICIOUS from neutral to effective */
#define NPC_TAG_CONNECTED (1<<3)  /* critical failure alerts nearby NPCs */
#define NPC_TAG_FEARLESS  (1<<4)  /* downgrades actions effective in FEARFUL from effective to neutral */

/* NPC live disposition during a social encounter — dynamic, changes per action */
typedef enum {
    DISP_STRANGER   = 0,
    DISP_SUSPICIOUS = 1,
    DISP_FEARFUL    = 2,
    DISP_TRUSTING   = 3,
    DISP_HOSTILE    = 4,
    DISP_GREEDY     = 5,
    DISP_COUNT      = 6,
    DISP_NONE       = 0xFF,
} Disposition;

/* Bitmasks for ActionDef.effective_disp / backfire_disp are in actions.h */

/* NPC counter-move bitmask — stored in NpcDef.move_mask */
#define NPC_MOVE_DISMISS  (1<<0)  /* lower disposition */
#define NPC_MOVE_THREATEN (1<<1)  /* lower disposition + add pressure clock */
#define NPC_MOVE_LEAVE    (1<<2)  /* exit if patience expires */

/* SocialEncounterDef flags */
#define SE_FLAG_ONE_SHOT      (1<<0)  /* cannot be retried after completion or failure */
#define SE_FLAG_DISP_OVERRIDE (1<<1)  /* use disp_start instead of deriving from standing */
#define SE_FLAG_HIDDEN        (1<<2)  /* unavailable until triggered by quest/event */

/* Per-encounter save state */
typedef enum {
    SE_STATE_NEVER_MET = 0,
    SE_STATE_PARTIAL,
    SE_STATE_COMPLETED,
    SE_STATE_FAILED,
    SE_STATE_LOCKED,    /* one-shot that was completed or permanently closed */
} SeState;

/* 8 bytes — stored in social_encounters.dat
 * flags is first so the u16 sits at a natural alignment boundary */
typedef struct {
    uint16_t flags;          /* SE_FLAG_* bitmask */
    uint8_t  id;
    uint8_t  npc_id;         /* index into npcDefs[] */
    uint8_t  reward_partial; /* index into reward table (TBD) */
    uint8_t  reward_full;    /* index into reward table (TBD) */
    uint8_t  disp_start;     /* starting disposition override; only used with SE_FLAG_DISP_OVERRIDE */
    uint8_t  _pad;
} SocialEncounterDef;

typedef char _check_sedef_size[(sizeof(SocialEncounterDef) == 8) ? 1 : -1];

/* Dynamic save states */
typedef struct { uint8_t standing; } NpcSaveState;
typedef struct { uint8_t state;    } SocialEncounterSaveState; /* SeState */

/* File format (social_encounters.dat):
 *   [1]      encounter count
 *   [N×8]    SocialEncounterDef array
 */

extern SocialEncounterDef seDefs[SE_DEF_MAX];
extern int                seDefCount;

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

/* Live encounter instance — separate from EnemyDef (the template in enemies.h) */
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
    uint8_t disposition;  /* Disposition enum; only meaningful in ENCOUNTER_SOCIAL */
} Enemy;

typedef struct { ActionId type; uint8_t power; } Action;

typedef enum { ENCOUNTER_PHASE_ACTIVE, ENCOUNTER_PHASE_VICTORY, ENCOUNTER_PHASE_TIMEOUT } EncounterPhase;

typedef enum {
    SOCIAL_OUTCOME_NONE   = 0,
    SOCIAL_OUTCOME_WIN    = 1, /* willingness reached high threshold */
    SOCIAL_OUTCOME_DEMAND = 2, /* player forced an end with Demand   */
    SOCIAL_OUTCOME_LOSS   = 3, /* NPC walked away                    */
} SocialOutcome;

#define ENCOUNTER_MAX_ENEMIES 3

typedef struct {
    Enemy       enemies[ENCOUNTER_MAX_ENEMIES];
    int         enemyCount;
    int         targetIndex;
    uint8_t     enemyDefIds[ENCOUNTER_MAX_ENEMIES];
    Action      actions[4];
    int         actionCount;
    int         selectedIndex;
    int         isFirstTurn;
    int         skipEnemyAttack;
    EncounterPhase phase;
    int         gainedGold;
    uint8_t     gainedDomainXp[14]; /* indexed by DOMAIN_* — XP earned this fight */
    uint8_t     droppedItems[4];
    int         droppedCount;
    PakData     enemyImgs[ENCOUNTER_MAX_ENEMIES];
    uint8_t     fromWorldEnemy[ENCOUNTER_MAX_ENEMIES];
    uint8_t     worldEnemyX[ENCOUNTER_MAX_ENEMIES];
    uint8_t     worldEnemyY[ENCOUNTER_MAX_ENEMIES];
    EncounterType encounterType;
    uint32_t    modifiers;     /* ENCOUNTER_MOD_* bitmask */
    char        log[128][28]; /* encounter log — newest at highest index */
    int         logCount;
    int         logScroll;   /* entries from bottom; 0 = newest visible */
    int         socialNpcId;          /* npc_id during ENCOUNTER_SOCIAL; -1 otherwise */
    SocialOutcome socialOutcome;      /* how the social encounter ended               */
    int         socialEndWillingness; /* final willingness (0-100) when it ended      */
    /* Investigation state */
    int         invId;                /* active InvestigationDef ID; -1 if none */
    int         invTurns;             /* turns remaining */
    uint8_t     invFoundMask;         /* bitmask of locally-found clue positions */
    int         invSuccess;           /* 1 if all key clues were found */
} EncounterState;

extern EncounterState encounter;

void generateActions(void);
void encounterStartCombat(const EnemyDef *def);
void encounterAddEnemy(const EnemyDef *def, uint8_t wx, uint8_t wy);
void encounterStart(EncounterType type, const EnemyDef *def, uint32_t mods);
void handleEncounterInput(int key);
void renderEncounter(void);
void returnToTown(void);
void encounterLog(const char *msg); /* public log push — for use from log_messages.c */

int  loadSocialEncounters(PakData data);
int  loadClues(PakData data);
int  loadInvestigations(PakData data);
void socialNewGame(void);              /* copy base standings into PlayerData; reset encounter states */
int  socialFindActive(uint8_t npc_id); /* first non-locked, non-hidden SE for this NPC; -1 if none */
void socialEncounterStart(int se_idx); /* begin a social encounter; transitions to STATE_ENCOUNTER */
