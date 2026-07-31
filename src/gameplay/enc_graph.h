#pragma once
#include <stdint.h>
#include "pak.h"
#include "effects.h"

/* -----------------------------------------------------------------------
 * Encounter state graphs — one pre-defined graph per encounter type.
 * States are graph-local (0..7); which graph applies is determined by the
 * encounter type, so triplet state ids never collide across types.
 * ----------------------------------------------------------------------- */

#define ENC_TYPE_COUNT   5
#define GRAPH_STATES_MAX 8

/* Canonical type indices = ACT_CAT_* bit positions (actions.h) */
#define ENC_IDX_COMBAT        0
#define ENC_IDX_SOCIAL        1
#define ENC_IDX_INVESTIGATION 2
#define ENC_IDX_HUNT          3
#define ENC_IDX_ENVIRONMENTAL 4

/* Canonical combat-graph state ids (must match seed_states.c) */
#define CSTATE_SQUARING  0
#define CSTATE_TRADING   1
#define CSTATE_STAGGERED 2
#define CSTATE_FRENZIED  3
#define CSTATE_BROKEN    4

/* Canonical hunt-graph state ids — the group's morale (seed_states.c) */
#define HSTATE_ORGANIZED 0
#define HSTATE_DISTURBED 1
#define HSTATE_ALERTED   2
#define HSTATE_TERRIFIED 3
#define HSTATE_BROKEN    4
#define HSTATE_PREPARING 5

/* Canonical investigation-graph (case arc) state ids */
#define ISTATE_COLD         0
#define ISTATE_FAMILIARIZE  1
#define ISTATE_CONNECTING   2
#define ISTATE_BREAKTHROUGH 3
#define ISTATE_UNRAVELED    4

/* Canonical social-graph state ids — the old dispositions (seed_states.c) */
#define SSTATE_STRANGER   0
#define SSTATE_SUSPICIOUS 1
#define SSTATE_FEARFUL    2
#define SSTATE_TRUSTING   3
#define SSTATE_HOSTILE    4
#define SSTATE_GREEDY     5

/* StateDef flags */
#define GSTATE_TERMINAL (1<<0) /* entering this state ends the encounter    */
#define GSTATE_SUCCESS  (1<<1) /* terminal counts as a win                  */
#define GSTATE_START    (1<<2) /* graph entry point (first flagged wins)    */

/* 24 bytes — stored in states.dat */
typedef struct {
    char    name[16];
    int8_t  pressure;  /* per-turn flow into the type's stake: drains player
                          HP in combat, moves willingness in social; an
                          enemy's damage stat is added on top of this */
    uint8_t flags;     /* GSTATE_* */
    Effect  onEnter;   /* fires when the state is entered */
    uint8_t cardTint;  /* UI palette hint */
    uint8_t _pad[2];
} StateDef;

typedef char _check_statedef_size[(sizeof(StateDef) == 24) ? 1 : -1];

/* File format (states.dat):
 *   [1]    graph count (ENC_TYPE_COUNT, canonical order)
 *   per graph: [1] state count, [N×24] StateDef array
 */

extern StateDef encGraphs[ENC_TYPE_COUNT][GRAPH_STATES_MAX];
extern uint8_t  encGraphCount[ENC_TYPE_COUNT];

int             loadEncGraphs(PakData data);
int             encTypeIndex(uint8_t encounterCatBit); /* ACT_CAT_* bit -> 0..4; -1 if not a single bit */
const StateDef *encGraphState(int typeIdx, uint8_t stateId);
int             encGraphStart(int typeIdx);            /* GSTATE_START state index, or 0 */
