#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include "cluelog.h"
#include "gfx.h"
#include "game.h"
#include "investigations.h"
#include "player.h"

static GameState g_returnState;
static int       g_sel = 0;  /* selected investigation index */

void clueLogOpen(GameState from) {
    g_returnState = from;
    g_sel         = 0;
    state         = STATE_CLUE_LOG;
}

void handleClueLogInput(int key) {
    /* Count investigations that have at least one found clue */
    int relevant = 0;
    for (int i = 0; i < invDefCount; i++) {
        for (int j = 0; j < invDefs[i].clueCount; j++)
            if (clueIsFound(invDefs[i].clueIds[j])) { relevant++; break; }
    }
    switch (key) {
        case VK_UP:
            if (g_sel > 0) g_sel--;
            break;
        case VK_DOWN:
            if (g_sel < relevant - 1) g_sel++;
            break;
        case VK_ESCAPE: case 'L':
            state = g_returnState;
            break;
    }
}

void renderClueLog(void) {
    const int x  = 60, y0 = 55;
    const int LH = 18;
    char buf[48];
    int  y = y0;

    fillRect(40, 40, gfxWidth - 80, gfxHeight - 80, rgb(8, 7, 2));
    drawText(x, y, "CASE LOG", rgb(200, 180, 80), 2);
    y += LH + 8;

    int caseIdx = 0;
    int anyCase = 0;

    for (int i = 0; i < invDefCount; i++) {
        const InvestigationDef *inv = &invDefs[i];

        /* Only show investigations with at least one found clue */
        int hasAny = 0;
        for (int j = 0; j < inv->clueCount; j++)
            if (clueIsFound(inv->clueIds[j])) { hasAny = 1; break; }
        if (!hasAny) continue;

        anyCase = 1;
        int sel = (caseIdx == g_sel);
        uint32_t hdrCol = sel ? rgb(255, 230, 80) : rgb(200, 180, 80);
        drawText(x, y, sel ? "> " : "  ", hdrCol, 1);
        drawText(x + 12, y, inv->name, hdrCol, 1);
        y += LH;

        /* Clue entries for the selected investigation */
        if (sel) {
            for (int j = 0; j < inv->clueCount; j++) {
                uint8_t cid = inv->clueIds[j];
                const ClueDef *c = clueGetDef(cid);
                if (!c) continue;
                if (!clueIsFound(cid)) continue;

                int grayed = clueIsInvalidated(cid);
                uint32_t cc = grayed ? rgb(60, 55, 35) : rgb(160, 150, 80);

                /* Show key/misleading marker */
                int localPos = j;
                int isKey = (inv->keyMask >> localPos) & 1;
                buf[0] = isKey ? '*' : ' '; buf[1] = ' ';
                int len = 0;
                while (c->text[len] && len < 42) len++;
                for (int k = 0; k < len; k++) buf[2 + k] = c->text[k];
                buf[2 + len] = '\0';

                if (grayed) {
                    /* Strikethrough feel: prepend marker */
                    snprintf(buf, sizeof(buf), "  [~] %.36s", c->text);
                }
                drawText(x + 16, y, buf, cc, 1);
                y += 14;

                if (y > gfxHeight - 100) goto done;
            }
            y += 4;
        }

        caseIdx++;
    }

    if (!anyCase)
        drawText(x, y, "  No clues recorded yet.", rgb(80, 72, 35), 1);

done:
    drawText(x, gfxHeight - 60, "UP/DN: case   L/ESC: close", rgb(60, 55, 30), 1);
    (void)buf;
}
