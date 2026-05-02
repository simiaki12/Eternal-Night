/*
 * ambient_editor.c — ncurses editor for ambient.dat
 *
 * Binary format:
 *   [1 byte]       uint8_t count
 *   [N × 80 bytes] AmbientEntry structs
 *
 * Navigation:
 *   SCR_LIST  → SCR_ENTRY (Enter)
 *   SCR_ENTRY → SCR_LIST  (Bksp / Q)
 */

#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_AMBIENT 128

#define AMBIENT_ALWAYS       0
#define AMBIENT_ONCE_SESSION 1
#define AMBIENT_ONCE_EVER    2

#define QUEST_INACTIVE 0
#define QUEST_ACTIVE   1
#define QUEST_DONE     2

typedef struct {
    char    mapId[8];
    uint8_t leftX, rightX, topY, bottomY;
    uint8_t flags;
    uint8_t questIdx;
    uint8_t questStatus;
    uint8_t _pad[1];
    char    text[64];
} AmbientEntry; /* 80 bytes */

static const char *outfile = "assets/data/ambient.dat";

static AmbientEntry entries[MAX_AMBIENT];
static int          entryCount = 0;
static int          dirty      = 0;

static const char *flagNames[] = { "Always", "Once/Session", "Once/Ever" };
static const char *statusNames[] = { "Inactive", "Active", "Done" };

/* ---- file I/O ---- */

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) { entryCount = 0; return; }
    uint8_t n = 0;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > MAX_AMBIENT) n = MAX_AMBIENT;
    size_t got = fread(entries, sizeof(AmbientEntry), n, f);
    entryCount = (int)got;
    fclose(f);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)entryCount;
    fwrite(&n, 1, 1, f);
    fwrite(entries, sizeof(AmbientEntry), (size_t)entryCount, f);
    fclose(f);
    dirty = 0;
}

/* ---- text editing ---- */

static void editStr(int row, int col, char *buf, int maxLen) {
    int len = (int)strlen(buf);
    int pos = len;
    curs_set(1);
    for (;;) {
        mvhline(row, col, ' ', maxLen + 1);
        mvprintw(row, col, "%s", buf);
        move(row, col + pos);
        refresh();

        int ch = getch();
        if (ch == '\n' || ch == KEY_ENTER) break;
        if (ch == 27) break;
        if ((ch == KEY_BACKSPACE || ch == 127 || ch == '\b') && pos > 0) {
            memmove(buf + pos - 1, buf + pos, (size_t)(len - pos + 1));
            pos--; len--;
        } else if (ch >= 32 && ch < 127 && len < maxLen - 1) {
            memmove(buf + pos + 1, buf + pos, (size_t)(len - pos + 1));
            buf[pos] = (char)ch;
            pos++; len++;
        } else if (ch == KEY_LEFT && pos > 0) {
            pos--;
        } else if (ch == KEY_RIGHT && pos < len) {
            pos++;
        }
    }
    curs_set(0);
}

/* ---- screens ---- */

#define SCR_LIST  0
#define SCR_ENTRY 1

static int screen   = SCR_LIST;
static int selEntry = 0;
static int selField = 0;

#define ENTRY_FIELD_COUNT 9

/* SCR_LIST */

static void drawList(void) {
    clear();
    mvprintw(0, 0, "AMBIENT TEXT  [%s]", dirty ? "unsaved" : "saved");
    mvprintw(1, 0, "Up/Down=select  Enter=edit  A=add  D=delete  S=save  Q=quit");

    if (entryCount == 0) {
        mvprintw(3, 2, "(no entries — press A to add one)");
        return;
    }

    for (int i = 0; i < entryCount; i++) {
        AmbientEntry *e = &entries[i];
        if (i == selEntry) attron(A_REVERSE);
        char truncated[32];
        strncpy(truncated, e->text, 31);
        truncated[31] = '\0';
        mvprintw(3 + i, 2, "[%3d] %-8s (%2d-%2d, %2d-%2d) %-12s  %s",
            i, e->mapId, e->leftX, e->rightX, e->topY, e->bottomY,
            flagNames[e->flags <= AMBIENT_ONCE_EVER ? e->flags : 0],
            truncated);
        if (i == selEntry) attroff(A_REVERSE);
    }
}

static void handleList(int ch) {
    switch (ch) {
        case KEY_UP:
            if (selEntry > 0) selEntry--;
            break;
        case KEY_DOWN:
            if (selEntry < entryCount - 1) selEntry++;
            break;
        case '\n': case KEY_ENTER:
            if (entryCount > 0) {
                selField = 0;
                screen = SCR_ENTRY;
            }
            break;
        case 'a': case 'A':
            if (entryCount < MAX_AMBIENT) {
                AmbientEntry *e = &entries[entryCount];
                memset(e, 0, sizeof(*e));
                e->questIdx = 0xFF;
                selEntry = entryCount;
                entryCount++;
                selField = 0;
                screen = SCR_ENTRY;
                dirty = 1;
            }
            break;
        case 'd': case 'D':
            if (entryCount > 0) {
                for (int k = selEntry; k < entryCount - 1; k++)
                    entries[k] = entries[k + 1];
                entryCount--;
                if (selEntry >= entryCount && selEntry > 0) selEntry--;
                dirty = 1;
            }
            break;
        case 's': case 'S': save(); break;
    }
}

/* SCR_ENTRY */

static const char *fieldLabels[ENTRY_FIELD_COUNT] = {
    "Map ID", "Left X", "Right X", "Top Y", "Bottom Y", "Flags", "Quest Idx", "Quest Status", "Text"
};

static void drawEntry(void) {
    clear();
    AmbientEntry *e = &entries[selEntry];
    mvprintw(0, 0, "AMBIENT ENTRY #%d  [%s]", selEntry, dirty ? "unsaved" : "saved");
    mvprintw(1, 0, "Up/Down=field  +/-=change  E=edit text/mapId  N=toggle none (Quest Idx)  Bksp/Q=back  S=save");

    for (int i = 0; i < ENTRY_FIELD_COUNT; i++) {
        int row = 3 + i;
        if (i == selField) attron(A_REVERSE);
        switch (i) {
            case 0:
                mvprintw(row, 2, "%-14s: %s", fieldLabels[i], e->mapId);
                break;
            case 1:
                mvprintw(row, 2, "%-14s: %d", fieldLabels[i], e->leftX);
                break;
            case 2:
                mvprintw(row, 2, "%-14s: %d", fieldLabels[i], e->rightX);
                break;
            case 3:
                mvprintw(row, 2, "%-14s: %d", fieldLabels[i], e->topY);
                break;
            case 4:
                mvprintw(row, 2, "%-14s: %d", fieldLabels[i], e->bottomY);
                break;
            case 5:
                mvprintw(row, 2, "%-14s: %s (%d)",
                    fieldLabels[i],
                    flagNames[e->flags <= AMBIENT_ONCE_EVER ? e->flags : 0],
                    e->flags);
                break;
            case 6:
                if (e->questIdx == 0xFF)
                    mvprintw(row, 2, "%-14s: none (0xFF)", fieldLabels[i]);
                else
                    mvprintw(row, 2, "%-14s: %d", fieldLabels[i], e->questIdx);
                break;
            case 7:
                if (e->questIdx == 0xFF)
                    mvprintw(row, 2, "%-14s: (no quest condition)", fieldLabels[i]);
                else
                    mvprintw(row, 2, "%-14s: %s (%d)",
                        fieldLabels[i],
                        statusNames[e->questStatus <= QUEST_DONE ? e->questStatus : 0],
                        e->questStatus);
                break;
            case 8:
                mvprintw(row, 2, "%-14s: %s", fieldLabels[i], e->text);
                break;
        }
        if (i == selField) attroff(A_REVERSE);
    }

    mvprintw(13, 0, "Flags: 0=Always (fires every step)  1=Once/Session  2=Once/Ever");
    mvprintw(14, 0, "Quest: set Quest Idx to a quest number to only show when that quest");
    mvprintw(15, 0, "       has the required status. 0xFF = no condition.");
}

static void handleEntry(int ch) {
    AmbientEntry *e = &entries[selEntry];

    switch (ch) {
        case KEY_UP:   if (selField > 0) selField--; break;
        case KEY_DOWN: if (selField < ENTRY_FIELD_COUNT - 1) selField++; break;

        case 'e': case 'E':
            dirty = 1;
            if (selField == 0)
                editStr(3, 18, e->mapId, 8);
            else if (selField == 8)
                editStr(11, 18, e->text, 64);
            break;

        case '\n': case KEY_ENTER:
            dirty = 1;
            if (selField == 0)
                editStr(3, 18, e->mapId, 8);
            else if (selField == 8)
                editStr(11, 18, e->text, 64);
            break;

        case '+': case '=':
            dirty = 1;
            switch (selField) {
                case 1: if (e->leftX < 255) e->leftX++; break;
                case 2: if (e->rightX < 255) e->rightX++; break;
                case 3: if (e->topY < 255) e->topY++; break;
                case 4: if (e->bottomY < 255) e->bottomY++; break;
                case 5: e->flags = (uint8_t)((e->flags + 1) % 3); break;
                case 6:
                    if (e->questIdx < 127) e->questIdx++;
                    break;
                case 7: if (e->questStatus < QUEST_DONE) e->questStatus++; break;
            }
            break;

        case '-':
            dirty = 1;
            switch (selField) {
                case 1: if (e->leftX > 0) e->leftX--; break;
                case 2: if (e->rightX > 0) e->rightX--; break;
                case 3: if (e->topY > 0) e->topY--; break;
                case 4: if (e->bottomY > 0) e->bottomY--; break;
                case 5: e->flags = (uint8_t)((e->flags + 2) % 3); break;
                case 6:
                    if (e->questIdx != 0xFF && e->questIdx > 0) e->questIdx--;
                    break;
                case 7: if (e->questStatus > QUEST_INACTIVE) e->questStatus--; break;
            }
            break;

        case 'n': case 'N':
            if (selField == 6) {
                e->questIdx = (e->questIdx == 0xFF) ? 0 : 0xFF;
                dirty = 1;
            }
            break;

        case KEY_BACKSPACE: case 127: case 'q': case 'Q':
            screen = SCR_LIST;
            selField = 0;
            break;

        case 's': case 'S': save(); break;
    }
}

/* ---- main ---- */

int main(void) {
    load();

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    int running = 1;
    while (running) {
        switch (screen) {
            case SCR_LIST:  drawList();  break;
            case SCR_ENTRY: drawEntry(); break;
        }
        refresh();

        int ch = getch();

        if ((ch == 'q' || ch == 'Q') && screen == SCR_LIST) {
            running = 0;
            break;
        }

        switch (screen) {
            case SCR_LIST:  handleList(ch);  break;
            case SCR_ENTRY: handleEntry(ch); break;
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
