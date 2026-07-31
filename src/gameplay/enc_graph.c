#include <string.h>
#include "enc_graph.h"

StateDef encGraphs[ENC_TYPE_COUNT][GRAPH_STATES_MAX];
uint8_t  encGraphCount[ENC_TYPE_COUNT];

int loadEncGraphs(PakData data) {
    if (!data.data || data.size < 1) return 0;
    const uint8_t *p   = (const uint8_t *)data.data;
    const uint8_t *end = p + data.size;
    uint8_t nGraphs = *p++;
    if (nGraphs > ENC_TYPE_COUNT) nGraphs = ENC_TYPE_COUNT;
    int total = 0;
    for (int g = 0; g < nGraphs; g++) {
        if (p >= end) break;
        uint8_t n = *p++;
        if (n > GRAPH_STATES_MAX) n = GRAPH_STATES_MAX;
        if (p + (size_t)n * sizeof(StateDef) > end) break;
        memcpy(encGraphs[g], p, (size_t)n * sizeof(StateDef));
        p += (size_t)n * sizeof(StateDef);
        encGraphCount[g] = n;
        total += n;
    }
    return total;
}

int encTypeIndex(uint8_t encounterCatBit) {
    for (int i = 0; i < ENC_TYPE_COUNT; i++)
        if (encounterCatBit == (uint8_t)(1u << i)) return i;
    return -1;
}

const StateDef *encGraphState(int typeIdx, uint8_t stateId) {
    if (typeIdx < 0 || typeIdx >= ENC_TYPE_COUNT)  return NULL;
    if (stateId >= encGraphCount[typeIdx])          return NULL;
    return &encGraphs[typeIdx][stateId];
}

int encGraphStart(int typeIdx) {
    if (typeIdx < 0 || typeIdx >= ENC_TYPE_COUNT) return 0;
    for (int i = 0; i < encGraphCount[typeIdx]; i++)
        if (encGraphs[typeIdx][i].flags & GSTATE_START) return i;
    return 0;
}
