/* tools/case_editor.c — ncurses editor for assets/data/cases.dat
 *
 * A case is an investigation arc spanning several scenes; its position on the
 * shared investigation graph persists in the save. Scenes point at a case via
 * InvestigationDef.caseId, and clues gate on ClueDef.minCaseState.
 *
 * Navigation:
 *   Up/Down = field   +/- = change   Enter = edit name
 *   Tab     = next case   E = new   D = delete   S = save   Q = quit
 */

#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CASE_DEF_MAX 12
#define CASE_FLAG_HIDDEN (1<<0)

#define ISTATE_COUNT 5
static const char *arcNames[ISTATE_COUNT] = {
    "Cold Trail", "Familiarizing", "Connecting", "Breakthrough", "Unraveled"
};

typedef struct {
    uint8_t id;
    char    name[24];
    uint8_t startState;
    uint8_t rewardQuest;
    uint8_t setFlag;
    uint8_t flags;
    uint8_t _pad[3];
} CaseDef;

typedef char _chk[(sizeof(CaseDef) == 32) ? 1 : -1];

static CaseDef cases[CASE_DEF_MAX];
static int  caseCount = 0;
static int  dirty     = 0;
static const char *outfile = "assets/data/cases.dat";

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) return;
    uint8_t n;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > CASE_DEF_MAX) n = CASE_DEF_MAX;
    caseCount = (int)fread(cases, sizeof(CaseDef), n, f);
    fclose(f);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)caseCount;
    fwrite(&n, 1, 1, f);
    fwrite(cases, sizeof(CaseDef), caseCount, f);
    fclose(f);
    dirty = 0;
}

static int editString(int row, int col, char *buf, int maxLen) {
    int len = 0; while (len < maxLen && buf[len]) len++;
    echo(); curs_set(1);
    while (1) {
        mvhline(row, col, ' ', maxLen + 1);
        mvprintw(row, col, "%.*s", len, buf);
        move(row, col + len);
        refresh();
        int ch = getch();
        if (ch == '\n' || ch == KEY_ENTER) break;
        if (ch == 27) { noecho(); curs_set(0); return 0; }
        if ((ch == KEY_BACKSPACE || ch == 127) && len > 0) { buf[--len] = '\0'; continue; }
        if (ch >= 32 && ch < 127 && len < maxLen - 1) { buf[len++] = (char)ch; buf[len] = '\0'; }
    }
    noecho(); curs_set(0);
    return 1;
}

enum { F_ID = 0, F_NAME, F_START, F_FLAG, F_QUEST, F_HIDDEN, F_TOTAL };

static const char *fieldNames[F_TOTAL] = {
    "ID",
    "Name",
    "Starting arc state",
    "World flag when Unraveled (FF=none)",
    "Reward quest (reserved, FF=none)",
    "Hidden until opened",
};

static void render(int idx, int sel, const char *status) {
    CaseDef *c = &cases[idx];
    clear();
    mvprintw(0, 0, "CASE EDITOR - %s (%d/%d)%s",
             c->name[0] ? c->name : "(unnamed)", idx + 1, caseCount, dirty ? " *" : "");
    mvprintw(1, 0, "Up/Down=field  +/-=change  Enter=name  Tab=next  E=new  D=del  S=save  Q=quit");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = 0; i < F_TOTAL; i++) {
        int row = i + 4;
        if (i == sel) attron(A_REVERSE);
        switch (i) {
            case F_ID:    mvprintw(row, 2, "%-38s  %d", fieldNames[i], c->id); break;
            case F_NAME:  mvprintw(row, 2, "%-38s  %s", fieldNames[i], c->name); break;
            case F_START:
                mvprintw(row, 2, "%-38s  %s", fieldNames[i],
                         c->startState < ISTATE_COUNT ? arcNames[c->startState] : "?");
                break;
            case F_FLAG:
                if (c->setFlag == 0xFF) mvprintw(row, 2, "%-38s  none", fieldNames[i]);
                else mvprintw(row, 2, "%-38s  %d", fieldNames[i], c->setFlag);
                break;
            case F_QUEST:
                if (c->rewardQuest == 0xFF) mvprintw(row, 2, "%-38s  none", fieldNames[i]);
                else mvprintw(row, 2, "%-38s  %d", fieldNames[i], c->rewardQuest);
                break;
            case F_HIDDEN:
                mvprintw(row, 2, "%-38s  %s", fieldNames[i],
                         (c->flags & CASE_FLAG_HIDDEN) ? "[X]" : "[ ]");
                break;
        }
        if (i == sel) attroff(A_REVERSE);
    }

    mvprintw(F_TOTAL + 6, 2, "Arc: Cold Trail -> Familiarizing -> Connecting -> Breakthrough -> Unraveled");
    mvprintw(F_TOTAL + 7, 2, "Scenes join a case via investigation_editor (caseId); clues gate on minCaseState.");
    refresh();
}

static void bump(CaseDef *c, int sel, int dir) {
    switch (sel) {
        case F_ID:    c->id = (uint8_t)(c->id + dir); break;
        case F_START: {
            int v = (int)c->startState + dir;
            if (v < 0) v = ISTATE_COUNT - 1;
            if (v >= ISTATE_COUNT) v = 0;
            c->startState = (uint8_t)v;
            break;
        }
        case F_FLAG:  c->setFlag     = (uint8_t)(c->setFlag + dir); break;
        case F_QUEST: c->rewardQuest = (uint8_t)(c->rewardQuest + dir); break;
        case F_HIDDEN: c->flags ^= CASE_FLAG_HIDDEN; break;
        default: return;
    }
    dirty = 1;
}

static void initCase(CaseDef *c, uint8_t id) {
    memset(c, 0, sizeof(*c));
    c->id          = id;
    c->setFlag     = 0xFF;
    c->rewardQuest = 0xFF;
}

int main(void) {
    load();
    if (caseCount == 0) { initCase(&cases[0], 0); caseCount = 1; }

    initscr(); noecho(); cbreak(); curs_set(0); keypad(stdscr, TRUE);

    int idx = 0, sel = 0;
    const char *status = NULL;
    while (1) {
        render(idx, sel, status);
        status = NULL;
        int ch = getch();
        if (ch == 'q' || ch == 'Q') { if (dirty) save(); break; }
        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--; break;
            case KEY_DOWN: if (sel < F_TOTAL - 1) sel++; break;
            case '\t':     idx = (idx + 1) % caseCount; sel = 0; break;
            case '+': case '=': bump(&cases[idx], sel,  1); break;
            case '-': case '_': bump(&cases[idx], sel, -1); break;
            case 's': case 'S': save(); status = "Saved."; break;
            case 'e': case 'E':
                if (caseCount < CASE_DEF_MAX) {
                    initCase(&cases[caseCount], (uint8_t)caseCount);
                    idx = caseCount++;
                    sel = 0; dirty = 1;
                }
                break;
            case 'd': case 'D':
                if (caseCount > 1) {
                    for (int i = idx; i < caseCount - 1; i++) cases[i] = cases[i + 1];
                    caseCount--;
                    if (idx >= caseCount) idx = caseCount - 1;
                    dirty = 1;
                }
                break;
            case '\n': case KEY_ENTER:
                if (sel == F_NAME && editString(F_NAME + 4, 42, cases[idx].name, 24))
                    dirty = 1;
                break;
        }
    }
    endwin();
    return 0;
}
