#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "game.h"
#include "pak.h"
#include "save.h"
#include "mainmenu.h"
#include "options.h"
#include "player.h"

/* --- Loading screen --- */

static DWORD    g_loadingEnd = 0;
static PakData  g_loadingImg = {0};
static PakData  g_logoImg    = {0};

static const char *g_introSeq[]   = { "assets/ui/intro_01.bin", "assets/ui/intro_02.bin", "assets/ui/intro_03.bin", "assets/ui/intro_04.bin" , "assets/ui/intro_05.bin", "assets/ui/intro_06.bin" };
static const int   g_introCount   = 6;
static int         g_introIdx     = -1;
static DWORD       g_introDur     = 0;

static void loadIntroFrame(int idx) {
    if (g_loadingImg.data) { free(g_loadingImg.data); }
    g_loadingImg = pakRead((char*)g_introSeq[idx]);
    g_loadingEnd = GetTickCount() + g_introDur;
}

void startLoading(DWORD durationMs, char* target) {
    if (g_loadingImg.data) { free(g_loadingImg.data); }
    g_loadingImg = pakRead(target);
    g_loadingEnd = GetTickCount() + durationMs;
    g_introIdx   = -1;
    state = STATE_LOADING;
}

void startIntro(DWORD durationPerFrame) {
    g_introDur = durationPerFrame;
    g_introIdx = 0;
    loadIntroFrame(0);
    state = STATE_LOADING;
}

void updateLoading(void) {
    if (GetTickCount() < g_loadingEnd) return;

    if (g_introIdx >= 0 && g_introIdx < g_introCount - 1) {
        g_introIdx++;
        loadIntroFrame(g_introIdx);
        return;
    }

    free(g_loadingImg.data);
    g_loadingImg.data = NULL;
    g_introIdx = -1;
    state = STATE_WORLD;
}

void renderLoading(void) {
    fillRect(0, 0, gfxWidth, gfxHeight, rgb(0, 0, 0));
    drawBW(g_loadingImg.data, g_loadingImg.size, rgb(0, 0, 0));
}

/* --- Animated background --- */

#define BG_FRAMES 8
#define BG_MS     140

static PakData g_bgFrames[BG_FRAMES];
static int     g_bgLoaded   = 0;
static int     g_bgFrame    = 0;
static DWORD   g_bgNextTick = 0;

static void loadBgFrames(void) {
    char path[48];
    for (int i = 0; i < BG_FRAMES; i++) {
        snprintf(path, sizeof(path), "assets/ui/main%d.bin", i + 1);
        g_bgFrames[i] = pakRead(path);
    }
    g_bgLoaded = 1;
}

/* --- Top-level menu --- */

static int g_sel = 0;
#define MENU_ITEMS 4
static const char *g_labels[MENU_ITEMS] = {
    "New Game", "Load Game", "Options", "Exit"
};

/* --- Save browser --- */

static int  g_inBrowser  = 0;
static int  g_browserSel = 0;
static int  g_saveCount  = 0;
static char g_saveNames[MAX_SAVES][64];

static void enterBrowser(void) {
    g_saveCount  = listSaves(g_saveNames, MAX_SAVES);
    g_browserSel = 0;
    g_inBrowser  = 1;
}

static void handleBrowserInput(int key) {
    switch (key) {
        case VK_UP:
            if (g_browserSel > 0) g_browserSel--;
            break;
        case VK_DOWN:
            if (g_browserSel < g_saveCount - 1) g_browserSel++;
            break;
        case VK_RETURN:
            if (g_saveCount > 0 && loadGameNamed(g_saveNames[g_browserSel]))
            {
                g_inBrowser  = 0;
                g_browserSel = 0;
                state = STATE_WORLD;
            }
            break;
        case VK_ESCAPE:
            g_inBrowser = 0;
            break;
    }
}

static void renderBrowser(void) {
    const int cx     = gfxWidth  / 2;
    const int lineH  = 22;
    const int startY = gfxHeight / 2 - (g_saveCount * lineH) / 2;

    fillRect(0, 0, gfxWidth, gfxHeight, rgb(0, 0, 0));
    drawText(cx - 72, startY - 40, "LOAD GAME", rgb(220, 220, 255), 2);

    if (g_saveCount == 0) {
        drawText(cx - 80, gfxHeight / 2, "No saves found.", rgb(120, 120, 120), 2);
        return;
    }

    for (int i = 0; i < g_saveCount; i++) {
        int sel = (i == g_browserSel);
        uint32_t color = sel ? rgb(255, 255, 100) : rgb(180, 180, 180);
        char buf[72];
        snprintf(buf, sizeof(buf), "%s%s", sel ? "> " : "  ", g_saveNames[i]);
        int labelW  = (int)(strlen(g_saveNames[i]) * 8 * 2);
        int prefixW = 2 * 8 * 2;
        drawText(cx - labelW / 2 - prefixW, startY + i * lineH, buf, color, 2);
    }

    drawText(cx - 96, startY + g_saveCount * lineH + 12,
             "ESC to go back", rgb(80, 80, 80), 1);
}

/* --- Public --- */

void handleStartNewGame(void) {
    playerInit();
    startIntro(3000);
}

void handleMainMenuInput(int key) {
    if (g_inBrowser) { handleBrowserInput(key); return; }

    switch (key) {
        case VK_UP:
            g_sel = (g_sel + MENU_ITEMS - 1) % MENU_ITEMS;
            break;
        case VK_DOWN:
            g_sel = (g_sel + 1) % MENU_ITEMS;
            break;
        case VK_RETURN:
            switch (g_sel) {
                case 0: handleStartNewGame(); break;
                case 1: if (anySaveExists()) {enterBrowser(); g_sel=0;} break;
                case 2: enterOptions(STATE_MAIN_MENU); break; /* Options */
                case 3: PostQuitMessage(0); break;
            }
            break;
    }
}

void renderMainMenu(void) {
    if (g_inBrowser) { renderBrowser(); return; }

    if (!g_bgLoaded) loadBgFrames();

    DWORD now = GetTickCount();
    if (now >= g_bgNextTick) {
        g_bgFrame    = (g_bgFrame + 1) % BG_FRAMES;
        g_bgNextTick = now + BG_MS;
    }

    const int cx     = gfxWidth  / 2;
    const int lineH  = 24;
    const int startY = gfxHeight / 2 - (MENU_ITEMS * lineH) / 2;

    fillRect(0, 0, gfxWidth, gfxHeight, rgb(0, 0, 0));

    PakData *frame = &g_bgFrames[g_bgFrame];
    if (frame->data)
        drawBWAt(0, 0, gfxWidth, gfxHeight, frame->data, frame->size,
                 rgb(0,0,0), rgb(200,100,100));

    if (!g_logoImg.data) g_logoImg = pakRead("assets/ui/logo.bin");
    drawBWAt(cx - 120, startY - 48, 240, 48, g_logoImg.data, g_logoImg.size, rgb(255, 0, 0), rgb(0,0,0));
//    drawText(cx - 72, startY - 48, "TINY HERO", rgb(220, 220, 255), 2);

    int hasSave = anySaveExists();
    for (int i = 0; i < MENU_ITEMS; i++) {
        int sel      = (i == g_sel);
        int disabled = (i == 1 && !hasSave);
        uint32_t color = disabled ? rgb(80, 80, 80) :
                         sel      ? rgb(255, 255, 100) : rgb(255, 255, 255);
        char buf[32];
        snprintf(buf, sizeof(buf), "%s%s", sel ? "> " : "  ", g_labels[i]);
        int labelW  = (int)(strlen(g_labels[i]) * 8 * 2);
        int prefixW = 2 * 8 * 2;
        drawText(cx - labelW / 2 - prefixW, startY + i * lineH, buf, color, 2);
    }
}
