/*
 * editor_hub.c — launcher menu for all Eternal Night editors
 *
 * Run from the repo root so child editors can find assets/data/.
 * Navigation: Up/Down=select  Enter=launch  Q=quit
 *
 * Editors that are not yet built show [not built] and cannot be launched.
 */

#include <ncurses.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SCROLL_MARGIN 3

/* Shift *scroll so sel stays at least SCROLL_MARGIN rows from either edge. */
static void scroll_to(int sel, int *scroll, int visible) {
    if (sel - *scroll < SCROLL_MARGIN)              *scroll = sel - SCROLL_MARGIN;
    if (sel - *scroll > visible - SCROLL_MARGIN - 1) *scroll = sel - visible + SCROLL_MARGIN + 1;
    if (*scroll < 0) *scroll = 0;
}

typedef enum { ENTRY_EDITOR, ENTRY_HEADER } EntryKind;

typedef struct {
    EntryKind   kind;
    const char *label;
    const char *binary;  /* relative to repo root; NULL for headers */
    const char *datafile;
} Entry;

static const Entry entries[] = {
    { ENTRY_HEADER, "-- World -----------------------------------------------", NULL, NULL },
    { ENTRY_EDITOR, "Map Editor",               "build/map_editor",               "assets/maps/*.bin" },

    { ENTRY_HEADER, "-- Combat ----------------------------------------------", NULL, NULL },
    { ENTRY_EDITOR, "Enemy Editor",             "build/enemy_editor",             "assets/data/enemies.dat" },
    { ENTRY_EDITOR, "Action Editor",            "build/action_editor",            "assets/data/actions.dat" },
    { ENTRY_EDITOR, "Log Message Editor",       "build/logmessage_editor",        "assets/data/log_messages.dat" },

    { ENTRY_HEADER, "-- Items & Loot ----------------------------------------", NULL, NULL },
    { ENTRY_EDITOR, "Item Editor",              "build/item_editor",              "assets/data/items.dat" },
    { ENTRY_EDITOR, "Shop Editor",              "build/shop_editor",              "assets/data/shops.dat" },
    { ENTRY_EDITOR, "Loot Table Editor",        "build/loottable_editor",         "assets/data/loottables.dat" },

    { ENTRY_HEADER, "-- NPCs & Story ----------------------------------------", NULL, NULL },
    { ENTRY_EDITOR, "NPC Editor",               "build/npc_editor",               "assets/data/npcs.dat" },
    { ENTRY_EDITOR, "Dialog Editor",            "build/dialog_editor",            "assets/data/dialog.dat" },
    { ENTRY_EDITOR, "Social Encounter Editor",  "build/social_encounter_editor",  "assets/data/social_encounters.dat" },
    { ENTRY_EDITOR, "Quest Editor",             "build/quest_editor",             "assets/data/quests.dat" },
    { ENTRY_EDITOR, "Ambient Editor",           "build/ambient_editor",           "assets/data/ambient.dat" },

    { ENTRY_HEADER, "-- Investigations --------------------------------------", NULL, NULL },
    { ENTRY_EDITOR, "Clue Editor",              "build/clue_editor",              "assets/data/clues.dat" },
    { ENTRY_EDITOR, "Investigation Editor",     "build/investigation_editor",     "assets/data/investigations.dat" },

    { ENTRY_HEADER, "-- Environmental Encounters ----------------------------", NULL, NULL },
    { ENTRY_EDITOR, "Env Encounter Editor",     "build/env_encounter_editor",     "assets/data/env_encounters.dat" },
    { ENTRY_EDITOR, "Hunt Encounter Editor",    "build/hunt_encounter_editor",    "assets/data/hunt_encounters.dat" },
    { ENTRY_EDITOR, "Camp Zone Editor",         "build/camp_zone_editor",         "assets/data/camp_zones.dat" },

    { ENTRY_HEADER, "-- Player ----------------------------------------------", NULL, NULL },
    { ENTRY_EDITOR, "Player Editor",            "build/player_editor",            "assets/data/player.dat" },

    { ENTRY_HEADER, "-- Audio -----------------------------------------------", NULL, NULL },
    { ENTRY_EDITOR, "Music Editor",             "build/music_editor",             "assets/music/*.mus" },
};

#define ENTRY_COUNT ((int)(sizeof(entries) / sizeof(entries[0])))

static int firstEditor(void) {
    for (int i = 0; i < ENTRY_COUNT; i++)
        if (entries[i].kind == ENTRY_EDITOR) return i;
    return 0;
}

static int nextEditor(int cur) {
    for (int i = cur + 1; i < ENTRY_COUNT; i++)
        if (entries[i].kind == ENTRY_EDITOR) return i;
    return cur;
}

static int prevEditor(int cur) {
    for (int i = cur - 1; i >= 0; i--)
        if (entries[i].kind == ENTRY_EDITOR) return i;
    return cur;
}

static void render(int sel, int scroll) {
    clear();
    int visible = LINES - 3;

    /* position indicator: which editor number is selected */
    int editor_idx = 0, editor_total = 0;
    for (int i = 0; i < ENTRY_COUNT; i++) {
        if (entries[i].kind != ENTRY_EDITOR) continue;
        if (i == sel) editor_idx = editor_total;
        editor_total++;
    }

    mvprintw(0, 0, "ETERNAL NIGHT - EDITOR HUB  [%d/%d]", editor_idx + 1, editor_total);
    mvprintw(1, 0, "Up/Down=select  Enter=launch  Q=quit");

    for (int i = scroll; i < ENTRY_COUNT && i < scroll + visible; i++) {
        int row = (i - scroll) + 3;
        const Entry *e = &entries[i];

        if (e->kind == ENTRY_HEADER) {
            mvprintw(row, 0, "%s", e->label);
            continue;
        }

        int built = (access(e->binary, X_OK) == 0);

        if (i == sel) attron(A_REVERSE);
        if (built)
            mvprintw(row, 4, "%-28s  %s", e->label, e->datafile);
        else
            mvprintw(row, 4, "%-28s  [not built]", e->label);
        if (i == sel) attroff(A_REVERSE);
    }

    refresh();
}

static void launch(int idx) {
    const Entry *e = &entries[idx];
    if (!e->binary) return;

    if (access(e->binary, X_OK) != 0) {
        /* show brief error inside ncurses, then return */
        clear();
        mvprintw(0, 0, "Not built: %s", e->binary);
        mvprintw(1, 0, "Run 'make <target>' from the repo root, then try again.");
        mvprintw(3, 0, "Press any key to return.");
        refresh();
        getch();
        return;
    }

    endwin();
    system(e->binary);
    /* reinitialise ncurses after child exits */
    refresh();
    clear();
}

int main(void) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    int sel    = firstEditor();
    int scroll = 0;

    while (1) {
        int visible = LINES - 3;
        scroll_to(sel, &scroll, visible);
        render(sel, scroll);
        int ch = getch();

        switch (ch) {
            case KEY_UP:   sel = prevEditor(sel); break;
            case KEY_DOWN: sel = nextEditor(sel); break;
            case '\n': case KEY_ENTER: launch(sel); break;
            case 'q': case 'Q': endwin(); return 0;
        }
    }
}
