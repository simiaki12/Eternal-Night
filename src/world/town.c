#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "town.h"
#include "dialog.h"
#include "game.h"
#include "player.h"
#include "gfx.h"
#include "shop.h"
#include "items.h"
#include <stdio.h>

TownState townSt;

static const char *townOptions[] = { "Talk", "Rest", "Shop", "Leave" };
#define TOWN_OPTION_COUNT 4

static DWORD g_restMsgEnd = 0;

void startTown(void) {
    townSt.selected = 0;
    state = STATE_TOWN;
}

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
                case 0: startDialog(0, STATE_TOWN); break;
                case 1:
                    player.hp    = (uint16_t)getMaxHp();
                    g_restMsgEnd = GetTickCount() + 2500;
                    break;
                case 2: enterShop(0, STATE_TOWN); break;
                case 3: state = STATE_WORLD;   break;
            }
            break;
        case VK_ESCAPE:
            state = STATE_WORLD;
            break;
    }
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
