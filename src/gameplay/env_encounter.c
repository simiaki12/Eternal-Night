#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "env_encounter.h"
#include "encounter.h"
#include "actions.h"
#include "items.h"
#include "player.h"
#include "domains.h"
#include "game.h"

EnvEncounterDef envEncDefs[ENV_ENC_MAX];
int             envEncDefCount = 0;

/* ----------------------------------------------------------------------- */

int loadEnvEncounters(PakData data) {
    if (!data.data || data.size < 1) return 0;
    const uint8_t *p = (const uint8_t *)data.data;
    uint8_t n = p[0];
    if (n > ENV_ENC_MAX) n = ENV_ENC_MAX;
    if (data.size < 1 + (uint32_t)n * sizeof(EnvEncounterDef)) return 0;
    memcpy(envEncDefs, p + 1, (size_t)n * sizeof(EnvEncounterDef));
    envEncDefCount = n;
    return n;
}

const EnvEncounterDef *envEncGetDef(uint8_t id) {
    for (int i = 0; i < envEncDefCount; i++)
        if (envEncDefs[i].id == id) return &envEncDefs[i];
    return NULL;
}

/* ----------------------------------------------------------------------- */

void envEncounterStart(uint8_t encId, uint32_t mods) {
    const EnvEncounterDef *def = envEncGetDef(encId);
    if (!def || def->stateCount == 0) return;

    encounter.encounterType  = ENCOUNTER_ENVIRONMENTAL;
    encounter.envEncId       = (int)encId;
    encounter.envStateIdx    = (int)def->startState;
    encounter.envProgress    = 0;
    encounter.envTurnInState = 0;
    encounter.phase          = ENCOUNTER_PHASE_ACTIVE;
    encounter.modifiers      = mods;
    encounter.logCount       = 0;
    encounter.logScroll      = 0;
    encounter.selectedIndex  = 0;
    encounter.enemyCount     = 0;

    /* Log entry text for the opening state */
    const EnvStateDef *st = &def->states[def->startState];
    if (st->description[0])
        encounterLog(st->description);

    /* Apply any damage from entering the start state — same clamp + death
       handling as enterState() so the start state behaves identically. */
    if (st->damage) {
        int newHp = (int)player.hp - (int)st->damage;
        player.hp = (uint16_t)(newHp < 0 ? 0 : newHp);
        if (player.hp == 0)
            encounter.phase = ENCOUNTER_PHASE_TIMEOUT;
    }

    generateActions();
    state = STATE_ENCOUNTER;
}

/* ----------------------------------------------------------------------- */

/* Advance to a new state, applying its entry effects. */
static void enterState(const EnvEncounterDef *def, int stateIdx) {
    encounter.envStateIdx    = stateIdx;
    encounter.envTurnInState = 0;

    const EnvStateDef *st = &def->states[stateIdx];
    if (st->description[0])
        encounterLog(st->description);

    if (st->damage) {
        int newHp = (int)player.hp - (int)st->damage;
        player.hp = (uint16_t)(newHp < 0 ? 0 : newHp);
    }

    /* Dying from state damage ends the encounter immediately */
    if (player.hp == 0) {
        encounter.phase = ENCOUNTER_PHASE_TIMEOUT;
        return;
    }

    if (st->flags & ENV_STATE_TERMINAL)
        encounter.phase = ENCOUNTER_PHASE_ACTIVE; /* stays active for finishing touch */
}

/* Resolve a terminal edge: set flag, grant item, end encounter. */
static void resolveEdge(const EnvEdge *edge, const EnvStateDef *st) {
    if (edge->setFlag != 0xFF) worldFlagSet(edge->setFlag);
    if (edge->rewardItem != 0xFF) addItem(edge->rewardItem);

    encounter.phase = (st->flags & ENV_STATE_SUCCESS)
        ? ENCOUNTER_PHASE_VICTORY
        : ENCOUNTER_PHASE_TIMEOUT;
}

/* ----------------------------------------------------------------------- */

/* Environmental encounters don't run the shared graph: an edge either
   matches the action or it doesn't, and firing is certain. Only the first
   match resolves, so later ones are reported as shadowed. */
int envEncPreview(uint8_t actionId, EdgePreview out[ENC_PREVIEW_MAX]) {
    const EnvEncounterDef *def = envEncGetDef((uint8_t)encounter.envEncId);
    if (!def) return 0;

    int si = encounter.envStateIdx;
    if (si < 0 || si >= (int)def->stateCount) return 0;
    const EnvStateDef *st = &def->states[si];

    int n = st->edgeCount > ENV_EDGE_MAX ? ENV_EDGE_MAX : st->edgeCount;
    int count = 0;

    for (int e = 0; e < n && count < ENC_PREVIEW_MAX; e++) {
        const EnvEdge *edge = &st->edges[e];
        int match = 0;
        for (int a = 0; a < ENV_ACTION_MAX; a++)
            if (edge->actionIds[a] == actionId) { match = 1; break; }
        if (!match) continue;

        /* Progress filling or a terminal state resolves the encounter here
           rather than moving on — say so instead of naming a next state. */
        int gain = st->progressGain < 1 ? 1 : st->progressGain;
        int ends = (encounter.envProgress + gain >= (int)def->progressGoal) ||
                   (st->flags & ENV_STATE_TERMINAL);

        out[count].to      = ends ? 0xFF : edge->nextState;
        out[count].verdict = (uint8_t)(count == 0 ? PV_LIVE : PV_SHADOWED);
        out[count].banked  = 0;
        out[count].pot     = 100; /* deterministic */
        count++;
    }
    return count;
}

void envEncounterDoAction(uint8_t actionId) {
    const EnvEncounterDef *def = envEncGetDef((uint8_t)encounter.envEncId);
    if (!def) return;

    int              si = encounter.envStateIdx;
    const EnvStateDef *st = &def->states[si];

    /* Domain XP for playing any action */
    const ActionDef *act = getActionDef(actionId);
    if (act) domainAwardXp(act->domain, 1);

    /* Check if this action fires any edge */
    const EnvEdge *fired = NULL;
    for (int e = 0; e < st->edgeCount && !fired; e++) {
        const EnvEdge *edge = &st->edges[e];
        for (int a = 0; a < ENV_ACTION_MAX; a++) {
            if (edge->actionIds[a] == actionId) { fired = edge; break; }
        }
    }

    if (fired) {
        /* Edge fired — add progress */
        int gain = (int)st->progressGain;
        if (gain < 1) gain = 1;
        encounter.envProgress += gain;
        if (encounter.envProgress > 255) encounter.envProgress = 255;

        /* Progress fills, or terminal state → resolve here */
        if (encounter.envProgress >= (int)def->progressGoal ||
            (st->flags & ENV_STATE_TERMINAL)) {
            resolveEdge(fired, st);
            return;
        }

        /* Edge moves to another state → fresh turn budget, no carry-over */
        if (fired->nextState != 0xFF && fired->nextState < def->stateCount &&
            fired->nextState != (uint8_t)si) {
            enterState(def, fired->nextState);
            if (encounter.phase != ENCOUNTER_PHASE_ACTIVE) return;
            generateActions();
            return;
        }
        /* Stayed in this state — falls through to turn accounting below */
    } else {
        encounterLog("The action has no effect here.");
    }

    /* One turn spent in the current state (a miss, or a hit with no transition).
       Counted exactly once; check the timeout against this same state. */
    encounter.envTurnInState++;
    if (st->turnBudget > 0 && encounter.envTurnInState >= (int)st->turnBudget) {
        if (st->timeoutNext != 0xFF && st->timeoutNext < def->stateCount) {
            enterState(def, st->timeoutNext);
            if (encounter.phase != ENCOUNTER_PHASE_ACTIVE) return;
        }
    }
    generateActions();
}
