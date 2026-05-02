#pragma once
#include <stdint.h>
#include "pak.h"

#define NPC_DEF_MAX 32

/* 32 bytes — stored in npcs.dat */
typedef struct {
    char    name[16];   /* display name shown as speaker in dialog */
    char    imgName[3]; /* 2-char base name of .bin sprite, e.g. "vi" → assets/vi.bin */
    uint8_t treeId;     /* index into dialogTrees[]; 0xFF = no dialog */
    uint8_t x;          /* tile position on the map */
    uint8_t y;
    char    mapId[8];   /* short identifier matched against currentMapName (e.g. "map1") */
    uint8_t _pad[2];
} NpcDef;               /* 32 bytes */

/* File format (npcs.dat):
 *   [1]      npc count
 *   [N×32]   NpcDef array
 */

extern NpcDef npcDefs[NPC_DEF_MAX];
extern int    npcDefCount;

int  loadNpcs(PakData data);
void renderNpcs(int tx, int ty, int rCamX, int rCamY);
int  npcTryInteract(void);   /* starts dialog if adjacent NPC found; returns 1 if triggered */
int  npcGetPrompt(char *buf, int len); /* fills buf with "[E]: Talk with Name" if adjacent NPC */
int  npcGetInteractable(int *outIdxs, int max); /* fills outIdxs with adjacent NPC indices; returns count */
void npcTriggerByIdx(int idx);                  /* start dialog for NPC at npcDefs[idx] */
