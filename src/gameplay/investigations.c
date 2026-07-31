#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include "investigations.h"
#include "encounter.h"
#include "actions.h"
#include "items.h"
#include "player.h"
#include "domains.h"
#include "game.h"
#include "cases.h"
#include "enc_graph.h"
#include "statuses.h"
#include <stdio.h>

ClueDef         clueDefs[CLUE_DEF_MAX];
int             clueDefCount = 0;
InvestigationDef invDefs[INV_DEF_MAX];
int             invDefCount  = 0;

/* ----------------------------------------------------------------------- */

int loadClues(PakData data) {
    if (!data.data || data.size < 1) return 0;
    const uint8_t *p = (const uint8_t *)data.data;
    uint8_t n = p[0];
    if (n > CLUE_DEF_MAX) n = CLUE_DEF_MAX;
    if (data.size < 1 + (uint32_t)n * sizeof(ClueDef)) return 0;
    memcpy(clueDefs, p + 1, (size_t)n * sizeof(ClueDef));
    clueDefCount = n;
    return n;
}

int loadInvestigations(PakData data) {
    if (!data.data || data.size < 1) return 0;
    const uint8_t *p = (const uint8_t *)data.data;
    uint8_t n = p[0];
    if (n > INV_DEF_MAX) n = INV_DEF_MAX;
    if (data.size < 1 + (uint32_t)n * sizeof(InvestigationDef)) return 0;
    memcpy(invDefs, p + 1, (size_t)n * sizeof(InvestigationDef));
    invDefCount = n;
    return n;
}

const ClueDef *clueGetDef(uint8_t id) {
    for (int i = 0; i < clueDefCount; i++)
        if (clueDefs[i].id == id) return &clueDefs[i];
    return NULL;
}

const InvestigationDef *invGetDef(uint8_t id) {
    for (int i = 0; i < invDefCount; i++)
        if (invDefs[i].id == id) return &invDefs[i];
    return NULL;
}

/* ----------------------------------------------------------------------- */
/* Clue found state — PlayerData.clueFound is an 8-byte bitmask (64 clues) */

int clueIsFound(uint8_t clueId) {
    if (clueId >= CLUE_DEF_MAX) return 0;
    return (player.clueFound[clueId / 8] >> (clueId % 8)) & 1;
}

void clueMarkFound(uint8_t clueId) {
    if (clueId >= CLUE_DEF_MAX) return;
    player.clueFound[clueId / 8] |= (uint8_t)(1 << (clueId % 8));
}

int clueIsInvalidated(uint8_t clueId) {
    for (int i = 0; i < clueDefCount; i++) {
        if (clueDefs[i].invalidates == clueId && clueIsFound(clueDefs[i].id))
            return 1;
    }
    return 0;
}

/* ----------------------------------------------------------------------- */

void investigationStart(uint8_t invId, uint32_t mods) {
    const InvestigationDef *inv = invGetDef(invId);
    if (!inv) return;

    encounter.encounterType  = ENCOUNTER_INVESTIGATION;
    encounter.invId          = (int)invId;
    encounter.invTurns       = (int)inv->turnLimit;
    encounter.invSuccess     = 0;
    encounter.phase          = ENCOUNTER_PHASE_ACTIVE;
    encounter.modifiers      = mods;
    encounter.logCount       = 0;
    encounter.logScroll      = 0;
    encounter.selectedIndex  = 0;
    encounter.enemyCount     = 0;
    encounter.progressNextMult   = 10;
    encounter.extraActionPending = 0;

    /* The scene is a window onto a case: pull the case's persistent arc
       position into slot 0 so the shared resolver can move it. */
    encounter.invCaseId = inv->caseId;
    if (inv->caseId != 0xFF) {
        caseLoadIntoSlot(inv->caseId);
        const CaseDef  *cd = caseGetDef(inv->caseId);
        const StateDef *st = encGraphState(ENC_IDX_INVESTIGATION,
                                           encounter.enemyState[0]);
        if (cd && st) {
            char msg[28];
            snprintf(msg, sizeof(msg), "%.14s: %.11s", cd->name, st->name);
            encounterLog(msg);
        }
    } else {
        encounter.enemyState[0] = (uint8_t)encGraphStart(ENC_IDX_INVESTIGATION);
        encounter.potCount[0]   = 0;
    }

    /* Restore already-found clues from persistent state */
    encounter.invFoundMask = 0;
    for (int i = 0; i < inv->clueCount; i++)
        if (clueIsFound(inv->clueIds[i]))
            encounter.invFoundMask |= (uint8_t)(1 << i);

    /* Generate first action hand */
    generateActions();
    state = STATE_ENCOUNTER;
}

/* Case reached its terminal state — bank the payoff once. */
static void caseResolve(uint8_t caseId) {
    const CaseDef *cd = caseGetDef(caseId);
    if (!cd) return;
    /* rewardQuest is reserved — quests are driven by world flags and the
       questOn* hooks, so a solved case signals through its flag. */
    if (cd->setFlag != 0xFF) worldFlagSet(cd->setFlag);
    encounterLog("The case comes together.");
}

/* ----------------------------------------------------------------------- */

void investigationDoAction(uint8_t actionId) {
    const InvestigationDef *inv = invGetDef((uint8_t)encounter.invId);
    if (!inv) return;
    const ActionDef *act = getActionDef(actionId);

    /* Push the case arc — the scene is only where the work happens. A whiff
       here is an approach that tells you nothing about the bigger picture. */
    if (encounter.invCaseId != 0xFF) {
        if (encGraphResolve(0, ENC_IDX_INVESTIGATION, act, 0xFF, 100,
                            applyEffect) == GRAPH_FIRED) {
            caseStoreFromSlot(encounter.invCaseId);
            const StateDef *ns = encGraphState(ENC_IDX_INVESTIGATION,
                                               encounter.enemyState[0]);
            if (ns && (ns->flags & GSTATE_TERMINAL)) {
                caseResolve(encounter.invCaseId);
                encounter.invSuccess = 1;
                encounter.phase      = ENCOUNTER_PHASE_VICTORY;
                return;
            }
        }
        caseStoreFromSlot(encounter.invCaseId);
    }

    /* Build weighted candidate list of unfound clues */
    typedef struct { int localIdx; int weight; } Candidate;
    Candidate candidates[INV_CLUE_SLOTS];
    int candCount = 0;
    uint8_t arc = (encounter.invCaseId != 0xFF)
                ? encounter.enemyState[0] : 0xFF;

    for (int i = 0; i < inv->clueCount; i++) {
        if (encounter.invFoundMask & (1 << i)) continue;
        const ClueDef *c = clueGetDef(inv->clueIds[i]);
        if (!c) continue;
        /* Material you are not yet ready to see */
        if (arc != 0xFF && c->minCaseState > arc) continue;

        int w = 200 - (int)c->difficulty;
        for (int j = 0; j < 4; j++)
            if (c->actionAffinity[j] == actionId) { w += 60; break; }
        if (act && c->domain != 0xFF && (uint8_t)act->domain == c->domain)
            w += (int)player.domains[c->domain].level * 4;
        if (w < 1) w = 1;

        candidates[candCount].localIdx = i;
        candidates[candCount].weight   = w;
        candCount++;
    }

    if (candCount == 0) {
        encounterLog("Nothing more here - for now.");
        encounter.invTurns--;
        if (encounter.invTurns <= 0) encounter.phase = ENCOUNTER_PHASE_TIMEOUT;
        return;
    }

    /* Weighted pick */
    int total = 0;
    for (int i = 0; i < candCount; i++) total += candidates[i].weight;
    int r = rand() % total, acc = 0, target = -1;
    for (int i = 0; i < candCount; i++) {
        acc += candidates[i].weight;
        if (r < acc) { target = candidates[i].localIdx; break; }
    }
    if (target < 0) target = candidates[0].localIdx;

    const ClueDef *c = clueGetDef(inv->clueIds[target]);

    /* Discovery roll: success if rand < (255 - difficulty + bonuses) */
    {
        int bonus = 0;
        for (int j = 0; j < 4; j++)
            if (c->actionAffinity[j] == actionId) { bonus += 50; break; }
        if (act && c->domain != 0xFF && (uint8_t)act->domain == c->domain)
            bonus += (int)player.domains[c->domain].level * 3;

        int threshold = (255 - (int)c->difficulty) + bonus;
        if (threshold > 240) threshold = 240;
        if (threshold < 10)  threshold = 10;

        if (rand() % 256 < threshold) {
            encounter.invFoundMask |= (uint8_t)(1 << target);
            clueMarkFound(c->id);
            encounterLog(c->text[0] ? c->text : "You notice something.");
        } else {
            encounterLog("Nothing stands out. The scene keeps its secrets.");
        }
    }
    encounter.invTurns--;

    /* Victory: all key clues found */
    if ((encounter.invFoundMask & inv->keyMask) == inv->keyMask) {
        encounter.invSuccess = 1;
        encounter.phase      = ENCOUNTER_PHASE_VICTORY;
        if (inv->rewardItem != 0xFF) addItem(inv->rewardItem);
        return;
    }
    /* Timeout */
    if (encounter.invTurns <= 0)
        encounter.phase = ENCOUNTER_PHASE_TIMEOUT;
}
