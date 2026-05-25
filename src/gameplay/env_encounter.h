#pragma once
#include <stdint.h>
#include "pak.h"

#define ENV_ENC_MAX      32
#define ENV_STATE_MAX     8   /* max states per encounter (4 typical, 8 for story) */
#define ENV_EDGE_MAX      4   /* max outgoing edges per state */
#define ENV_ACTION_MAX    3   /* max action IDs that trigger a single edge */

/* EnvStateDef flags */
#define ENV_STATE_TERMINAL (1<<0)  /* no more player turns; show finishing touch */
#define ENV_STATE_SUCCESS  (1<<1)  /* good outcome if encounter ends here */

/* EnvEncounterDef flags */
#define ENV_FLAG_REPEATABLE (1<<0)  /* can be entered again after resolution */
#define ENV_FLAG_STORY      (1<<1)  /* important encounter; allows up to 8 states */

/* TODO: reconsider the graph representation (edge list per state with fixed arrays)
 * once we have more encounters authored. A flat transition table or a separate
 * edge dat file may be cleaner if edge counts grow uneven across states. */

/* 8 bytes
 * An outgoing edge from a state. Playing any action in actionIds[] triggers this
 * edge: progress is added and the encounter moves to nextState.
 * For terminal-state finishing touches, nextState == 0xFF (resolve here). */
typedef struct {
    uint8_t actionIds[ENV_ACTION_MAX]; /* action IDs that fire this edge; 0xFF = unused */
    uint8_t nextState;                 /* destination state index; 0xFF = terminal resolve */
    uint8_t setFlag;                   /* world flag to set on resolve (0xFF = none) */
    uint8_t rewardItem;                /* item ID to grant on resolve (0xFF = none) */
    uint8_t _pad[2];
} EnvEdge;

typedef char _check_env_edge[(sizeof(EnvEdge) == 8) ? 1 : -1];

/* 64 bytes */
typedef struct {
    char    description[24];           /* log text shown when entering this state */
    EnvEdge edges[ENV_EDGE_MAX];       /* outgoing edges (4 × 8 = 32 bytes) */
    uint8_t edgeCount;
    uint8_t turnBudget;                /* turns before timeoutNext fires; 0 = no timeout */
    uint8_t timeoutNext;               /* state to advance to on timeout; 0xFF = stay */
    uint8_t damage;                    /* HP damage dealt on entering this state; 0 = none */
    uint8_t progressGain;             /* progress added when an edge fires from this state */
    uint8_t flags;                     /* ENV_STATE_* */
    uint8_t _pad[2];
} EnvStateDef;

typedef char _check_env_state[(sizeof(EnvStateDef) == 64) ? 1 : -1];

/* 32 + 8×64 = 544 bytes */
typedef struct {
    uint8_t     id;
    char        name[24];
    uint8_t     progressGoal;          /* progress needed to end encounter */
    uint8_t     stateCount;            /* number of valid states (1–ENV_STATE_MAX) */
    uint8_t     startState;            /* index of the initial state */
    uint8_t     flags;                 /* ENV_FLAG_* */
    uint8_t     _pad[3];
    EnvStateDef states[ENV_STATE_MAX]; /* embedded state table */
} EnvEncounterDef;

typedef char _check_env_enc[(sizeof(EnvEncounterDef) == 544) ? 1 : -1];

extern EnvEncounterDef envEncDefs[ENV_ENC_MAX];
extern int             envEncDefCount;

int                     loadEnvEncounters(PakData data);
const EnvEncounterDef  *envEncGetDef(uint8_t id);

/* Begin an environmental encounter */
void envEncounterStart(uint8_t encId, uint32_t mods);

/* Called from encounter.c when the player plays an action */
void envEncounterDoAction(uint8_t actionId);
