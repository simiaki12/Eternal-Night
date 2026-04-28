#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "town.h"
#include "game.h"
#include "player.h"
#include "skills.h"
#include "gfx.h"
#include "quests.h"
#include "shop.h"

DialogState dialogSt;
TownState   townSt;
DialogTree  dialogTrees[DIALOG_MAX_TREES];
int         dialogTreeCount = 0;

/* -----------------------------------------------------------------------
 * Built-in fallback dialog — used when dialog.dat is absent from the pak.
 * ----------------------------------------------------------------------- */
static void initBuiltinDialogs(void) {
    DialogTree *t = &dialogTrees[0];
    memset(t, 0, sizeof(*t));
    strncpy(t->speakerName, "???", 31);
    strncpy(t->nodes[0].line, "Dialog not implemented yet.", 73);
    t->nodes[0].optionCount = 1;
    t->nodes[0].options[0] = (DialogOption){ 0xFF, 0, -1, "Ok." };
    t->nodeCount = 1;
    dialogTreeCount = 1;
}

/* -----------------------------------------------------------------------
 * Packed variable-length parser helpers.
 * ----------------------------------------------------------------------- */
static int dlgReadByte(const uint8_t *d, uint32_t sz, uint32_t *pos, uint8_t *out) {
    if (*pos >= sz) return 0;
    *out = d[(*pos)++];
    return 1;
}

static int dlgReadStr(const uint8_t *d, uint32_t sz, uint32_t *pos, char *out, int maxLen) {
    uint8_t len;
    if (!dlgReadByte(d, sz, pos, &len)) return 0;
    if (*pos + len > sz) return 0;
    int n = (len < (uint8_t)(maxLen - 1)) ? len : (uint8_t)(maxLen - 1);
    memcpy(out, d + *pos, n);
    out[n] = '\0';
    *pos += len;
    return 1;
}

int loadDialogs(PakData data) {
    if (!data.data || data.size < 1) { initBuiltinDialogs(); return 0; }

    const uint8_t *d  = (const uint8_t *)data.data;
    uint32_t       pos = 0;

    uint8_t count = d[pos++];
    if (count > DIALOG_MAX_TREES) count = DIALOG_MAX_TREES;
    dialogTreeCount = 0;

    for (int i = 0; i < (int)count; i++) {
        DialogTree *t = &dialogTrees[i];
        memset(t, 0, sizeof(*t));

        if (!dlgReadStr(d, data.size, &pos, t->speakerName, 32)) goto done;

        uint8_t nc;
        if (!dlgReadByte(d, data.size, &pos, &nc)) goto done;
        if (nc > DIALOG_MAX_NODES) nc = DIALOG_MAX_NODES;
        t->nodeCount = nc;

        for (int j = 0; j < (int)nc; j++) {
            DialogNode *n = &t->nodes[j];
            if (!dlgReadStr(d, data.size, &pos, n->line, 74)) goto done;

            uint8_t oc;
            if (!dlgReadByte(d, data.size, &pos, &oc)) goto done;
            if (oc > DIALOG_MAX_OPTIONS) oc = DIALOG_MAX_OPTIONS;
            n->optionCount = oc;

            for (int k = 0; k < (int)oc; k++) {
                DialogOption *o = &n->options[k];
                if (!dlgReadByte(d, data.size, &pos, &o->requiredSkill)) goto done;
                if (!dlgReadByte(d, data.size, &pos, &o->requiredLevel)) goto done;
                if (!dlgReadByte(d, data.size, &pos, (uint8_t *)&o->nextNode)) goto done;
                if (!dlgReadStr(d, data.size, &pos, o->text, 41)) goto done;
            }
        }
        dialogTreeCount++;
    }

done:
    if (dialogTreeCount == 0) { initBuiltinDialogs(); return 0; }
    return dialogTreeCount;
}

/* ----------------------------------------------------------------------- */

static const char *townOptions[] = { "Talk", "Rest", "Shop", "Leave" };
#define TOWN_OPTION_COUNT 4

static DWORD g_restMsgEnd = 0;

void startTown(void) {
    townSt.selected = 0;
    state = STATE_TOWN;
}

void startDialog(int treeId, GameState returnTo) {
    if (treeId < 0 || treeId >= dialogTreeCount) return;
    dialogSt.treeId      = treeId;
    dialogSt.nodeIdx     = 0;
    dialogSt.selected    = 0;
    dialogSt.returnState = returnTo;
    state = STATE_DIALOG;
}

static int optionUnlocked(const DialogOption *opt) {
    if (opt->requiredSkill == 0xFF) return 1;
    return player.skills[opt->requiredSkill] >= opt->requiredLevel;
}

/* ----------------------------------------------------------------------- */

void handleTownInput(int key) {
    switch (key) {
        case VK_UP:
            townSt.selected--;
            if (townSt.selected < 0) townSt.selected = TOWN_OPTION_COUNT - 1;
            break;
        case VK_DOWN:
            townSt.selected = (townSt.selected + 1) % TOWN_OPTION_COUNT;
            break;
        case VK_RETURN:
            switch (townSt.selected) {
                case 0: startDialog(0, STATE_TOWN);          break;
                case 1:
                    player.hp    = player.maxHp;
                    g_restMsgEnd = GetTickCount() + 2500;
                    break;
                case 2: enterShop(STATE_TOWN);               break;
                case 3: state = STATE_WORLD;                 break;
            }
            break;
        case VK_ESCAPE:
            state = STATE_WORLD;
            break;
    }
}

void handleDialogInput(int key) {
    const DialogTree *tree = &dialogTrees[dialogSt.treeId];
    const DialogNode *node = &tree->nodes[dialogSt.nodeIdx];

    switch (key) {
        case VK_UP:
            dialogSt.selected--;
            if (dialogSt.selected < 0) dialogSt.selected = node->optionCount - 1;
            break;
        case VK_DOWN:
            dialogSt.selected = (dialogSt.selected + 1) % node->optionCount;
            break;
        case VK_RETURN: {
            const DialogOption *opt = &node->options[dialogSt.selected];
            if (!optionUnlocked(opt)) break;
            if (opt->nextNode < 0) {
                questOnDialogDone(dialogSt.treeId);
                state = dialogSt.returnState;
            }
            else {
                dialogSt.nodeIdx  = opt->nextNode;
                dialogSt.selected = 0;
            }
            break;
        }
        case VK_ESCAPE:
            state = dialogSt.returnState;
            break;
    }
}

/* ----------------------------------------------------------------------- */

/* Draws text at (x,y), wrapping at wrapCols characters. Returns lines drawn. */
static int drawWrapped(int x, int y, const char *text, uint32_t color, int scale, int wrapCols) {
    int len = (int)strlen(text);
    if (len <= wrapCols) {
        drawText(x, y, text, color, scale);
        return 1;
    }
    int split = wrapCols;
    while (split > 0 && text[split] != ' ') split--;
    if (split == 0) split = wrapCols;

    char line[80];
    int n = split < 79 ? split : 79;
    memcpy(line, text, n);
    line[n] = '\0';
    drawText(x, y,             line,            color, scale);
    drawText(x, y + 8 * scale, text + split + 1, color, scale);
    return 2;
}

void renderTown(void) {
    int x = 60, y = 55;
    const int lineH = 24;

    fillRect(40, 40, gfxWidth - 80, gfxHeight - 80, rgb(10, 30, 60));
    drawText(x, y, "TOWN", rgb(220, 200, 100), 2);
    y += lineH + 8;

    for (int i = 0; i < TOWN_OPTION_COUNT; i++) {
        uint32_t color = (i == townSt.selected) ? rgb(255, 255, 100) : rgb(180, 180, 180);
        char buf[32];
        snprintf(buf, sizeof(buf), "%s%s",
            (i == townSt.selected) ? "> " : "  ",
            townOptions[i]);
        drawText(x, y, buf, color, 2);
        y += lineH;
    }

    if (g_restMsgEnd && GetTickCount() < g_restMsgEnd)
        drawText(x, y + 8, "Health restored to full!", rgb(80, 220, 80), 2);
}

void renderDialog(void) {
    const DialogTree *tree = &dialogTrees[dialogSt.treeId];
    const DialogNode *node = &tree->nodes[dialogSt.nodeIdx];

    const int boxX   = 20;
    const int boxW   = gfxWidth - 40;
    const int boxY   = gfxHeight - 185;
    const int boxH   = 175;
    const int textX  = boxX + 12;
    const int scale  = 2;
    const int lineH  = 8 * scale;
    const int optH   = 18;
    const int wrapAt = (boxW - 24) / (8 * scale);

    fillRect(boxX, boxY, boxW, boxH, rgb(0, 0, 30));
    fillRect(boxX, boxY, boxW, 2, rgb(100, 140, 200));

    drawText(textX, boxY + 6, tree->speakerName, rgb(160, 200, 255), 2);

    int speechY = boxY + 28;
    int lines   = drawWrapped(textX, speechY, node->line, rgb(220, 220, 220), scale, wrapAt);
    int divY    = speechY + lines * lineH + 6;

    fillRect(boxX, divY, boxW, 1, rgb(40, 60, 100));

    int optY = divY + 5;
    for (int i = 0; i < node->optionCount; i++) {
        const DialogOption *opt = &node->options[i];
        int sel      = (i == dialogSt.selected);
        int unlocked = optionUnlocked(opt);
        char buf[56];

        if (unlocked) {
            snprintf(buf, sizeof(buf), "%s%s",
                sel ? "> " : "  ", opt->text);
            drawText(textX, optY, buf, sel ? rgb(255, 255, 100) : rgb(200, 200, 200), 2);
        } else {
            snprintf(buf, sizeof(buf), "%s[Locked. %s %d required]",
                sel ? "> " : "  ",
                skillName(opt->requiredSkill), opt->requiredLevel);
            drawText(textX, optY, buf, sel ? rgb(120, 120, 160) : rgb(70, 70, 90), 2);
        }
        optY += optH;
    }
}
