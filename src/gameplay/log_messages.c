#include "log_messages.h"
#include "encounter.h"
#include "player.h"
#include "items.h"
#include "world_enemies.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

LogMessage logMessages[LOG_MSG_MAX];
int        logMessageCount = 0;

int loadLogMessages(PakData data) {
    if (!data.data || data.size < 1) return 0;
    const uint8_t *p = data.data;
    uint8_t n = *p++;
    if (n > LOG_MSG_MAX) n = LOG_MSG_MAX;
    size_t available = (data.size - 1) / sizeof(LogMessage);
    if ((size_t)n > available) n = (uint8_t)available;
    memcpy(logMessages, p, (size_t)n * sizeof(LogMessage));
    logMessageCount = n;
    return logMessageCount;
}

static int resolveTargets(LogFxTarget targetType, int slotHint, int *slots, int maxSlots) {
    int count = 0;
    (void)slotHint;
    switch (targetType) {
        case LOGFX_TARGET_FOCUSED:
            if (encounter.enemies[encounter.targetIndex].maxHp > 0)
                slots[count++] = encounter.targetIndex;
            break;
        case LOGFX_TARGET_WEAKEST: {
            int best = -1, bestHp = 0x7FFF;
            for (int i = 0; i < encounter.enemyCount; i++) {
                if (encounter.enemies[i].maxHp > 0 && encounter.enemies[i].hp < bestHp) {
                    bestHp = encounter.enemies[i].hp;
                    best   = i;
                }
            }
            if (best >= 0) slots[count++] = best;
            break;
        }
        case LOGFX_TARGET_BIGGEST: {
            int best = -1, bestSz = -1;
            for (int i = 0; i < encounter.enemyCount; i++) {
                if (encounter.enemies[i].maxHp > 0 && (int)encounter.enemies[i].size > bestSz) {
                    bestSz = encounter.enemies[i].size;
                    best   = i;
                }
            }
            if (best >= 0) slots[count++] = best;
            break;
        }
        case LOGFX_TARGET_RANDOM: {
            int alive[ENCOUNTER_MAX_ENEMIES], n = 0;
            for (int i = 0; i < encounter.enemyCount; i++)
                if (encounter.enemies[i].maxHp > 0) alive[n++] = i;
            if (n > 0) slots[count++] = alive[rand() % n];
            break;
        }
        case LOGFX_TARGET_ALL:
            for (int i = 0; i < encounter.enemyCount && count < maxSlots; i++)
                if (encounter.enemies[i].maxHp > 0) slots[count++] = i;
            break;
    }
    return count;
}

static void applyEffect(const LogMessage *m, int slotHint) {
    if (m->effectType == LOGFX_NONE) return;

    int slots[ENCOUNTER_MAX_ENEMIES];
    int nTargets = resolveTargets((LogFxTarget)m->effectTarget, slotHint,
                                  slots, ENCOUNTER_MAX_ENEMIES);

    switch ((LogFxType)m->effectType) {
        case LOGFX_ADD_MOD:
            encounter.modifiers |= m->effectValue;
            break;
        case LOGFX_REMOVE_MOD:
            encounter.modifiers &= ~(uint32_t)m->effectValue;
            break;
        case LOGFX_ENEMY_FLEE:
            for (int i = 0; i < nTargets; i++) {
                int s = slots[i];
                if (encounter.enemies[s].maxHp > 0) {
                    char vm[28];
                    snprintf(vm, sizeof(vm), "%.16s flees!", encounter.enemies[s].name);
                    encounterLog(vm);
                    encounter.enemies[s].maxHp = 0;
                    encounter.enemies[s].hp    = 0;
                }
            }
            break;
        case LOGFX_ENEMY_STUN:
            for (int i = 0; i < nTargets; i++)
                if (encounter.enemies[slots[i]].maxHp > 0)
                    encounter.skipEnemyAttack = 1;
            break;
        case LOGFX_ENEMY_ATK_DOWN:
            for (int i = 0; i < nTargets; i++) {
                int s = slots[i];
                if (encounter.enemies[s].maxHp > 0)
                    encounter.enemies[s].attack = encounter.enemies[s].attack > m->effectValue
                        ? encounter.enemies[s].attack - m->effectValue : 1;
            }
            break;
        case LOGFX_HEAL_PLAYER: {
            int newHp = (int)player.hp + m->effectValue;
            int cap   = getMaxHp();
            player.hp = (uint16_t)(newHp > cap ? cap : newHp);
            break;
        }
        case LOGFX_DAMAGE_PLAYER:
            player.hp = (m->effectValue >= (int)player.hp)
                ? 0 : (uint16_t)(player.hp - m->effectValue);
            break;
        case LOGFX_NONE:
        default:
            break;
    }
}

void fireLogMessages(LogTrigger trigger, uint8_t actionId, int slotHint) {
    for (int i = 0; i < logMessageCount; i++) {
        const LogMessage *m = &logMessages[i];

        if ((LogTrigger)m->trigger != trigger) continue;
        if (m->actionId != 0xFF && m->actionId != actionId) continue;
        if (m->enemyDefId != 0xFF) {
            int ok = 0;
            for (int ei = 0; ei < encounter.enemyCount; ei++)
                if (encounter.enemies[ei].maxHp > 0 && encounter.enemyDefIds[ei] == m->enemyDefId)
                    { ok = 1; break; }
            if (!ok) continue;
        }
        if (m->encounterType != 0xFF &&
            m->encounterType != (uint8_t)encounter.encounterType) continue;
        if (m->modRequired &&
            (encounter.modifiers & m->modRequired) != m->modRequired) continue;

        if ((EnemyCond)m->enemyCond != ENEMYCOND_NONE) {
            int ok = 0;
            for (int ei = 0; ei < encounter.enemyCount && !ok; ei++) {
                const Enemy *e = &encounter.enemies[ei];
                if (e->maxHp == 0) continue;
                switch ((EnemyCond)m->enemyCond) {
                    case ENEMYCOND_SIZE_GTE:
                        if (e->size >= m->enemyCondVal) ok = 1;
                        break;
                    case ENEMYCOND_HP_LTE_PCT:
                        if (e->maxHp > 0 && (e->hp * 100 / e->maxHp) <= m->enemyCondVal) ok = 1;
                        break;
                    case ENEMYCOND_HP_GTE_PCT:
                        if (e->maxHp > 0 && (e->hp * 100 / e->maxHp) >= m->enemyCondVal) ok = 1;
                        break;
                    case ENEMYCOND_HAS_FLAG:
                        if (e->flags & m->enemyCondVal) ok = 1;
                        break;
                    case ENEMYCOND_COUNT_GTE: {
                        int alive = 0;
                        for (int j = 0; j < encounter.enemyCount; j++)
                            if (encounter.enemies[j].maxHp > 0) alive++;
                        if (alive >= m->enemyCondVal) ok = 1;
                        break;
                    }
                    default: break;
                }
            }
            if (!ok) continue;
        }

        if (m->chance < 100 && (rand() % 100) >= m->chance) continue;

        encounterLog(m->text);
        applyEffect(m, slotHint);
    }
}
