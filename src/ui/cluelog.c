#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include "cluelog.h"
#include "gfx.h"
#include "game.h"
#include "investigations.h"
#include "cases.h"
#include "enc_graph.h"
#include "player.h"

/* The journal is organised by case: an arc that spans several scenes and
   whose position persists in the save. Scenes with no case of their own are
   collected under "Loose ends". Only cases you have actually touched (any
   clue found, or the arc moved off its start) are listed. */

static GameState g_returnState;
static int       g_sel = 0;

/* Has the player found anything in this scene? */
static int sceneTouched(const InvestigationDef *inv) {
    for (int j = 0; j < inv->clueCount; j++)
        if (clueIsFound(inv->clueIds[j])) return 1;
    return 0;
}

static int caseTouched(const CaseDef *cd) {
    if (caseGetState(cd->id) != cd->startState) return 1;
    for (int i = 0; i < invDefCount; i++)
        if (invDefs[i].caseId == cd->id && sceneTouched(&invDefs[i])) return 1;
    return 0;
}

/* Journal rows: every touched case, then a Loose-ends row if needed. */
static int rowCount(void) {
    int n = 0;
    for (int i = 0; i < caseDefCount; i++)
        if (caseTouched(&caseDefs[i])) n++;
    for (int i = 0; i < invDefCount; i++)
        if (invDefs[i].caseId == 0xFF && sceneTouched(&invDefs[i])) { n++; break; }
    return n;
}

void clueLogOpen(GameState from) {
    g_returnState = from;
    g_sel         = 0;
    state         = STATE_CLUE_LOG;
}

void handleClueLogInput(int key) {
    int rows = rowCount();
    switch (key) {
        case VK_UP:
            if (g_sel > 0) g_sel--;
            break;
        case VK_DOWN:
            if (g_sel < rows - 1) g_sel++;
            break;
        case VK_ESCAPE: case 'L':
            state = g_returnState;
            break;
    }
}

/* Found clues of one scene, indented under its heading. Returns new y. */
static int drawSceneClues(const InvestigationDef *inv, int x, int y) {
    char buf[48];
    for (int j = 0; j < inv->clueCount; j++) {
        uint8_t cid = inv->clueIds[j];
        const ClueDef *c = clueGetDef(cid);
        if (!c || !clueIsFound(cid)) continue;

        int grayed = clueIsInvalidated(cid);
        uint32_t cc = grayed ? rgb(60, 55, 35) : rgb(160, 150, 80);
        int isKey = (inv->keyMask >> j) & 1;

        if (grayed) snprintf(buf, sizeof(buf), "  [~] %.36s", c->text);
        else        snprintf(buf, sizeof(buf), "%c %.42s", isKey ? '*' : ' ', c->text);

        drawText(x + 28, y, buf, cc, 1);
        y += 14;
        if (y > gfxHeight - 110) break;
    }
    return y;
}

void renderClueLog(void) {
    const int x  = 60, y0 = 55;
    const int LH = 18;
    char buf[64];
    int  y = y0;

    fillRect(40, 40, gfxWidth - 80, gfxHeight - 80, rgb(8, 7, 2));
    drawText(x, y, "CASE LOG", rgb(200, 180, 80), 2);
    y += LH + 8;

    int row = 0;

    /* ── Cases ──────────────────────────────────────────────────── */
    for (int i = 0; i < caseDefCount && y < gfxHeight - 110; i++) {
        const CaseDef *cd = &caseDefs[i];
        if (!caseTouched(cd)) continue;

        int sel = (row == g_sel);
        uint32_t hdrCol = sel ? rgb(255, 230, 80) : rgb(200, 180, 80);
        drawText(x, y, sel ? ">" : " ", hdrCol, 1);
        drawText(x + 12, y, cd->name, hdrCol, 1);

        /* Arc position, right-aligned in the heading row */
        const StateDef *st = encGraphState(ENC_IDX_INVESTIGATION, caseGetState(cd->id));
        if (st) {
            uint32_t sc = caseIsComplete(cd->id) ? rgb(120, 210, 120)
                        : sel                    ? rgb(210, 190, 110)
                        :                          rgb(130, 118, 60);
            drawText(x + 260, y, st->name, sc, 1);
        }
        y += LH;

        if (sel) {
            for (int s = 0; s < invDefCount && y < gfxHeight - 110; s++) {
                const InvestigationDef *inv = &invDefs[s];
                if (inv->caseId != cd->id)  continue;
                if (!sceneTouched(inv))     continue;
                drawText(x + 16, y, inv->name, rgb(140, 128, 70), 1);
                y += 14;
                y = drawSceneClues(inv, x, y);
            }
            y += 4;
        }
        row++;
    }

    /* ── Scenes with no case of their own ───────────────────────── */
    int anyLoose = 0;
    for (int i = 0; i < invDefCount; i++)
        if (invDefs[i].caseId == 0xFF && sceneTouched(&invDefs[i])) { anyLoose = 1; break; }

    if (anyLoose && y < gfxHeight - 110) {
        int sel = (row == g_sel);
        uint32_t hdrCol = sel ? rgb(255, 230, 80) : rgb(200, 180, 80);
        drawText(x, y, sel ? ">" : " ", hdrCol, 1);
        drawText(x + 12, y, "Loose ends", hdrCol, 1);
        y += LH;
        if (sel) {
            for (int s = 0; s < invDefCount && y < gfxHeight - 110; s++) {
                const InvestigationDef *inv = &invDefs[s];
                if (inv->caseId != 0xFF) continue;
                if (!sceneTouched(inv))  continue;
                drawText(x + 16, y, inv->name, rgb(140, 128, 70), 1);
                y += 14;
                y = drawSceneClues(inv, x, y);
            }
        }
        row++;
    }

    if (row == 0) {
        drawText(x, y, "  Nothing worth writing down yet.", rgb(80, 72, 35), 1);
    } else {
        int found = 0;
        for (int i = 0; i < clueDefCount; i++)
            if (clueIsFound(clueDefs[i].id)) found++;
        snprintf(buf, sizeof(buf), "%d clue%s recorded", found, found == 1 ? "" : "s");
        drawText(x, gfxHeight - 76, buf, rgb(70, 64, 32), 1);
    }

    drawText(x, gfxHeight - 60, "UP/DN: case   L/ESC: close", rgb(60, 55, 30), 1);
}
