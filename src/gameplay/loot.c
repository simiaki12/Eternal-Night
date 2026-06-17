#include "loot.h"
#include "quests.h"
#include "items.h"
#include <stdlib.h>
#include <string.h>

LootTable lootTables[LOOT_TABLE_MAX];
int       lootTableCount = 0;

int loadLootTables(PakData data) {
    lootTableCount = 0;
    if (!data.data || data.size < 1) return 0;

    const uint8_t *d   = (const uint8_t *)data.data;
    uint32_t       pos = 0;

    uint8_t tc = d[pos++];
    if (tc > LOOT_TABLE_MAX) tc = LOOT_TABLE_MAX;

    for (int t = 0; t < tc; t++) {
        if (pos >= data.size) break;
        uint8_t ec = d[pos++];
        if (ec > LOOT_ENTRY_MAX) ec = LOOT_ENTRY_MAX;
        if (pos + (uint32_t)ec * sizeof(LootEntry) > data.size) break;
        lootTables[t].count = ec;
        memcpy(lootTables[t].entries, d + pos, (size_t)ec * sizeof(LootEntry));
        pos += (uint32_t)ec * sizeof(LootEntry);
        lootTableCount++;
    }
    return lootTableCount;
}

void rollLoot(uint8_t tableId, uint8_t *dropsOut, int *dropCountOut) {
    if (tableId >= (uint8_t)lootTableCount) return;
    LootTable *t = &lootTables[tableId];
    for (int i = 0; i < t->count && *dropCountOut < LOOT_DROP_MAX; i++) {
        LootEntry *e = &t->entries[i];
        /* Quest gate */
        if (e->questId != 0xFF) {
            if (e->questId >= MAX_QUESTS) continue;
            if (questSt.status[e->questId] != e->questStatus) continue;
        }
        /* Chance roll */
        if ((rand() % 256) >= e->chance) continue;
        /* Drop it — only record on the victory screen if it actually fit in
           the bag, so we never claim an item the player didn't receive. */
        if (addItem(e->itemId))
            dropsOut[(*dropCountOut)++] = e->itemId;
    }
}
