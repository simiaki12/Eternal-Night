/*
 * loottable_editor.c — ncurses editor for assets/loottables.dat
 *
 * Navigation:
 *   SCR_TABLES  Up/Down=select  N=new table  D=delete  Enter=open  S=save  Q=quit
 *   SCR_ENTRIES Up/Down=select  N=new entry  D=delete  Enter=edit  Bksp=back
 *   SCR_ENTRY   Up/Down=field   +/-=change   Bksp=back
 *
 * chance display: shows 0-255 and the approximate % alongside.
 * questId 255 = no requirement (shown as "---").
 * questStatus: 0=Inactive  1=Active  2=Done
 */

#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- mirror of src/loot.h ---- */
#define LOOT_TABLE_MAX  32
#define LOOT_ENTRY_MAX   8

typedef struct {
    uint8_t itemId;
    uint8_t chance;
    uint8_t questId;
    uint8_t questStatus;
} LootEntry;

typedef struct {
    LootEntry entries[LOOT_ENTRY_MAX];
    uint8_t   count;
} LootTable;
/* ------------------------------ */

static const char *questStatusNames[] = { "Inactive", "Active", "Done" };

#define SCROLL_MARGIN 3

static void scroll_to(int sel, int *scroll, int visible) {
    if (sel - *scroll < SCROLL_MARGIN)               *scroll = sel - SCROLL_MARGIN;
    if (sel - *scroll > visible - SCROLL_MARGIN - 1)  *scroll = sel - visible + SCROLL_MARGIN + 1;
    if (*scroll < 0) *scroll = 0;
}

static LootTable tables[LOOT_TABLE_MAX];
static int       tableCount = 0;
static int       dirty      = 0;
static const char *outfile  = "assets/data/loottables.dat";

/* ---- file I/O ---- */

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) { tableCount = 0; return; }

    uint8_t tc;
    if (fread(&tc, 1, 1, f) != 1) { fclose(f); return; }
    if (tc > LOOT_TABLE_MAX) tc = LOOT_TABLE_MAX;

    for (int t = 0; t < (int)tc; t++) {
        uint8_t ec;
        if (fread(&ec, 1, 1, f) != 1) break;
        if (ec > LOOT_ENTRY_MAX) ec = LOOT_ENTRY_MAX;
        tables[t].count = ec;
        if (fread(tables[t].entries, sizeof(LootEntry), ec, f) != ec) break;
        tableCount++;
    }
    fclose(f);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    fwrite(&(uint8_t){(uint8_t)tableCount}, 1, 1, f);
    for (int t = 0; t < tableCount; t++) {
        fwrite(&tables[t].count, 1, 1, f);
        fwrite(tables[t].entries, sizeof(LootEntry), tables[t].count, f);
    }
    fclose(f);
    dirty = 0;
}

/* ---- entry edit screen ---- */

typedef enum { EF_ITEM, EF_CHANCE, EF_QUEST, EF_QSTATUS, EF_COUNT } EntryField;
static const char *efNames[] = { "Item ID", "Chance", "Quest ID", "Quest Status" };

static void renderEntry(LootEntry *e, int sel, int tableIdx, int entryIdx) {
    clear();
    mvprintw(0, 0, "TABLE %d  ENTRY %d", tableIdx, entryIdx);
    mvprintw(1, 0, "Up/Down=field  +/-=change  Bksp=back  S=save");

    for (int i = 0; i < EF_COUNT; i++) {
        if (i == sel) attron(A_REVERSE);
        switch (i) {
            case EF_ITEM:
                mvprintw(i + 3, 2, "%-14s  %d", efNames[i], e->itemId);
                break;
            case EF_CHANCE:
                mvprintw(i + 3, 2, "%-14s  %3d  (~%d%%)", efNames[i],
                    e->chance, (int)((unsigned)e->chance * 100 / 255));
                break;
            case EF_QUEST:
                if (e->questId == 0xFF)
                    mvprintw(i + 3, 2, "%-14s  ---  (no requirement)", efNames[i]);
                else
                    mvprintw(i + 3, 2, "%-14s  %d", efNames[i], e->questId);
                break;
            case EF_QSTATUS:
                if (e->questId == 0xFF)
                    mvprintw(i + 3, 2, "%-14s  (n/a)", efNames[i]);
                else
                    mvprintw(i + 3, 2, "%-14s  %s", efNames[i],
                        questStatusNames[e->questStatus % 3]);
                break;
        }
        if (i == sel) attroff(A_REVERSE);
    }
    refresh();
}

static void screenEntry(int tableIdx, int entryIdx) {
    LootEntry *e = &tables[tableIdx].entries[entryIdx];
    int sel = 0;

    while (1) {
        renderEntry(e, sel, tableIdx, entryIdx);
        int ch = getch();
        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--; break;
            case KEY_DOWN: if (sel < EF_COUNT - 1) sel++; break;
            case KEY_BACKSPACE: case 127: return;
            case 's': case 'S': save(); break;

            case '+': case '=':
                dirty = 1;
                switch (sel) {
                    case EF_ITEM:    e->itemId++;   break;
                    case EF_CHANCE:  if (e->chance < 255) e->chance++; break;
                    case EF_QUEST:
                        if (e->questId == 0xFF) e->questId = 0;
                        else if (e->questId < 254) e->questId++;
                        break;
                    case EF_QSTATUS: e->questStatus = (e->questStatus + 1) % 3; break;
                }
                break;

            case '-':
                dirty = 1;
                switch (sel) {
                    case EF_ITEM:    if (e->itemId > 0) e->itemId--; break;
                    case EF_CHANCE:  if (e->chance > 0) e->chance--; break;
                    case EF_QUEST:
                        if (e->questId == 0) e->questId = 0xFF;
                        else if (e->questId != 0xFF) e->questId--;
                        break;
                    case EF_QSTATUS: e->questStatus = (e->questStatus + 2) % 3; break;
                }
                break;
        }
    }
}

/* ---- entry list screen ---- */

static void renderEntries(int tableIdx, int sel, int scroll, const char *status) {
    LootTable *t = &tables[tableIdx];
    clear();
    int visible = LINES - 4;
    mvprintw(0, 0, "TABLE %d  [%d/%d]  [%s]", tableIdx,
        t->count > 0 ? sel + 1 : 0, t->count, dirty ? "unsaved" : "saved");
    mvprintw(1, 0, "Up/Down=select  Enter=edit  N=new  D=delete  S=save  Bksp=back");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = scroll; i < t->count && i < scroll + visible; i++) {
        LootEntry *e = &t->entries[i];
        if (i == sel) attron(A_REVERSE);
        if (e->questId == 0xFF)
            mvprintw((i - scroll) + 4, 2, "%d  item=%-3d  chance=%3d (~%d%%)  quest=---",
                i, e->itemId, e->chance,
                (int)((unsigned)e->chance * 100 / 255));
        else
            mvprintw((i - scroll) + 4, 2, "%d  item=%-3d  chance=%3d (~%d%%)  quest=%d [%s]",
                i, e->itemId, e->chance,
                (int)((unsigned)e->chance * 100 / 255),
                e->questId, questStatusNames[e->questStatus % 3]);
        if (i == sel) attroff(A_REVERSE);
    }

    if (t->count == 0)
        mvprintw(4, 2, "(no entries — press N to add one)");

    refresh();
}

static void screenEntries(int tableIdx) {
    int sel = 0;
    int scroll = 0;
    const char *status = NULL;

    while (1) {
        LootTable *t = &tables[tableIdx];
        if (sel >= t->count && t->count > 0) sel = t->count - 1;
        if (sel < 0) sel = 0;
        scroll_to(sel, &scroll, LINES - 4);

        renderEntries(tableIdx, sel, scroll, status);
        status = NULL;
        int ch = getch();

        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--; break;
            case KEY_DOWN: if (sel < t->count - 1) sel++; break;
            case KEY_BACKSPACE: case 127: return;

            case '\n': case KEY_ENTER:
                if (t->count > 0) screenEntry(tableIdx, sel);
                break;

            case 'n': case 'N':
                if (t->count < LOOT_ENTRY_MAX) {
                    LootEntry *e = &t->entries[t->count++];
                    e->itemId      = 0;
                    e->chance      = 128;
                    e->questId     = 0xFF;
                    e->questStatus = 0;
                    sel = t->count - 1;
                    dirty = 1;
                    screenEntry(tableIdx, sel);
                } else {
                    status = "Max entries reached (8).";
                }
                break;

            case 'd': case 'D':
                if (t->count > 0) {
                    for (int i = sel; i < t->count - 1; i++)
                        t->entries[i] = t->entries[i + 1];
                    t->count--;
                    dirty = 1;
                    status = "Entry deleted.";
                }
                break;

            case 's': case 'S':
                save();
                status = "Saved.";
                break;
        }
    }
}

/* ---- table list screen ---- */

static void renderTables(int sel, int scroll, const char *status) {
    clear();
    int visible = LINES - 4;
    mvprintw(0, 0, "LOOT TABLES  [%s]  [%d/%d]", dirty ? "unsaved" : "saved",
        tableCount > 0 ? sel + 1 : 0, tableCount);
    mvprintw(1, 0, "Up/Down=select  Enter=open  N=new  D=delete  S=save  Q=quit");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = scroll; i < tableCount && i < scroll + visible; i++) {
        if (i == sel) attron(A_REVERSE);
        mvprintw((i - scroll) + 4, 2, "Table %-3d  %d entr%s",
            i, tables[i].count, tables[i].count == 1 ? "y" : "ies");
        if (i == sel) attroff(A_REVERSE);
    }

    if (tableCount == 0)
        mvprintw(4, 2, "(no tables — press N to add one)");

    refresh();
}

int main(void) {
    load();

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    int sel = 0;
    int scroll = 0;
    int running = 1;
    const char *status = NULL;

    while (running) {
        if (sel >= tableCount && tableCount > 0) sel = tableCount - 1;
        if (sel < 0) sel = 0;
        scroll_to(sel, &scroll, LINES - 4);

        renderTables(sel, scroll, status);
        status = NULL;
        int ch = getch();

        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--; break;
            case KEY_DOWN: if (sel < tableCount - 1) sel++; break;

            case '\n': case KEY_ENTER:
                if (tableCount > 0) screenEntries(sel);
                break;

            case 'n': case 'N':
                if (tableCount < LOOT_TABLE_MAX) {
                    tables[tableCount].count = 0;
                    sel = tableCount++;
                    dirty = 1;
                    screenEntries(sel);
                } else {
                    status = "Max tables reached (32).";
                }
                break;

            case 'd': case 'D':
                if (tableCount > 0) {
                    for (int i = sel; i < tableCount - 1; i++)
                        tables[i] = tables[i + 1];
                    tableCount--;
                    dirty = 1;
                    status = "Table deleted. Check enemy lootTableId references!";
                }
                break;

            case 's': case 'S':
                save();
                status = "Saved.";
                break;

            case 'q': case 'Q':
                running = 0;
                break;
        }
    }

    endwin();

    if (dirty) {
        printf("Unsaved changes. Save? (y/n): ");
        fflush(stdout);
        int ch = getchar();
        if (ch == 'y' || ch == 'Y') save();
    }

    return 0;
}
