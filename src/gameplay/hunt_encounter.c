#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "hunt_encounter.h"
#include "encounter.h"
#include "enemies.h"
#include "player.h"
#include "domains.h"
#include "actions.h"
#include "statuses.h"
#include "enc_graph.h"
#include "game.h"

HuntEncounterDef huntEncDefs[HUNT_ENC_MAX];
int              huntEncCount = 0;

CampZone campZones[64];
int      campZoneCount = 0;

int loadHuntEncounters(PakData data) {
    if (!data.data || data.size < 1) return 0;
    const uint8_t *p = (const uint8_t *)data.data;
    uint8_t n = *p++;
    if (n > HUNT_ENC_MAX) n = HUNT_ENC_MAX;
    size_t available = (data.size - 1) / sizeof(HuntEncounterDef);
    if ((size_t)n > available) n = (uint8_t)available;
    memcpy(huntEncDefs, p, (size_t)n * sizeof(HuntEncounterDef));
    huntEncCount = n;
    return huntEncCount;
}

int loadCampZones(PakData data) {
    if (!data.data || data.size < 1) return 0;
    const uint8_t *p = (const uint8_t *)data.data;
    uint8_t n = *p++;
    if (n > 64) n = 64;
    size_t available = (data.size - 1) / sizeof(CampZone);
    if ((size_t)n > available) n = (uint8_t)available;
    memcpy(campZones, p, (size_t)n * sizeof(CampZone));
    campZoneCount = n;
    return campZoneCount;
}

static int mapMatches(const char *mapId, const char *mapName) {
    const char *slash = strrchr(mapName, '/');
    const char *base  = slash ? slash + 1 : mapName;
    size_t idLen = strlen(mapId);
    return strncmp(base, mapId, idLen) == 0 &&
           (base[idLen] == '.' || base[idLen] == '\0');
}

int campZoneAt(const char *mapName, uint8_t x, uint8_t y) {
    for (int i = 0; i < campZoneCount; i++) {
        const CampZone *z = &campZones[i];
        if (x < z->leftX || x > z->rightX || y < z->topY || y > z->bottomY) continue;
        if (!mapMatches(z->mapId, mapName)) continue;
        if (z->clearedFlag != 0xFF && worldFlagGet(z->clearedFlag)) continue;
        return (int)z->huntEncId;
    }
    return -1;
}

const HuntEncounterDef *huntEncGetDef(uint8_t id) {
    for (int i = 0; i < huntEncCount; i++)
        if (huntEncDefs[i].id == id) return &huntEncDefs[i];
    return NULL;
}

/* -------------------------------------------------------------------- */

/* Thin the group by n (0xFF = all of them). */
static void huntKill(int n) {
    if (n < 0) return;
    if (n > encounter.huntEnemiesLeft) n = encounter.huntEnemiesLeft;
    if (n == 0) return;
    encounter.huntEnemiesLeft -= n;

    char msg[28];
    if (encounter.huntEnemiesLeft == 0 && n > 1) snprintf(msg, 28, "All %d go down.", n);
    else if (n > 1)                              snprintf(msg, 28, "%d enemies eliminated.", n);
    else                                         snprintf(msg, 28, "One enemy down.");
    encounterLog(msg);
}

/* Hunt-scoped effect executor: EFX_KILL is ours, the rest are generic. */
static void applyHuntEffect(const Effect *e) {
    if (!e || e->type == EFX_NONE || e->chance == 0) return;
    if (e->type == EFX_KILL) {
        if (rand() % 100 < e->chance)
            huntKill(e->value == 0xFF ? encounter.huntEnemiesLeft : (int)e->value);
        return;
    }
    applyEffect(e);
}

/* They stop reacting and come at you with whoever is left. */
static void huntCombatFallback(void) {
    const HuntEncounterDef *def = huntEncGetDef((uint8_t)encounter.huntEncId);
    int n = encounter.huntEnemiesLeft;
    if (!def || n <= 0 || def->enemyPoolId == 0xFF) {
        encounter.phase = ENCOUNTER_PHASE_TIMEOUT;
        encounterLog("They slip away.");
        return;
    }
    encounterLog("They turn and charge!");
    encounterStartFromPoolN(def->enemyPoolId, n);
}

static void huntVictory(void) {
    const HuntEncounterDef *def = huntEncGetDef((uint8_t)encounter.huntEncId);
    if (def && def->setFlag != 0xFF) worldFlagSet(def->setFlag);
    for (int i = 0; i < campZoneCount; i++) {
        if (campZones[i].huntEncId == (uint8_t)encounter.huntEncId &&
            campZones[i].clearedFlag != 0xFF)
            worldFlagSet(campZones[i].clearedFlag);
    }
    encounter.phase = ENCOUNTER_PHASE_VICTORY;
    encounterLog("The camp is silent.");
}

void huntEncounterStart(int encId) {
    const HuntEncounterDef *def = huntEncGetDef((uint8_t)encId);
    if (!def) return;

    EnemyDef group;
    memset(&group, 0, sizeof(group));
    strncpy(group.name, def->name, sizeof(group.name) - 1);
    group.lootTableId = 0xFF;
    group.stateMask   = def->stateMask;
    group.tenacity    = def->tenacity;

    encounterStart(ENCOUNTER_HUNT, &group, 0);
    encounter.huntEncId        = encId;
    encounter.huntEnemiesLeft  = (int)def->enemyCount;
    encounter.huntEnemiesTotal = (int)def->enemyCount;
    encounter.huntAlert        = 0;
    encounter.enemyState[0]    = (uint8_t)encGraphStart(ENC_IDX_HUNT);
    encounter.potCount[0]      = 0;

    char msg[28];
    snprintf(msg, 28, "Hunt: %.19s", def->name);
    encounterLog(msg);
}

void huntEncounterDoAction(uint8_t actionId) {
    const HuntEncounterDef *def = huntEncGetDef((uint8_t)encounter.huntEncId);
    if (!def) return;
    const ActionDef *adef = getActionDef(actionId);
    char msg[28];

    /* Kills and other payloads fire whether or not the group's posture moves */
    if (adef) applyHuntEffect(&adef->onPlay);

    if (encounter.huntEnemiesLeft > 0) {
        GraphResult r = encGraphResolve(0, ENC_IDX_HUNT, adef, def->stateMask,
                                        def->tenacity, applyHuntEffect);

        /* A botched approach makes noise and stirs the camp */
        if (r == GRAPH_WHIFF || r == GRAPH_NO_TRIPLETS) {
            encounter.huntAlert++;
            if (def->escalateEvery > 0 &&
                encounter.huntAlert % def->escalateEvery == 0) {
                uint8_t cur  = encounter.enemyState[0];
                uint8_t next = (cur < GRAPH_STATES_MAX) ? def->escalateTo[cur] : 0xFF;
                if (next != 0xFF && next != cur &&
                    (def->stateMask & (1u << next))) {
                    encounter.potCount[0]   = 0;
                    encounter.enemyState[0] = next;
                    const StateDef *ns = encGraphState(ENC_IDX_HUNT, next);
                    snprintf(msg, 28, "They grow %.14s!", ns ? ns->name : "?");
                    encounterLog(msg);
                    if (ns) applyHuntEffect(&ns->onEnter);
                }
            }
        }
    }

    /* Domain XP */
    if (adef && adef->domain < 14) {
        domainAwardXp(adef->domain, 1);
        if (encounter.gainedDomainXp[adef->domain] < 255)
            encounter.gainedDomainXp[adef->domain]++;
    }

    if (encounter.huntEnemiesLeft <= 0) { huntVictory(); return; }

    /* They are done reacting */
    if (def->alertLimit > 0 && encounter.huntAlert >= def->alertLimit) {
        huntCombatFallback();
        return;
    }

    /* Pressure from their current posture */
    if (encounter.extraActionPending) {
        encounter.extraActionPending = 0;
    } else {
        const StateDef *st = encGraphState(ENC_IDX_HUNT, encounter.enemyState[0]);
        int threat = (st ? st->pressure : 0) + statusThreatMod();
        if (threat > 0) {
            player.hp = (threat >= (int)player.hp) ? 0 : (uint16_t)(player.hp - threat);
            snprintf(msg, 28, "Pressure: -%d HP.", threat);
            encounterLog(msg);
        }
    }

    statusTickTurn();
    if (player.hp == 0) {
        encounterLog("You fall unconscious.");
        enterDeath();
        return;
    }

    encounter.isFirstTurn = 0;
    if (encounter.phase == ENCOUNTER_PHASE_ACTIVE) generateActions();
}
