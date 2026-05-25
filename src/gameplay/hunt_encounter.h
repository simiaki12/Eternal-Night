#pragma once
#include <stdint.h>
#include "pak.h"

#define HUNT_ENC_MAX    16
#define HUNT_STATE_MAX   8
#define HUNT_EDGE_MAX    4
#define HUNT_ACTION_MAX  3

/* State flags */
#define HUNT_STATE_TERMINAL (1<<0)
#define HUNT_STATE_SUCCESS  (1<<1)
#define HUNT_STATE_COMBAT   (1<<2) /* failure: start combat with remaining enemies */

/* Encounter flags */
#define HUNT_FLAG_REPEATABLE (1<<0)

/* Camp zone flags */
#define CAMP_FLAG_REPEATABLE (1<<0)

/* Action IDs used in hunt edges — must match actions.dat */
#define HUNT_ACTION_AMBUSH    14
#define HUNT_ACTION_TRACK     21
#define HUNT_ACTION_SET_TRAP  17
#define HUNT_ACTION_BLOOD_SCENT 26
#define HUNT_ACTION_BLOOD_HOWL  24
#define HUNT_ACTION_MASSACRE    36

/* 8 bytes */
typedef struct {
    uint8_t actionIds[HUNT_ACTION_MAX]; /* 0xFF = unused */
    uint8_t nextState;                  /* state index after edge fires; 0xFF = terminal */
    uint8_t setFlag;                    /* world flag to set on fire; 0xFF = none */
    uint8_t _pad[3];
} HuntEdge;

typedef char _check_hunt_edge[(sizeof(HuntEdge) == 8) ? 1 : -1];

/* 64 bytes */
typedef struct {
    char     description[24];
    HuntEdge edges[HUNT_EDGE_MAX]; /* 4 * 8 = 32 */
    uint8_t  edgeCount;
    uint8_t  turnBudget;           /* 0 = no timeout */
    uint8_t  timeoutNext;          /* state on timeout; 0xFF = trigger combat fallback */
    uint8_t  damage;               /* damage taken on entering this state */
    uint8_t  flags;                /* HUNT_STATE_* */
    uint8_t  _pad[3];
} HuntStateDef;

typedef char _check_hunt_state[(sizeof(HuntStateDef) == 64) ? 1 : -1];

/* 544 bytes */
typedef struct {
    uint8_t      id;
    char         name[24];
    uint8_t      enemyPoolId;      /* pool for combat fallback; 0xFF = no fallback enemies */
    uint8_t      enemyCount;       /* initial group size */
    uint8_t      stateCount;
    uint8_t      startState;
    uint8_t      flags;            /* HUNT_FLAG_* */
    uint8_t      _pad[2];
    HuntStateDef states[HUNT_STATE_MAX]; /* 8 * 64 = 512 */
} HuntEncounterDef;

typedef char _check_hunt_enc[(sizeof(HuntEncounterDef) == 544) ? 1 : -1];

/* 16 bytes */
typedef struct {
    char    mapId[8];
    uint8_t leftX, rightX, topY, bottomY;
    uint8_t huntEncId;   /* index into huntEncDefs[] */
    uint8_t clearedFlag; /* world flag marking zone cleared; 0xFF = no flag */
    uint8_t flags;       /* CAMP_FLAG_* */
    uint8_t _pad[1];
} CampZone;

typedef char _check_camp_zone[(sizeof(CampZone) == 16) ? 1 : -1];

extern HuntEncounterDef huntEncDefs[HUNT_ENC_MAX];
extern int              huntEncCount;

extern CampZone campZones[64];
extern int      campZoneCount;

int  loadHuntEncounters(PakData data);
int  loadCampZones(PakData data);
int  campZoneAt(const char *mapName, uint8_t x, uint8_t y); /* returns huntEncId or -1 */
void huntEncounterStart(int encId);
void huntEncounterDoAction(uint8_t actionId, uint8_t actionPower);
const HuntEncounterDef *huntEncGetDef(uint8_t id);
