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

    /* Restore already-found clues from persistent state */
    encounter.invFoundMask = 0;
    for (int i = 0; i < inv->clueCount; i++)
        if (clueIsFound(inv->clueIds[i]))
            encounter.invFoundMask |= (uint8_t)(1 << i);

    /* Generate first action hand */
    generateActions();
    state = STATE_ENCOUNTER;
}

/* ----------------------------------------------------------------------- */

void investigationDoAction(uint8_t actionId) {
    const InvestigationDef *inv = invGetDef((uint8_t)encounter.invId);
    if (!inv) return;
    const ActionDef *act = getActionDef(actionId);

    /* Build weighted candidate list of unfound clues */
    typedef struct { int localIdx; int weight; } Candidate;
    Candidate candidates[INV_CLUE_SLOTS];
    int candCount = 0;

    for (int i = 0; i < inv->clueCount; i++) {
        if (encounter.invFoundMask & (1 << i)) continue;
        const ClueDef *c = clueGetDef(inv->clueIds[i]);
        if (!c) continue;

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
        encounterLog("You have uncovered everything this scene has to offer.");
        encounter.invTurns--;
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
