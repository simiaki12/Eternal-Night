#pragma once
#include <stdint.h>
#include "game.h"
#include "pak.h"

#define DIALOG_MAX_OPTIONS  4
#define DIALOG_MAX_NODES   16
#define DIALOG_MAX_TREES   64

typedef struct {
    uint8_t requiredDomain; /* DOMAIN_* or 0xFF = no requirement */
    uint8_t requiredLevel;  /* minimum domain level */
    int8_t  nextNode;       /* index into tree's node array, or -1 = close dialog */
    char    text[41];       /* what the player says/chooses */
} DialogOption;             /* 44 bytes */

typedef struct {
    char         line[74];                       /* NPC speech — wraps at ~37 chars (scale 2) */
    uint8_t      optionCount;
    DialogOption options[DIALOG_MAX_OPTIONS];    /* 4 × 44 = 176 bytes */
} DialogNode;                                    /* 251 bytes */

typedef struct {
    char       speakerName[32];
    uint8_t    nodeCount;
    DialogNode nodes[DIALOG_MAX_NODES];          /* 16 × 251 = 4016 bytes */
} DialogTree;                                    /* 4049 bytes, no pointers */

typedef struct {
    int       treeId;
    int       nodeIdx;
    int       selected;
    GameState returnState;
} DialogState;

typedef struct {
    int selected;
} TownState;

extern DialogState dialogSt;
extern TownState   townSt;
extern DialogTree  dialogTrees[DIALOG_MAX_TREES];
extern int         dialogTreeCount;

int  loadDialogs(PakData data);
void startTown(void);
void startDialog(int treeId, GameState returnTo);
void handleTownInput(int key);
void handleDialogInput(int key);
void renderTown(void);
void renderDialog(void);
