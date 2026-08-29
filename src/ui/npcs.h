#pragma once
#include <stdint.h>
#include "pak.h"

#define NPC_DEF_MAX 32

/* 40 bytes — stored in npcs.dat */
typedef struct {
    char    name[16];       /* display name shown as speaker in dialog */
    char    imgName[3];     /* 2-char base name of .bin sprite, e.g. "vi" → assets/vi.bin */
    uint8_t treeId;         /* index into dialogTrees[]; 0xFF = no dialog */
    uint8_t x;              /* tile position on the map */
    uint8_t y;
    char    mapId[8];       /* short identifier matched against currentMapName (e.g. "map1") */
    /* Social encounter profile */
    uint8_t resistance;     /* how hard to shift disposition per action (0=pushover) */
    uint8_t patience;       /* turns before escalation/exit; 0 = no limit */
    uint8_t social_power;   /* counter-move impact; 0 = passive NPC */
    uint8_t move_mask;      /* NPC_MOVE_* bitmask of available counter-actions */
    uint8_t tags;           /* NPC_TAG_* bitmask of trait tags */
    uint8_t base_standing;  /* starting standing value for a new game */
    uint8_t npc_flags;      /* reserved */
    uint8_t id;             /* stable id — what other data files reference */
    uint8_t _pad[2];
} NpcDef;                   /* 40 bytes */

typedef char _check_npcdef_size[(sizeof(NpcDef) == 40) ? 1 : -1];

/* File format (npcs.dat):
 *   [1]      npc count
 *   [N×32]   NpcDef array
 */

extern NpcDef npcDefs[NPC_DEF_MAX];
extern int    npcDefCount;

int  loadNpcs(PakData data);
const NpcDef *npcGetDef(uint8_t id);   /* by stable id; NULL if unknown */
int  npcIndexById(uint8_t id);         /* slot in npcDefs[]; -1 if unknown */
void renderNpcs(int tx, int ty, int rCamX, int rCamY);
int  npcTryInteract(void);   /* starts dialog if adjacent NPC found; returns 1 if triggered */
int  npcGetPrompt(char *buf, int len); /* fills buf with "[E]: Talk with Name" if adjacent NPC */
int  npcGetInteractable(int *outIdxs, int max); /* fills outIdxs with adjacent NPC indices; returns count */
void npcTriggerByIdx(int idx);                  /* start dialog for NPC at npcDefs[idx] */
