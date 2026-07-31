#pragma once
#include <stdint.h>
#include "pak.h"
#include "enc_graph.h"

/* -----------------------------------------------------------------------
 * Hunt encounters — the group walks the shared hunt morale graph
 * (Organized -> Disturbed -> Alerted -> Terrified / Broken / Preparing)
 * while a counter tracks how many of them are still standing.
 *
 * Two ways to thin the group: an action's onPlay EFX_KILL, or entering a
 * rout state whose onEnter kills (Terrified/Broken). The hunt is won when
 * the counter empties — no terminal state needed.
 *
 * Botched actions make noise: every structural whiff raises `alert`. Each
 * `escalateEvery` points of it pushes the group one step along escalateTo[],
 * and reaching `alertLimit` means they stop reacting and attack — the
 * encounter cascades into a real fight with whoever is left.
 * ----------------------------------------------------------------------- */

#define HUNT_ENC_MAX 16

/* Encounter flags */
#define HUNT_FLAG_REPEATABLE (1<<0)

/* Camp zone flags */
#define CAMP_FLAG_REPEATABLE (1<<0)

/* 44 bytes */
typedef struct {
    uint8_t id;
    char    name[24];
    uint8_t enemyPoolId;    /* pool used when they turn and fight; 0xFF = none */
    uint8_t enemyCount;     /* initial group size                              */
    uint8_t stateMask;      /* hunt-graph states this group can enter          */
    uint8_t tenacity;       /* % scaling of progress banked against them; 0=100 */
    uint8_t escalateEvery;  /* alert points per escalation step; 0 = never      */
    uint8_t alertLimit;     /* alert at which they attack; 0 = never            */
    uint8_t escalateTo[GRAPH_STATES_MAX]; /* per state: next posture, 0xFF none */
    uint8_t flags;          /* HUNT_FLAG_*                                      */
    uint8_t setFlag;        /* world flag set on success; 0xFF = none           */
    uint8_t _pad[3];
} HuntEncounterDef;

typedef char _check_hunt_enc[(sizeof(HuntEncounterDef) == 44) ? 1 : -1];

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
void huntEncounterDoAction(uint8_t actionId);
const HuntEncounterDef *huntEncGetDef(uint8_t id);
