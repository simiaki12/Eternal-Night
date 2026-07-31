#include <string.h>
#include "cases.h"
#include "encounter.h"
#include "enc_graph.h"
#include "player.h"

CaseDef caseDefs[CASE_DEF_MAX];
int     caseDefCount = 0;

int loadCases(PakData data) {
    if (!data.data || data.size < 1) return 0;
    const uint8_t *p = (const uint8_t *)data.data;
    uint8_t n = p[0];
    if (n > CASE_DEF_MAX) n = CASE_DEF_MAX;
    if (data.size < 1 + (uint32_t)n * sizeof(CaseDef)) return 0;
    memcpy(caseDefs, p + 1, (size_t)n * sizeof(CaseDef));
    caseDefCount = n;
    return n;
}

const CaseDef *caseGetDef(uint8_t id) {
    for (int i = 0; i < caseDefCount; i++)
        if (caseDefs[i].id == id) return &caseDefs[i];
    return NULL;
}

/* Index into player.caseStates[] for a case id */
static int caseSlot(uint8_t id) {
    for (int i = 0; i < caseDefCount; i++)
        if (caseDefs[i].id == id) return i;
    return -1;
}

void casesNewGame(void) {
    memset(player.caseStates, 0, sizeof(player.caseStates));
    for (int i = 0; i < caseDefCount; i++)
        player.caseStates[i].state = caseDefs[i].startState;
}

uint8_t caseGetState(uint8_t caseId) {
    int s = caseSlot(caseId);
    return s < 0 ? 0 : player.caseStates[s].state;
}

int caseIsComplete(uint8_t caseId) {
    const StateDef *st = encGraphState(ENC_IDX_INVESTIGATION, caseGetState(caseId));
    return st && (st->flags & GSTATE_TERMINAL);
}

void caseLoadIntoSlot(uint8_t caseId) {
    int s = caseSlot(caseId);
    encounter.potCount[0] = 0;
    if (s < 0) { encounter.enemyState[0] = (uint8_t)encGraphStart(ENC_IDX_INVESTIGATION); return; }

    const CaseSaveState *cs = &player.caseStates[s];
    encounter.enemyState[0] = cs->state;
    for (int i = 0; i < CASE_POT_MAX; i++) {
        if (cs->potVal[i] == 0) continue;
        int idx = encounter.potCount[0]++;
        encounter.potKey[0][idx] = cs->potKey[i];
        encounter.potVal[0][idx] = cs->potVal[i];
    }
}

void caseStoreFromSlot(uint8_t caseId) {
    int s = caseSlot(caseId);
    if (s < 0) return;
    CaseSaveState *cs = &player.caseStates[s];
    cs->state = encounter.enemyState[0];
    memset(cs->potKey, 0, sizeof(cs->potKey));
    memset(cs->potVal, 0, sizeof(cs->potVal));
    int n = encounter.potCount[0];
    if (n > CASE_POT_MAX) n = CASE_POT_MAX;
    for (int i = 0; i < n; i++) {
        cs->potKey[i] = encounter.potKey[0][i];
        cs->potVal[i] = encounter.potVal[0][i];
    }
}
