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

/* Must match GRAPH_STATES_MAX in enc_graph.h (kept separate to avoid a
   circular include). */
#define TRANSITION_SOURCES 8

/* A transition names ONE destination state and how much progress the action
   banks toward it from each possible source state — so a single entry tunes
   "easy to bribe a stranger, hard to bribe someone hostile". A source whose
   progress is 0 cannot take this route at all; an entry with no non-zero
   source is an unused slot. Playing the action banks progress[current] on
   the edge, then rolls d100 — the transition fires on a roll under the pot. */
typedef struct {
    uint8_t to;
    uint8_t progress[TRANSITION_SOURCES];
} Transition;
