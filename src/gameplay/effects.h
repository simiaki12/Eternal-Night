#pragma once
#include <stdint.h>

/* -----------------------------------------------------------------------
 * Effect vocabulary — the single enumerated home for "something happens":
 * action onPlay/fallback effects, state entry effects, status ticks.
 * Never add bespoke effect code paths; extend this enum instead.
 * ----------------------------------------------------------------------- */

typedef enum {
    EFX_NONE          = 0,
    EFX_PROGRESS      = 1, /* +value progress banked on the action's first live edge  */
    EFX_PROGRESS_NEXT = 2, /* next action's progress ×(value/10); 20 = double         */
    EFX_HEAL_HP       = 3, /* restore value HP                                        */
    EFX_DAMAGE_HP     = 4, /* lose value HP (costs, entry damage)                     */
    EFX_EXTRA_ACTION  = 5, /* play another card this turn                             */
    EFX_APPLY_STATUS  = 6, /* value = status id (persistent status layer)             */
    EFX_CLEAR_STATUS  = 7, /* value = status id; 0xFF = clear all negative            */
    EFX_METER         = 8, /* push the encounter type's meter by (int8_t)value:
                              willingness in social, case progress in investigation,
                              nothing in combat (e.g. a big Bribe)                    */
    EFX_KILL          = 9, /* hunt: remove `value` enemies from the group;
                              0xFF = the whole remaining group (Massacre)             */
    EFX_COUNT
} EffectType;

/* 3 bytes — embedded wherever an effect can fire */
typedef struct {
    uint8_t type;   /* EffectType             */
    uint8_t value;  /* argument for the type  */
    uint8_t chance; /* 0-100; 0 = never fires */
} Effect;

/* Widest graph in states.dat. Every graph is addressed by a dense
   from×to matrix, so this bounds both. (enc_graph.h keeps GRAPH_STATES_MAX
   at 8 purely as the width of the hunt escalate table; graphs themselves
   are clamped to GRAPH_STATES on load.) */
#define GRAPH_STATES 6

/* How much progress an action banks on every possible edge of one graph:
   progress[from][to]. A cell of 0 means that route cannot be taken from
   that state at all — which is how "easy to bribe a stranger, hard to
   bribe someone hostile" is expressed. Playing the action banks the cell
   on that edge's pot, then rolls d100; the transition fires on a roll
   under the pot.

   Dense in memory, sparse on disk: actions.dat stores only the matrices
   an action actually uses, selected by ActionDef.graphMask. */
typedef struct {
    uint8_t progress[GRAPH_STATES][GRAPH_STATES];
} TransMatrix;

typedef char _check_transmatrix_size[(sizeof(TransMatrix) == 36) ? 1 : -1];
