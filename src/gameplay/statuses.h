#pragma once
#include <stdint.h>
#include "pak.h"
#include "effects.h"

/* -----------------------------------------------------------------------
 * Persistent status layer — buffs and conditions on Azrael.
 * Encounter buffs (DUR_TURNS) and world conditions (DUR_STEPS/DUR_PERM)
 * are one system; only their clock differs. DUR_TURNS statuses are swept
 * when the encounter ends.
 * ----------------------------------------------------------------------- */

#define STATUS_DEF_MAX    32
#define PLAYER_STATUS_MAX 8

typedef enum {
    DUR_TURNS = 0, /* encounter turns; cleared at encounter end          */
    DUR_STEPS = 1, /* world-map steps                                    */
    DUR_PERM  = 2, /* until explicitly cured; duration field unused      */
} DurType;

/* What a status passively does while active */
typedef enum {
    STFX_NONE         = 0,
    STFX_PROGRESS_ALL = 1, /* +-(int8)fxValue progress on every transition attempt */
    STFX_PROGRESS_DOM = 2, /* same, only for actions of domain fxValue2            */
    STFX_THREAT_MOD   = 3, /* +-(int8)fxValue to damage taken from enemies         */
    STFX_HP_TICK      = 4, /* +-(int8)fxValue HP on each tick of the status clock  */
    STFX_WEIGHT_DOM   = 5, /* +-(int8)fxValue draw weight for domain fxValue2      */
    STFX_COUNT
} StatusFx;

/* StatusDef flags */
#define STATUS_NEGATIVE (1<<0) /* curable by EFX_CLEAR_STATUS 0xFF; drawn red */

/* 24 bytes — stored in statuses.dat */
typedef struct {
    uint8_t id;
    char    name[12];
    char    icon[2];     /* 1-2 glyphs for the HUD strip */
    uint8_t durType;     /* DurType                       */
    uint8_t duration;    /* turns or steps; 0 if DUR_PERM */
    uint8_t fxType;      /* StatusFx                      */
    uint8_t fxValue;     /* signed where noted (int8_t)   */
    uint8_t fxValue2;    /* domain id for _DOM effects    */
    uint8_t flags;       /* STATUS_*                      */
    Effect  onExpire;    /* fires when the clock runs out (e.g. Wounded -> Sick) */
} StatusDef;

typedef char _check_statusdef_size[(sizeof(StatusDef) == 24) ? 1 : -1];

/* One active status on the player — statusId 0xFF = empty slot */
typedef struct {
    uint8_t statusId;
    uint8_t remaining; /* turns or steps left; unused for DUR_PERM */
} PlayerStatus;

/* File format (statuses.dat): [1] count, [N×24] StatusDef array */

extern StatusDef statusDefs[STATUS_DEF_MAX];
extern int       statusDefCount;

int              loadStatuses(PakData data);
const StatusDef *statusGetDef(uint8_t id);

void statusApply(uint8_t id);        /* re-applying refreshes the clock     */
void statusClear(uint8_t id);        /* 0xFF = clear all STATUS_NEGATIVE    */
void statusTickTurn(void);           /* call once per encounter turn        */
void statusTickStep(void);           /* call once per world-map step        */
void statusEncounterEnd(void);       /* sweep all DUR_TURNS statuses        */

int  statusWeightBonus(uint8_t domain);   /* summed STFX_WEIGHT_DOM, signed */
int  statusThreatMod(void);               /* summed STFX_THREAT_MOD, signed */
int  statusProgressBonus(uint8_t domain); /* PROGRESS_ALL + matching _DOM   */

/* Execute an Effect (rolls chance internally). Engine-scoped types
   (EFX_PROGRESS*, EFX_EXTRA_ACTION, EFX_METER) are handled by the
   encounter engine, not here. Caller checks player.hp afterwards. */
void applyEffect(const Effect *e);

int  renderStatusStrip(int x, int y, int outlined); /* one line of icons;
                                                       returns statuses drawn */
