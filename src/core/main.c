#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pak.h"
#include "player.h"
#include "items.h"
#include "gfx.h"
#include "game.h"
#include "enemies.h"
#include "encounter.h"
#include "town.h"
#include "dialog.h"
#include "world.h"
#include "domains.h"
#include "quests.h"
#include "mainmenu.h"
#include "pausemenu.h"
#include "save.h"
#include "loot.h"
#include "audio.h"
#include "options.h"
#include "npcs.h"
#include "shop.h"
#include "actions.h"
#include "ambient.h"
#include "log_messages.h"
#include "investigations.h"
#include "env_encounter.h"
#include "cluelog.h"

/* State machine */
GameState state     = STATE_MAIN_MENU;
GameState prevState = STATE_MAIN_MENU;

static int       g_musicStarted  = 0;
static GameState g_lastMusicState = STATE_MAIN_MENU;

/* Overlay states sit on top of the world and don't affect music. */
#define IS_OVERLAY(s) ((s)==STATE_INVENTORY||(s)==STATE_DOMAINS|| \
                       (s)==STATE_QUEST_LOG||(s)==STATE_CLUE_LOG||(s)==STATE_CHAR_SHEET|| \
                       (s)==STATE_PAUSE_MENU||(s)==STATE_DIALOG|| \
                       (s)==STATE_OPTIONS||(s)==STATE_SHOP)

/* --- Input --- */

static int  g_pendingKey  = 0;
static char g_pendingChar = 0;
static DWORD g_savedNotifyEnd = 0;
#define SAVED_NOTIFY_MS 2000


static int g_actPanel      = 0;
static int g_actPanelSel   = 0;
static int g_worldActPanel = 0;
static int g_worldActSel   = 0;

static void handleInventoryInput(int key) {
    if (g_actPanel) {
        /* panel open — collect actions for selected item */
        const ItemDef *d = inventory.count > 0 ? itemGetDef(inventory.items[inventory.selected]) : NULL;
        int cnt = 0;
        if (d) { for (int j = 0; j < 4; j++) if (d->actions[j] != 0xFF) cnt++; }
        switch (key) {
            case VK_UP:   if (g_actPanelSel > 0) g_actPanelSel--; break;
            case VK_DOWN: if (g_actPanelSel < cnt - 1) g_actPanelSel++; break;
            case VK_ESCAPE: case 'A': g_actPanel = 0; break;
        }
        return;
    }
    switch (key) {
        case VK_UP:
            inventory.selected--;
            if (inventory.selected < 0)
                inventory.selected = inventory.count > 0 ? inventory.count - 1 : 0;
            break;
        case VK_DOWN:
            if (inventory.count > 0)
                inventory.selected = (inventory.selected + 1) % inventory.count;
            break;
        case VK_RETURN:
            if (inventory.count > 0)
                useOrEquipItem(inventory.selected);
            break;
        case 'A':
            if (inventory.count > 0) {
                const ItemDef *d = itemGetDef(inventory.items[inventory.selected]);
                int hasAny = 0;
                if (d) for (int j = 0; j < 4; j++) if (d->actions[j] != 0xFF) { hasAny = 1; break; }
                if (hasAny) { g_actPanel = 1; g_actPanelSel = 0; }
            }
            break;
        case VK_ESCAPE:
            state = STATE_WORLD;
            break;
    }
}

static GameState g_charSheetReturn;

static void openCharSheet(GameState from) {
    g_charSheetReturn = from;
    state = STATE_CHAR_SHEET;
}

static void handleCharSheetInput(int key) {
    if (key == VK_ESCAPE || key == 'C')
        state = g_charSheetReturn;
}

static void handleLoadingInput(int key) {
    if(key==VK_ESCAPE)
        state = STATE_WORLD;
}

static void handleDungeonInput(int key) { if (key == VK_ESCAPE) state = STATE_WORLD; }

/* --- Render --- */

static void renderInventory(void) {
    int x = 60, y = 55;
    const int lineH = 22;
    char buf[48];
    int i;

    fillRect(40, 40, gfxWidth - 80, gfxHeight - 80, rgb(10, 20, 50));
    drawText(x, y, "INVENTORY", rgb(220, 220, 255), 2);
    y += lineH + 4;

    if (inventory.count == 0) {
        drawText(x, y, "  (empty)", rgb(120, 120, 120), 2);
    } else {
        for (i = 0; i < inventory.count; i++) {
            uint8_t id     = inventory.items[i];
            int     sel    = (i == inventory.selected);
            uint32_t color = sel ? rgb(255, 255, 100) : rgb(180, 180, 180);

            snprintf(buf, sizeof(buf), "%s%-10s", sel ? "> " : "  ", itemName(id));
            if (isEquipped(id)) {
                size_t len = strlen(buf);
                buf[len] = ' '; buf[len+1] = '('; buf[len+2] = 'E'; buf[len+3] = ')'; buf[len+4] = '\0';
            }
            drawText(x, y, buf, color, 2);
            y += lineH;
        }
    }

    /* Divider */
    int divY = y + 6;
    fillRect(x, divY, gfxWidth - 120, 1, rgb(60, 60, 100));
    y = divY + 10;

    /* Stats affected by selected item */
    if (inventory.count > 0) {
        uint8_t id       = inventory.items[inventory.selected];
        const ItemDef *d = itemGetDef(id);

        drawText(x, y, itemDesc(id), rgb(160, 200, 255), 2);
        y += lineH + 2;

        if (d) {
            #define STAT_ROW(label, stat, col) { \
                int cur = getStat(stat), pre = getStatPreview(stat, id); \
                if (pre != cur) snprintf(buf, sizeof(buf), label ": %d -> %d", cur, pre); \
                else            snprintf(buf, sizeof(buf), label ": %d", cur); \
                drawText(x, y, buf, col, 2); y += lineH; }

            if (d->attackBonus      || d->type == ITEM_WEAPON)    { STAT_ROW("ATK", STAT_ATK, rgb(220, 100, 100)) }
            if (d->defenseBonus     || d->type == ITEM_OUTFIT)    { STAT_ROW("DEF", STAT_DEF, rgb(100, 160, 220)) }
            if (d->hpBonus)                                        { STAT_ROW("MHP", STAT_MHP, rgb( 80, 200,  80)) }
            if (d->intelligenceBonus)                              { STAT_ROW("INT", STAT_INT, rgb(180, 100, 220)) }
            if (d->perceptionBonus)                                { STAT_ROW("PER", STAT_PER, rgb(220, 180, 100)) }
            if (d->staminaBonus)                                   { STAT_ROW("STA", STAT_STA, rgb(100, 220, 180)) }
            if (d->flags & ITEM_FLAG_HEAL) {
                int preHp = player.hp + 10;
                int cap   = getMaxHp();
                if (preHp > cap) preHp = cap;
                if (preHp != player.hp) snprintf(buf, sizeof(buf), "HP : %d -> %d", player.hp, preHp);
                else                    snprintf(buf, sizeof(buf), "HP : %d", player.hp);
                drawText(x, y, buf, rgb(100, 220, 100), 2); y += lineH;
            }
            #undef STAT_ROW

        }
    }

    /* Gold — always shown */
    if(player.gold <=1)
        snprintf(buf, sizeof(buf), "Solmark: %d ~", player.gold);
    else
        snprintf(buf, sizeof(buf), "Solmarks: %d ~", player.gold);
    drawText(x, gfxHeight - 80, buf, rgb(255, 215, 0), 2);

    drawText(x, gfxHeight - 56, "A: view actions", rgb(50, 60, 90), 1);

    /* Action panel overlay */
    if (g_actPanel && inventory.count > 0) {
        const ItemDef *d = itemGetDef(inventory.items[inventory.selected]);
        if (d) {
            uint8_t ids[4]; int cnt = 0;
            for (int j = 0; j < 4; j++) if (d->actions[j] != 0xFF) ids[cnt++] = d->actions[j];
            if (cnt > 0) renderActionPanel(d->name, ids, cnt, g_actPanelSel);
        }
    }
}

static void renderCharSheet(void) {
    const int x = 60, lineH = 22;
    int y = 55;
    char buf[48];

    fillRect(40, 40, gfxWidth - 80, gfxHeight - 80, rgb(10, 20, 50));
    drawText(x, y, "CHARACTER", rgb(220, 220, 255), 2);
    y += lineH + 8;

    #define CSTAT(label, val, col) \
        snprintf(buf, sizeof(buf), label ": %d", val); \
        drawText(x, y, buf, col, 2); y += lineH;

    snprintf(buf, sizeof(buf), "HP : %d / %d", player.hp, getMaxHp());
    drawText(x, y, buf, rgb(100, 220, 100), 2); y += lineH;
    CSTAT("ATK", getAttack(),       rgb(220, 100, 100))
    CSTAT("DEF", getDefense(),      rgb(100, 160, 220))
    y += 6;
    CSTAT("INT", player.intelligence, rgb(180, 100, 220))
    CSTAT("PER", player.perception,   rgb(220, 180, 100))
    CSTAT("STA", player.stamina,      rgb(100, 220, 180))
    CSTAT("CHA", player.charisma,     rgb(220, 160, 200))
    CSTAT("AGI", player.agility,      rgb(160, 220, 160))
    #undef CSTAT

    y += 2;
    fillRect(x, y, gfxWidth - 120, 1, rgb(60, 60, 100));
    y += 6;
    drawText(x, y, "DOMAINS", rgb(180, 180, 255), 2);
    y += lineH;

    for (int i = 0; i < 4; i++) {   /* show base domains only */
        const PlayerDomainState *d = &player.domains[i];
        snprintf(buf, sizeof(buf), "%-14s Lv.%d  %u/%u xp",
                 domainName(i), d->level,
                 (unsigned)d->xp,
                 (unsigned)domainXpToNext(d->level));
        drawText(x, y, buf, rgb(160, 180, 220), 2);
        y += lineH;
    }

    drawText(x, gfxHeight - 80, "C/ESC: close", rgb(80, 80, 80), 1);
}

static void handleDeathInput(int key) {

    if (key == VK_RETURN || key == VK_ESCAPE) 
    {
        returnToTown();
        startTown();
    }
}

static void renderDeath(void) {
    const int cx = gfxWidth  / 2;
    const int cy = gfxHeight / 2;
    const int scale = 2;
    const int lineH = 8 * scale + 6;

    fillRect(0, 0, gfxWidth, gfxHeight, rgb(15, 5, 5));
    fillRect(30, cy - 60, gfxWidth - 60, 120, rgb(30, 10, 10));
    fillRect(30, cy - 60, gfxWidth - 60, 2, rgb(120, 40, 40));
    fillRect(30, cy + 58, gfxWidth - 60, 2, rgb(120, 40, 40));

    drawText(cx - 120, cy - 48, "YOU HAVE FALLEN", rgb(200, 60, 60), scale);
    drawText(cx - 272, cy - 48 + lineH,
        "You have been knocked unconscious.", rgb(180, 160, 140), scale);
    drawText(cx - 288, cy - 48 + lineH * 2,
        "You wake up slowly at the healer in", rgb(180, 160, 140), scale);
    drawText(cx - 128, cy - 48 + lineH * 3,
        "the nearby town.", rgb(180, 160, 140), scale);
    drawText(cx - 92, cy - 48 + lineH * 4 + 4,
        "Press Enter to continue", rgb(100, 80, 60), 1);
}

static void renderDungeon(void) {
    /* Placeholder — dungeon system coming soon */
    fillRect(40, 40, gfxWidth - 80, gfxHeight - 80, rgb(80, 0, 80));
}

/* --- Win32 --- */

static int g_running = 1;
HWND       g_hwnd;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_KEYDOWN:
            g_pendingKey = (int)wp;
            return 0;
        case WM_CHAR:
            g_pendingChar = (char)wp;
            return 0;
        case WM_SIZE:
            gfxResize((int)LOWORD(lp), (int)HIWORD(lp));
            worldUpdateCamera();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            g_running = 0;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmdLine, int nCmdShow) {
    (void)hPrev; (void)cmdLine;

    srand((unsigned int)GetTickCount());

    if (!pakOpen("data.pak")) return 1;

    playerInit();

    PakData itemData    = pakRead("assets/data/items.dat");
    PakData enemyData   = pakRead("assets/data/enemies.dat");
    PakData dialogData    = pakRead("assets/data/dialog.dat");
    PakData questData     = pakRead("assets/data/quests.dat");
    PakData lootData      = pakRead("assets/data/loottables.dat");
    PakData npcData       = pakRead("assets/data/npcs.dat");
    PakData actionData    = pakRead("assets/data/actions.dat");
    PakData ambientData   = pakRead("assets/data/ambient.dat");
    PakData logMsgData    = pakRead("assets/data/log_messages.dat");
    PakData seData        = pakRead("assets/data/social_encounters.dat");
    PakData shopData      = pakRead("assets/data/shops.dat");
    PakData clueData      = pakRead("assets/data/clues.dat");
    PakData invData       = pakRead("assets/data/investigations.dat");
    PakData envEncData    = pakRead("assets/data/env_encounters.dat");

    loadItems(itemData);        /* optional — falls back to builtins if not in pak */
    loadEnemies(enemyData);     /* optional — falls back to builtins if not in pak */
    loadDialogs(dialogData);    /* optional — falls back to builtins if not in pak */
    loadQuests(questData);      /* optional — no quests if absent */
    loadLootTables(lootData);   /* optional — no item drops if absent */
    loadNpcs(npcData);          /* optional — no NPCs if absent */
    loadActions(actionData);      /* optional — falls back to builtins if not in pak */
    loadAmbient(ambientData);           /* optional — no ambient text if absent */
    loadLogMessages(logMsgData);        /* optional — no reactive messages if absent */
    loadSocialEncounters(seData);       /* optional — no social encounters if absent */
    loadShops(shopData);               /* optional — falls back to builtin shop 0 if absent */
    loadClues(clueData);               /* optional — no investigations if absent */
    loadInvestigations(invData);       /* optional — no investigations if absent */
    loadEnvEncounters(envEncData);     /* optional — no env encounters if absent */
    socialNewGame();            /* must run after loadNpcs + loadSocialEncounters */
    free(itemData.data);
    free(enemyData.data);
    free(dialogData.data);
    free(questData.data);
    free(lootData.data);
    free(npcData.data);
    free(actionData.data);
    free(ambientData.data);
    free(logMsgData.data);
    free(seData.data);
    free(shopData.data);
    free(clueData.data);
    free(invData.data);
    free(envEncData.data);

    const int screenW = 640;
    const int screenH = 480;
    const int monW    = GetSystemMetrics(SM_CXSCREEN);
    const int monH    = GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "EternalNight";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    g_hwnd = CreateWindowA(
        "EternalNight", "Eternal Night",
        WS_POPUP,
        0, 0, monW, monH,
        NULL, NULL, hInstance, NULL
    );

    gfxInit(g_hwnd, screenW, screenH);
    gfxResize(monW, monH);

    if (!worldLoadNamed("assets/maps/map1.bin")) { pakClose(); gfxShutdown(); return 1; }
    /* pak stays open for dynamic map loading during gameplay */

    ShowWindow(g_hwnd, nCmdShow);

#define TARGET_MS 16   /* ~62 FPS */

    timeBeginPeriod(1); /* 1ms timer resolution — makes Sleep accurate */

    MSG msg;
    while (g_running) {
        DWORD frameStart = GetTickCount();
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_running = 0; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (g_pendingKey) {
            /* F5 quick-saves from any in-game state */
            if (g_pendingKey == VK_F5 && state != STATE_MAIN_MENU) {
                if (saveGameNew())
                    g_savedNotifyEnd = GetTickCount() + SAVED_NOTIFY_MS;
            /* Escape closes world action panel before opening pause menu */
            } else if (g_pendingKey == VK_ESCAPE && g_worldActPanel && state == STATE_WORLD) {
                g_worldActPanel = 0;
            /* Escape opens pause from the main gameplay states */
            } else if (g_pendingKey == VK_ESCAPE &&
                       (state == STATE_WORLD || state == STATE_ENCOUNTER || state == STATE_TOWN)) {
                enterPauseMenu(state);
            /* Pause menu gets both key and char for the save-name form */
            } else if (state == STATE_PAUSE_MENU) {
                handlePauseMenuInput(g_pendingKey, g_pendingChar);
            /* A opens the action pool panel from the world */
            } else if (g_pendingKey == 'A' && state == STATE_WORLD) {
                if (g_worldActPanel) {
                    g_worldActPanel = 0;
                } else {
                    g_worldActPanel = 1; g_worldActSel = 0;
                }
            } else if (g_worldActPanel && state == STATE_WORLD) {
                uint8_t pool[64]; int cnt = buildActionPool(pool, ACT_CAT_ANY);
                if      (g_pendingKey == VK_UP   && g_worldActSel > 0)       g_worldActSel--;
                else if (g_pendingKey == VK_DOWN  && g_worldActSel < cnt - 1) g_worldActSel++;
                else if (g_pendingKey == 'F' && cnt > 0) toggleFavoured(pool[g_worldActSel]);
                else if (g_pendingKey == 'S' && cnt > 0) toggleSuppressed(pool[g_worldActSel]);
            /* P is a global hotkey — opens domain tree from world */
            } else if (g_pendingKey == 'P' && (state == STATE_WORLD || state == STATE_DOMAINS)) {
                state = (state == STATE_DOMAINS) ? STATE_WORLD : STATE_DOMAINS;
            /* J toggles the quest log from any non-encounter state */
            } else if (g_pendingKey == 'J' && state != STATE_ENCOUNTER) {
                if (state == STATE_QUEST_LOG) {
                    state = questLogSt.returnState;
                } else {
                    questLogSt.returnState = state;
                    questLogSt.sel = 0;
                    state = STATE_QUEST_LOG;
                }
            /* L toggles the clue log from any non-encounter state */
            } else if (g_pendingKey == 'L' && state != STATE_ENCOUNTER) {
                if (state == STATE_CLUE_LOG)
                    state = STATE_WORLD;
                else
                    clueLogOpen(state);
            /* C toggles the character sheet from any non-encounter state */
            } else if (g_pendingKey == 'C' && state != STATE_ENCOUNTER && state != STATE_MAIN_MENU && state != STATE_LOADING) {
                if (state == STATE_CHAR_SHEET)
                    state = g_charSheetReturn;
                else
                    openCharSheet(state);
            /* 1-5: test-fire encounter types from world */
            } else if (g_pendingKey >= '1' && g_pendingKey <= '5' && state == STATE_WORLD && enemyDefCount > 0) {
                static const EncounterType encTypes[5] = {
                    ENCOUNTER_COMBAT, ENCOUNTER_SOCIAL, ENCOUNTER_INVESTIGATION,
                    ENCOUNTER_ENVIRONMENTAL, ENCOUNTER_HUNT
                };
                encounterStart(encTypes[g_pendingKey - '1'], &enemyDefs[0], 0);
            } else {
                switch (state) {
                    case STATE_WORLD:   handleWorldInput(g_pendingKey);   break;
                    case STATE_ENCOUNTER:  handleEncounterInput(g_pendingKey);  break;
                    case STATE_MAIN_MENU: handleMainMenuInput(g_pendingKey); break;
                    case STATE_INVENTORY: handleInventoryInput(g_pendingKey); break;
                    case STATE_DOMAINS: handleDomainsInput(g_pendingKey); break;
                    case STATE_DIALOG:  handleDialogInput(g_pendingKey);  break;
                    case STATE_DUNGEON:   handleDungeonInput(g_pendingKey);   break;
                    case STATE_TOWN:     handleTownInput(g_pendingKey);      break;
                    case STATE_QUEST_LOG:  handleQuestLogInput(g_pendingKey); break;
                    case STATE_CLUE_LOG:   handleClueLogInput(g_pendingKey); break;
                    case STATE_DEATH:      handleDeathInput(g_pendingKey);   break;
                    case STATE_CHAR_SHEET: handleCharSheetInput(g_pendingKey); break;
                    case STATE_OPTIONS:     handleOptionsInput(g_pendingKey); break;
                    case STATE_SHOP:        handleShopInput(g_pendingKey);    break;
                    case STATE_PAUSE_MENU: break; /* handled above */
                    case STATE_LOADING: handleLoadingInput(g_pendingKey); break;
                }
            }
            g_pendingKey  = 0;
            g_pendingChar = 0;
        }

        if (state != STATE_WORLD) g_worldActPanel = 0;

        /* Music: switch on real state changes; overlays are transparent to music */
        if (!IS_OVERLAY(state) && (!g_musicStarted || state != g_lastMusicState)) {
            switch (state) {
                case STATE_MAIN_MENU: audioPlayMusic("assets/music/eternal_test.mus"); break;
                case STATE_WORLD:     audioPlayMusic("assets/music/hopes_and_dreams_eternal_night_ost.mus"); break;
                case STATE_TOWN:      audioPlayMusic("assets/music/eternal_town.mus"); break;
                case STATE_DUNGEON:   audioPlayMusic("assets/music/eternal_cave.mus"); break;
                case STATE_ENCOUNTER:    audioPlayMusic("assets/music/shining_star_eternal_night_ost.mus"); break;
                case STATE_DEATH:     audioPlayMusic("assets/music/over.mus"); break;
                default:              audioStop();                          break;
            }
            g_musicStarted   = 1;
            g_lastMusicState = state;
        }
        prevState = state;

        if (state == STATE_LOADING) updateLoading();
        if (state == STATE_WORLD)   updateWorld();

        clearScreen();
        switch (state) {
            case STATE_WORLD:
                renderWorld();
                if (g_worldActPanel) {
                    uint8_t pool[ACTION_MAX]; int cnt = buildActionPool(pool, ACT_CAT_ANY);
                    if (g_worldActSel >= cnt) g_worldActSel = cnt > 0 ? cnt - 1 : 0;
                    renderActionPanel("Your Actions", pool, cnt, g_worldActSel);
                }
                break;
            case STATE_ENCOUNTER:  renderEncounter();  break;
            case STATE_LOADING:   renderLoading();   break;
            case STATE_MAIN_MENU: renderMainMenu(); break;
            case STATE_INVENTORY: renderInventory(); break;
            case STATE_DOMAINS: renderDomains(); break;
            case STATE_DIALOG:  renderDialog();  break;
            case STATE_DUNGEON:   renderDungeon();   break;
            case STATE_TOWN:     renderTown();      break;
            case STATE_QUEST_LOG: renderQuestLog(); break;
            case STATE_CLUE_LOG:  renderClueLog();  break;
            case STATE_DEATH:      renderDeath();      break;
            case STATE_PAUSE_MENU: renderPauseMenu();  break;
            case STATE_CHAR_SHEET: renderCharSheet();  break;
            case STATE_OPTIONS:    renderOptions();    break;
            case STATE_SHOP:       renderShop();       break;
        }

        if (g_savedNotifyEnd && GetTickCount() < g_savedNotifyEnd)
            drawText(gfxWidth - 72, 8, "SAVED!", rgb(100, 255, 100), 1);
        renderQuestNotifications();

        gfxPresent(g_hwnd);

        DWORD frameMs = GetTickCount() - frameStart;
        if (frameMs < TARGET_MS) Sleep(TARGET_MS - frameMs);
    }

    timeEndPeriod(1);
    audioCleanup();
    pakClose();
    gfxShutdown();
    return 0;
}

