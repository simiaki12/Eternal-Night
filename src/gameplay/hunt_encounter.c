#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include "hunt_encounter.h"
#include "encounter.h"
#include "enemies.h"
#include "player.h"
#include "domains.h"
#include "actions.h"
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

static void huntEnterState(int stateIdx) {
    const HuntEncounterDef *def = huntEncGetDef((uint8_t)encounter.huntEncId);
    if (!def || stateIdx < 0 || stateIdx >= def->stateCount) return;

    encounter.huntStateIdx    = stateIdx;
    encounter.huntTurnInState = 0;

    const HuntStateDef *st = &def->states[stateIdx];
    char msg[28];
    snprintf(msg, sizeof(msg), "%.27s", st->description);
    encounterLog(msg);

    if (st->damage > 0) {
        int dmg = st->damage;
        player.hp = (dmg >= (int)player.hp) ? 0 : (uint16_t)(player.hp - dmg);
        snprintf(msg, sizeof(msg), "Took %d damage.", dmg);
        encounterLog(msg);
        if (player.hp == 0) {
            encounterLog("You fall unconscious.");
            enterDeath();
        }
    }
}

static void huntCombatFallback(void) {
    const HuntEncounterDef *def = huntEncGetDef((uint8_t)encounter.huntEncId);
    int n = encounter.huntEnemiesLeft;
    if (!def || n <= 0 || def->enemyPoolId == 0xFF) {
        state = STATE_WORLD;
        return;
    }
    encounterLog("They fight back!");
    encounterStartFromPoolN(def->enemyPoolId, n);
}

static void huntResolve(int success) {
    if (success) {
        for (int i = 0; i < campZoneCount; i++) {
            if (campZones[i].huntEncId == (uint8_t)encounter.huntEncId &&
                campZones[i].clearedFlag != 0xFF)
                worldFlagSet(campZones[i].clearedFlag);
        }
        encounter.phase = ENCOUNTER_PHASE_VICTORY;
        encounterLog("Hunt complete.");
    } else {
        const HuntEncounterDef *def = huntEncGetDef((uint8_t)encounter.huntEncId);
        if (def && (def->states[encounter.huntStateIdx].flags & HUNT_STATE_COMBAT)) {
            huntCombatFallback();
        } else {
            encounter.phase = ENCOUNTER_PHASE_TIMEOUT;
            encounterLog("Hunt failed.");
        }
    }
}

void huntEncounterStart(int encId) {
    const HuntEncounterDef *def = huntEncGetDef((uint8_t)encId);
    if (!def) return;

    EnemyDef dummy;
    memset(&dummy, 0, sizeof(dummy));
    strncpy(dummy.name, def->name, sizeof(dummy.name) - 1);
    dummy.lootTableId = 0xFF;

    encounterStart(ENCOUNTER_HUNT, &dummy, 0);
    encounter.huntEncId        = encId;
    encounter.huntStateIdx     = (int)def->startState;
    encounter.huntEnemiesLeft  = (int)def->enemyCount;
    encounter.huntEnemiesTotal = (int)def->enemyCount;
    encounter.huntTurnInState  = 0;

    char msg[28];
    snprintf(msg, sizeof(msg), "Hunt: %.19s", def->name);
    encounterLog(msg);
    huntEnterState(def->startState);
    generateActions();
}

void huntEncounterDoAction(uint8_t actionId, uint8_t actionPower) {
    const HuntEncounterDef *def = huntEncGetDef((uint8_t)encounter.huntEncId);
    if (!def) return;

    const HuntStateDef *st = &def->states[encounter.huntStateIdx];

    /* Domain XP */
    {
        const ActionDef *adef = getActionDef(actionId);
        if (adef && adef->domain < 14) {
            domainAwardXp(adef->domain, 1);
            if (encounter.gainedDomainXp[adef->domain] < 255)
                encounter.gainedDomainXp[adef->domain]++;
        }
    }

    /* Search edges */
    int matched = 0;
    int transitioned = 0;
    for (int ei = 0; ei < st->edgeCount && ei < HUNT_EDGE_MAX; ei++) {
        const HuntEdge *edge = &st->edges[ei];
        int found = 0;
        for (int ai = 0; ai < HUNT_ACTION_MAX; ai++) {
            if (edge->actionIds[ai] == 0xFF) break;
            if (edge->actionIds[ai] == actionId) { found = 1; break; }
        }
        if (!found) continue;

        matched = 1;

        int killed = (actionId == HUNT_ACTION_MASSACRE)
                   ? encounter.huntEnemiesLeft
                   : (int)actionPower;
        if (killed > encounter.huntEnemiesLeft) killed = encounter.huntEnemiesLeft;
        if (killed < 0) killed = 0;

        encounter.huntEnemiesLeft -= killed;

        char msg[28];
        if (killed >= encounter.huntEnemiesTotal && killed > 1)
            snprintf(msg, sizeof(msg), "Massacre! All %d down.", killed);
        else if (killed > 1)
            snprintf(msg, sizeof(msg), "%d enemies eliminated.", killed);
        else if (killed == 1)
            snprintf(msg, sizeof(msg), "One enemy down.");
        else
            snprintf(msg, sizeof(msg), "No kill this time.");
        encounterLog(msg);

        if (edge->setFlag != 0xFF) worldFlagSet(edge->setFlag);

        if (encounter.huntEnemiesLeft <= 0) {
            huntResolve(1);
            return;
        }

        if (edge->nextState == 0xFF || (st->flags & HUNT_STATE_TERMINAL)) {
            huntResolve((st->flags & HUNT_STATE_SUCCESS) != 0);
            return;
        }
        if (edge->nextState != (uint8_t)encounter.huntStateIdx) {
            huntEnterState(edge->nextState);
            transitioned = 1;
        }
        break;
    }

    if (!matched)
        encounterLog("No opening found.");

    /* Turn accounting + timeout apply only when we stayed in this state.
       A transition via huntEnterState() already reset the turn counter and
       moved us on, so `st` (the state the action was taken in) must not be
       used to drive a timeout against the freshly-entered state. */
    if (!transitioned) {
        encounter.huntTurnInState++;
        if (st->turnBudget > 0 && encounter.huntTurnInState >= (int)st->turnBudget) {
            if (st->timeoutNext == 0xFF) {
                huntCombatFallback();
                return;
            }
            huntEnterState(st->timeoutNext);
        }
    }

    if (encounter.phase == ENCOUNTER_PHASE_ACTIVE)
        generateActions();
}
