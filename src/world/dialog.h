#pragma once
#include <stdint.h>
#include "game.h"
#include "pak.h"

#define DIALOG_MAX_OPTIONS  4
#define DIALOG_MAX_NODES   16
#define DIALOG_MAX_TREES   64

/* Dialog option effects — applied when the option is selected */
#define DLGFX_NONE              0
#define DLGFX_SOCIAL_ENCOUNTER  1  /* effect_arg = se_idx */

typedef struct {
    uint8_t requiredDomain; /* DOMAIN_* or 0xFF = no requirement */
    uint8_t requiredLevel;  /* minimum domain level */
    int8_t  nextNode;       /* index into tree's node array, or -1 = close dialog */
    uint8_t effect_type;    /* DLGFX_* */
    uint8_t effect_arg;     /* se_idx for DLGFX_SOCIAL_ENCOUNTER; 0 otherwise */
    char    text[41];       /* what the player says/chooses */
} DialogOption;             /* 46 bytes */

typedef struct {
    char         line[74];                       /* NPC speech — wraps at ~37 chars (scale 2) */
    uint8_t      optionCount;
    DialogOption options[DIALOG_MAX_OPTIONS];
} DialogNode;

typedef struct {
    char       speakerName[32];
    uint8_t    nodeCount;
    DialogNode nodes[DIALOG_MAX_NODES];
} DialogTree;

typedef struct {
    int       treeId;
    int       nodeIdx;
    int       selected;
    GameState returnState;
} DialogState;

extern DialogState dialogSt;
extern DialogTree  dialogTrees[DIALOG_MAX_TREES];
extern int         dialogTreeCount;

int  loadDialogs(PakData data);
void startDialog(int treeId, GameState returnTo);
void handleDialogInput(int key);
void renderDialog(void);
