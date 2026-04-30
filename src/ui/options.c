#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "gfx.h"
#include "game.h"
#include "audio.h"
#include "options.h"

static GameState g_returnState;
static int       g_vol;
static int       g_row;

#define OPT_ROWS 7  /* fullscreen + volume + 5 CRT toggles */

void enterOptions(GameState from) {
    g_returnState = from;
    g_vol         = audioGetVolume();
    g_row         = 0;
    state         = STATE_OPTIONS;
}

static int *crtFlag(int row) {
    switch (row) {
        case 2: return &g_crtScanlines;
        case 3: return &g_crtGrid;
        case 4: return &g_crtBleed;
        case 5: return &g_crtBlur;
        case 6: return &g_crtVignette;
    }
    return NULL;
}

void handleOptionsInput(int key) {
    switch (key) {
        case VK_UP:
            g_row = (g_row + OPT_ROWS - 1) % OPT_ROWS;
            break;
        case VK_DOWN:
            g_row = (g_row + 1) % OPT_ROWS;
            break;
        case VK_LEFT:
            if      (g_row == 0) { gfxSetFullscreen(0); }
            else if (g_row == 1) { if (g_vol > 0)  { g_vol--; audioSetVolume(g_vol); } }
            else { int *f = crtFlag(g_row); if (f) *f = 0; }
            break;
        case VK_RIGHT:
            if      (g_row == 0) { gfxSetFullscreen(1); }
            else if (g_row == 1) { if (g_vol < 10) { g_vol++; audioSetVolume(g_vol); } }
            else { int *f = crtFlag(g_row); if (f) *f = 1; }
            break;
        case VK_RETURN:
            if      (g_row == 0) { gfxSetFullscreen(!gfxIsFullscreen()); }
            else if (g_row > 1)  { int *f = crtFlag(g_row); if (f) *f ^= 1; }
            break;
        case VK_ESCAPE:
        case 'O':
            state = g_returnState;
            break;
    }
}

void renderOptions(void) {
    const int cx    = gfxWidth  / 2;
    const int cy    = gfxHeight / 2;
    const int boxW  = 300;
    const int boxH  = 278;
    const int boxX  = cx - boxW / 2;
    const int boxY  = cy - boxH / 2;
    const int lineH = 28;
    const int tx    = boxX + 16;

    fillRect(boxX, boxY,          boxW, boxH, rgb(10, 10, 30));
    fillRect(boxX, boxY,          boxW,    1, rgb(80, 80, 160));
    fillRect(boxX, boxY+boxH-1,   boxW,    1, rgb(80, 80, 160));

    drawText(cx - 56, boxY + 10, "OPTIONS", rgb(220, 220, 255), 2);

    int y = boxY + 42;
    char buf[48];

    /* Fullscreen row */
    {
        int sel = (g_row == 0);
        int on  = gfxIsFullscreen();
        snprintf(buf, sizeof(buf), "%s%-13s [%s]",
                 sel ? "> " : "  ", "Fullscreen", on ? "ON " : "OFF");
        uint32_t color = sel ? rgb(255,255,100) : (on ? rgb(140,220,140) : rgb(130,130,130));
        drawText(tx, y, buf, color, 2);
        y += lineH;
    }

    /* Volume row */
    {
        int sel = (g_row == 1);
        uint32_t lc = sel ? rgb(255,255,100) : rgb(180,180,180);
        drawText(tx, y, sel ? "> Volume" : "  Volume", lc, 2);
        const int barX = tx + 140, barY = y + 2, barW = 96, barH = 12;
        fillRect(barX, barY, barW, barH, rgb(40, 40, 80));
        if (g_vol > 0) fillRect(barX, barY, g_vol * barW / 10, barH, rgb(100, 160, 255));
        snprintf(buf, sizeof(buf), "%d", g_vol);
        drawText(barX + barW + 6, barY - 2, buf, rgb(220, 220, 100), 2);
        y += lineH;
    }

    /* CRT toggle rows */
    static const char *labels[] = {
        "Scanlines", "Pixel Grid", "Color Bleed", "Blur", "Vignette"
    };
    for (int i = 2; i < OPT_ROWS; i++) {
        int sel = (g_row == i);
        int *f  = crtFlag(i);
        int on  = f ? *f : 0;
        snprintf(buf, sizeof(buf), "%s%-13s [%s]",
                 sel ? "> " : "  ", labels[i-2], on ? "ON " : "OFF");
        uint32_t color = sel ? rgb(255,255,100) : (on ? rgb(140,220,140) : rgb(130,130,130));
        drawText(tx, y, buf, color, 2);
        y += lineH;
    }

    drawText(tx, boxY + boxH - 16, "UP/DN  ENTER=toggle  ESC=back", rgb(65, 65, 65), 1);
}
