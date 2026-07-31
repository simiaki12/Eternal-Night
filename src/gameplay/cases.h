#pragma once
#include <stdint.h>
#include "pak.h"

/* -----------------------------------------------------------------------
 * Cases — investigation's gimmick. Where every other encounter type keeps
 * its graph inside one encounter, a case's graph position lives ABOVE the
 * encounters and persists in the save: scenes are where you push it.
 *
 *   Cold Trail -> Familiarizing -> Connecting -> Breakthrough -> Unraveled
 *
 * Clues carry a minCaseState, so a scene reveals different material as the
 * case deepens — the bloodstain means nothing until you are Connecting.
 * Leaving a scene banks case progress; only the scene is lost on failure.
 * ----------------------------------------------------------------------- */

#define CASE_DEF_MAX  12
#define CASE_POT_MAX   6

/* CaseDef flags */
#define CASE_FLAG_HIDDEN (1<<0) /* not shown in the journal until opened */

/* 32 bytes — stored in cases.dat */
typedef struct {
    uint8_t id;
    char    name[24];
    uint8_t startState;  /* usually ISTATE_COLD */
    uint8_t rewardQuest; /* RESERVED — not yet wired; 0xFF = none        */
    uint8_t setFlag;     /* world flag set when Unraveled; 0xFF = none  */
    uint8_t flags;       /* CASE_FLAG_*                                 */
    uint8_t _pad[3];
} CaseDef;

typedef char _check_casedef_size[(sizeof(CaseDef) == 32) ? 1 : -1];

/* Persistent per-case graph position and banked edge progress (13 bytes) */
typedef struct {
    uint8_t state;
    uint8_t potKey[CASE_POT_MAX];
    uint8_t potVal[CASE_POT_MAX];
} CaseSaveState;

extern CaseDef caseDefs[CASE_DEF_MAX];
extern int     caseDefCount;

int            loadCases(PakData data);
const CaseDef *caseGetDef(uint8_t id);
void           casesNewGame(void);      /* reset every case to its start state */

uint8_t caseGetState(uint8_t caseId);   /* current arc state; 0 if unknown */
int     caseIsComplete(uint8_t caseId);

/* Move the case's persisted graph position into encounter slot 0 so the
   shared resolver can drive it, and write it back afterwards. */
void caseLoadIntoSlot(uint8_t caseId);
void caseStoreFromSlot(uint8_t caseId);
