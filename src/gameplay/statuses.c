#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "statuses.h"
#include "player.h"
#include "items.h"
#include "gfx.h"

StatusDef statusDefs[STATUS_DEF_MAX];
int       statusDefCount = 0;

int loadStatuses(PakData data) {
    if (!data.data || data.size < 1) return 0;
    const uint8_t *d = (const uint8_t *)data.data;
    uint8_t count = d[0];
    if (count > STATUS_DEF_MAX) count = STATUS_DEF_MAX;
    uint32_t expected = 1 + (uint32_t)count * sizeof(StatusDef);
    if (data.size < expected) return 0;
    memcpy(statusDefs, d + 1, (size_t)count * sizeof(StatusDef));
    statusDefCount = count;
    return statusDefCount;
}

const StatusDef *statusGetDef(uint8_t id) {
    for (int i = 0; i < statusDefCount; i++)
        if (statusDefs[i].id == id) return &statusDefs[i];
    return NULL;
}

void statusApply(uint8_t id) {
    const StatusDef *def = statusGetDef(id);
    if (!def) return;
    int empty = -1;
    for (int i = 0; i < PLAYER_STATUS_MAX; i++) {
        if (player.statuses[i].statusId == id) {          /* refresh clock */
            player.statuses[i].remaining = def->duration;
            return;
        }
        if (player.statuses[i].statusId == 0xFF && empty < 0) empty = i;
    }
    if (empty < 0) return; /* all slots busy — drop silently */
    player.statuses[empty].statusId  = id;
    player.statuses[empty].remaining = def->duration;
}

void statusClear(uint8_t id) {
    for (int i = 0; i < PLAYER_STATUS_MAX; i++) {
        if (player.statuses[i].statusId == 0xFF) continue;
        if (id == 0xFF) {
            const StatusDef *def = statusGetDef(player.statuses[i].statusId);
            if (!def || !(def->flags & STATUS_NEGATIVE)) continue;
        } else if (player.statuses[i].statusId != id) {
            continue;
        }
        player.statuses[i].statusId = 0xFF;
    }
}

void applyEffect(const Effect *e) {
    if (!e || e->type == EFX_NONE || e->chance == 0) return;
    if (rand() % 100 >= e->chance) return;
    switch (e->type) {
        case EFX_HEAL_HP: {
            int newHp = (int)player.hp + e->value;
            int cap   = getMaxHp();
            player.hp = (uint16_t)(newHp > cap ? cap : newHp);
            break;
        }
        case EFX_DAMAGE_HP:
            player.hp = (e->value >= player.hp) ? 0 : (uint16_t)(player.hp - e->value);
            break;
        case EFX_APPLY_STATUS: statusApply(e->value); break;
        case EFX_CLEAR_STATUS: statusClear(e->value); break;
        default: break; /* engine-scoped types resolve in the encounter engine */
    }
}

/* Advance every status running on `clock`; HP_TICK fires per tick of its
   own clock, expiry fires onExpire and frees the slot. */
static void tick(uint8_t clock) {
    for (int i = 0; i < PLAYER_STATUS_MAX; i++) {
        if (player.statuses[i].statusId == 0xFF) continue;
        const StatusDef *def = statusGetDef(player.statuses[i].statusId);
        if (!def) { player.statuses[i].statusId = 0xFF; continue; }
        if (def->durType != clock) continue;

        if (def->fxType == STFX_HP_TICK) {
            int newHp = (int)player.hp + (int8_t)def->fxValue;
            int cap   = getMaxHp();
            if (newHp < 0)   newHp = 0;
            if (newHp > cap) newHp = cap;
            player.hp = (uint16_t)newHp;
        }

        if (player.statuses[i].remaining > 0) player.statuses[i].remaining--;
        if (player.statuses[i].remaining == 0) {
            Effect ex = def->onExpire;
            player.statuses[i].statusId = 0xFF;
            applyEffect(&ex);
        }
    }
}

void statusTickTurn(void) { tick(DUR_TURNS); }
void statusTickStep(void) { tick(DUR_STEPS); }

void statusEncounterEnd(void) {
    for (int i = 0; i < PLAYER_STATUS_MAX; i++) {
        if (player.statuses[i].statusId == 0xFF) continue;
        const StatusDef *def = statusGetDef(player.statuses[i].statusId);
        if (def && def->durType == DUR_TURNS)
            player.statuses[i].statusId = 0xFF;
    }
}

static int sumFx(uint8_t fxType, uint8_t domain, int domainGated) {
    int sum = 0;
    for (int i = 0; i < PLAYER_STATUS_MAX; i++) {
        if (player.statuses[i].statusId == 0xFF) continue;
        const StatusDef *def = statusGetDef(player.statuses[i].statusId);
        if (!def || def->fxType != fxType) continue;
        if (domainGated && def->fxValue2 != domain) continue;
        sum += (int8_t)def->fxValue;
    }
    return sum;
}

int statusWeightBonus(uint8_t domain) {
    if (domain == 0xFF) return 0;
    return sumFx(STFX_WEIGHT_DOM, domain, 1);
}

int statusThreatMod(void) {
    return sumFx(STFX_THREAT_MOD, 0, 0);
}

int statusProgressBonus(uint8_t domain) {
    int sum = sumFx(STFX_PROGRESS_ALL, 0, 0);
    if (domain != 0xFF) sum += sumFx(STFX_PROGRESS_DOM, domain, 1);
    return sum;
}

int renderStatusStrip(int x, int y, int outlined) {
    int drawn = 0;
    for (int i = 0; i < PLAYER_STATUS_MAX; i++) {
        if (player.statuses[i].statusId == 0xFF) continue;
        const StatusDef *def = statusGetDef(player.statuses[i].statusId);
        if (!def) continue;
        char buf[8];
        char ic0 = def->icon[0] ? def->icon[0] : '?';
        char ic1 = def->icon[1];
        if (def->durType == DUR_PERM)
            snprintf(buf, sizeof(buf), "%c%c", ic0, ic1 ? ic1 : ' ');
        else
            snprintf(buf, sizeof(buf), "%c%c%d", ic0, ic1 ? ic1 : ' ',
                     player.statuses[i].remaining);
        uint32_t col = (def->flags & STATUS_NEGATIVE)
                     ? rgb(220, 80, 70) : rgb(90, 200, 110);
        if (outlined) drawTextOutlined(x, y, buf, col, 1);
        else          drawText(x, y, buf, col, 1);
        x += ((int)strlen(buf) + 1) * 8;
        drawn++;
    }
    return drawn;
}
