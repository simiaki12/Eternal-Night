#include "player.h"
#include "items.h"
#include "game.h"
#include <stdint.h>
#include <string.h>

PlayerData player;

void playerInit(void) {
    memset(&player, 0, sizeof(player));
    memset(player.equipped, ITEM_UNEQUIPPED, EQUIP_SLOTS);
    player.maxHp        = 100;
    player.attack       =  5;
    player.defense      =  5;
    player.stamina      =  3;
    player.intelligence =  3;
    player.perception   =  3;
    player.charisma     =  3;
    player.agility      =  3;
    player.hp           = player.maxHp;
    player.gold         = 200;
    player.focusedDomain  = DOMAIN_NONE;
    player.favourCount    = 0;
    player.suppressCount  = 0;
    memset(player.favouredActions,   0xFF, FAVOUR_MAX);
    memset(player.suppressedActions, 0xFF, FAVOUR_MAX);
    memset(player.clueFound,   0, sizeof(player.clueFound));
    memset(player.worldFlags,  0, sizeof(player.worldFlags));
    /* perk_points start at 0 (already zeroed by memset above) — earned by
       levelling domains. */
}

int favourSlots(void) {
    uint8_t best = 0;
    for (int i = 0; i < DOMAIN_COUNT; i++)
        if (player.domains[i].level > best) best = player.domains[i].level;
    int n = best > 0 ? 1 + (best - 1) / 2 : 1;
    return n > FAVOUR_MAX ? FAVOUR_MAX : n;
}

int isActionFavoured(uint8_t id) {
    for (int i = 0; i < player.favourCount; i++)
        if (player.favouredActions[i] == id) return 1;
    return 0;
}

int isActionSuppressed(uint8_t id) {
    for (int i = 0; i < player.suppressCount; i++)
        if (player.suppressedActions[i] == id) return 1;
    return 0;
}

void toggleFavoured(uint8_t id) {
    for (int i = 0; i < player.favourCount; i++) {
        if (player.favouredActions[i] == id) {
            for (int j = i; j < player.favourCount - 1; j++)
                player.favouredActions[j] = player.favouredActions[j + 1];
            player.favourCount--;
            return;
        }
    }
    playerRemoveActionFromQueues(id);
    int cap = favourSlots();
    if (player.favourCount >= (uint8_t)cap) {
        for (int i = 0; i < player.favourCount - 1; i++)
            player.favouredActions[i] = player.favouredActions[i + 1];
        player.favourCount--;
    }
    player.favouredActions[player.favourCount++] = id;
}

void toggleSuppressed(uint8_t id) {
    for (int i = 0; i < player.suppressCount; i++) {
        if (player.suppressedActions[i] == id) {
            for (int j = i; j < player.suppressCount - 1; j++)
                player.suppressedActions[j] = player.suppressedActions[j + 1];
            player.suppressCount--;
            return;
        }
    }
    playerRemoveActionFromQueues(id);
    int cap = favourSlots();
    if (player.suppressCount >= (uint8_t)cap) {
        for (int i = 0; i < player.suppressCount - 1; i++)
            player.suppressedActions[i] = player.suppressedActions[i + 1];
        player.suppressCount--;
    }
    player.suppressedActions[player.suppressCount++] = id;
}

void playerRemoveActionFromQueues(uint8_t id) {
    int i, j;
    for (i = 0; i < player.favourCount; i++) {
        if (player.favouredActions[i] == id) {
            for (j = i; j < player.favourCount - 1; j++)
                player.favouredActions[j] = player.favouredActions[j + 1];
            player.favourCount--;
            break;
        }
    }
    for (i = 0; i < player.suppressCount; i++) {
        if (player.suppressedActions[i] == id) {
            for (j = i; j < player.suppressCount - 1; j++)
                player.suppressedActions[j] = player.suppressedActions[j + 1];
            player.suppressCount--;
            break;
        }
    }
}

void enterDeath(void) {
    player.hp = player.maxHp;
    state     = STATE_DEATH;
}

int worldFlagGet(uint8_t flagId) {
    if (flagId >= 128) return 0;
    return (player.worldFlags[flagId / 8] >> (flagId % 8)) & 1;
}

void worldFlagSet(uint8_t flagId) {
    if (flagId >= 128) return;
    player.worldFlags[flagId / 8] |= (uint8_t)(1 << (flagId % 8));
}

void worldFlagClear(uint8_t flagId) {
    if (flagId >= 128) return;
    player.worldFlags[flagId / 8] &= (uint8_t)~(1 << (flagId % 8));
}
