/*
 * clue_editor.c — ncurses editor for assets/data/clues.dat
 *
 * Binary format: [1 byte count][N × 80 bytes ClueDef]
 *
 * Navigation:
 *   SCR_LIST  Up/Down=select  N=new  D=delete  Enter=edit  S=save  Q=quit
 *   SCR_EDIT  Up/Down=field   +/-=change numeric  Enter=edit text  T=toggle flag  Bksp/Q=back
 */

#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CLUE_DEF_MAX 64

#define CLUE_FLAG_KEY        (1<<0)
#define CLUE_FLAG_MISLEADING (1<<1)
#define CLUE_FLAG_LINGERING  (1<<2)

typedef struct {
    uint8_t id;
    char    text[32];
    char    itemHint[32];
    uint8_t difficulty;
    uint8_t domain;
    uint8_t actionAffinity[4];
    uint8_t requiredItem;
    uint8_t invalidates;
    uint8_t flags;
    uint8_t minCaseState;
    uint8_t _pad[5];
} ClueDef;

typedef char _check_size[(sizeof(ClueDef) == 80) ? 1 : -1];

#define SCROLL_MARGIN 3

static void scroll_to(int sel, int *scroll, int visible) {
    if (sel - *scroll < SCROLL_MARGIN)               *scroll = sel - SCROLL_MARGIN;
    if (sel - *scroll > visible - SCROLL_MARGIN - 1)  *scroll = sel - visible + SCROLL_MARGIN + 1;
    if (*scroll < 0) *scroll = 0;
}

static ClueDef      clues[CLUE_DEF_MAX];
static int          clueCount = 0;
static int          dirty     = 0;
static const char  *outfile   = "assets/data/clues.dat";

/* ---- file I/O ---- */

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) { clueCount = 0; return; }
    uint8_t n;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > CLUE_DEF_MAX) n = CLUE_DEF_MAX;
    clueCount = (int)fread(clues, sizeof(ClueDef), n, f);
    fclose(f);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)clueCount;
    fwrite(&n, 1, 1, f);
    fwrite(clues, sizeof(ClueDef), (size_t)clueCount, f);
    fclose(f);
    dirty = 0;
}

/* ---- inline text input ---- */

static int editString(int row, int col, char *buf, int maxLen) {
    int len = 0; while (len < maxLen - 1 && buf[len]) len++;
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

/* ---- screens ---- */

typedef enum { SCR_LIST, SCR_EDIT } Screen;

static Screen screen   = SCR_LIST;
static int    selClue  = 0;
static int    scrollC  = 0;
static int    selFld   = 0;

typedef enum {
    F_TEXT = 0, F_HINT,
    F_DIFF, F_DOMAIN,
    F_AFF0, F_AFF1, F_AFF2, F_AFF3,
    F_REQ_ITEM, F_INVALIDATES,
    F_FLAG_KEY, F_FLAG_MISLEADING, F_FLAG_LINGERING,
    F_COUNT
} Field;

static const char *fldLabels[] = {
    "Text", "Item hint",
    "Difficulty", "Domain",
    "Affinity[0]", "Affinity[1]", "Affinity[2]", "Affinity[3]",
    "Required item", "Invalidates",
    "Flag: KEY", "Flag: MISLEADING", "Flag: LINGERING",
};

/* ------ SCR_LIST ------ */

static void drawList(void) {
    clear();
    int visible = LINES - 3;
    scroll_to(selClue, &scrollC, visible);
    mvprintw(0, 0, "CLUE EDITOR  [%s]  [%d/%d]",
        dirty ? "unsaved" : "saved", clueCount > 0 ? selClue + 1 : 0, clueCount);
    mvprintw(1, 0, "Enter=edit  N=new  D=delete  S=save  Q=quit");
    mvprintw(2, 0, "  %-4s  %-5s  %-5s  %-34s  flags", "id", "diff", "dom", "text");

    for (int i = scrollC; i < clueCount && i < scrollC + visible; i++) {
        ClueDef *c = &clues[i];
        if (i == selClue) attron(A_REVERSE);
        char flags[16] = "";
        if (c->flags & CLUE_FLAG_KEY)        strcat(flags, "K");
        if (c->flags & CLUE_FLAG_MISLEADING) strcat(flags, "M");
        if (c->flags & CLUE_FLAG_LINGERING)  strcat(flags, "L");
        mvprintw((i - scrollC) + 3, 2, "[%2d]  d:%-3d  dom:%-3d  %-34s  %s",
            c->id, c->difficulty, c->domain, c->text, flags);
        if (i == selClue) attroff(A_REVERSE);
    }
    if (clueCount == 0)
        mvprintw(3, 2, "(no clues — press N to add one)");
}

static void handleList(int ch) {
    switch (ch) {
        case KEY_UP:   if (selClue > 0) selClue--; break;
        case KEY_DOWN: if (selClue < clueCount - 1) selClue++; break;
        case '\n': case KEY_ENTER:
            if (clueCount > 0) { selFld = 0; screen = SCR_EDIT; }
            break;
        case 'n': case 'N':
            if (clueCount < CLUE_DEF_MAX) {
                ClueDef *c = &clues[clueCount];
                memset(c, 0, sizeof(ClueDef));
                c->id          = (uint8_t)clueCount;
                c->difficulty  = 100;
                c->domain      = 0xFF;
                c->actionAffinity[0] = c->actionAffinity[1] = 0xFF;
                c->actionAffinity[2] = c->actionAffinity[3] = 0xFF;
                c->requiredItem = 0xFF;
                c->invalidates  = 0xFF;
                selClue = clueCount++;
                dirty = 1;
            }
            break;
        case 'd': case 'D':
            if (clueCount > 0) {
                for (int i = selClue; i < clueCount - 1; i++)
                    clues[i] = clues[i + 1];
                for (int i = 0; i < clueCount - 1; i++)
                    clues[i].id = (uint8_t)i;
                clueCount--;
                if (selClue >= clueCount && selClue > 0) selClue--;
                dirty = 1;
            }
            break;
        case 's': case 'S': save(); break;
    }
}

/* ------ SCR_EDIT ------ */

static void drawEdit(void) {
    clear();
    ClueDef *c = &clues[selClue];
    mvprintw(0, 0, "CLUE [%d]  %.32s  [%s]",
        c->id, c->text[0] ? c->text : "(unnamed)", dirty ? "unsaved" : "saved");
    mvprintw(1, 0, "Up/Down=field  +/-=change  Enter=edit text  T=toggle flag  Bksp/Q=back  S=save");

    for (int i = 0; i < F_COUNT; i++) {
        int row = 3 + i;
        if (i == selFld) attron(A_REVERSE);
        switch (i) {
            case F_TEXT:
                mvprintw(row, 2, "%-16s  %.32s", fldLabels[i], c->text); break;
            case F_HINT:
                mvprintw(row, 2, "%-16s  %.32s", fldLabels[i], c->itemHint); break;
            case F_DIFF:
                mvprintw(row, 2, "%-16s  %d  (0=trivial, 255=near-impossible)",
                    fldLabels[i], c->difficulty); break;
            case F_DOMAIN:
                if (c->domain == 0xFF)
                    mvprintw(row, 2, "%-16s  none (0xFF)", fldLabels[i]);
                else
                    mvprintw(row, 2, "%-16s  %d", fldLabels[i], c->domain);
                break;
            case F_AFF0: case F_AFF1: case F_AFF2: case F_AFF3: {
                int j = i - F_AFF0;
                if (c->actionAffinity[j] == 0xFF)
                    mvprintw(row, 2, "%-16s  none (0xFF)", fldLabels[i]);
                else
                    mvprintw(row, 2, "%-16s  %d", fldLabels[i], c->actionAffinity[j]);
                break;
            }
            case F_REQ_ITEM:
                if (c->requiredItem == 0xFF)
                    mvprintw(row, 2, "%-16s  none (0xFF)", fldLabels[i]);
                else
                    mvprintw(row, 2, "%-16s  %d", fldLabels[i], c->requiredItem);
                break;
            case F_INVALIDATES:
                if (c->invalidates == 0xFF)
                    mvprintw(row, 2, "%-16s  none (0xFF)", fldLabels[i]);
                else
                    mvprintw(row, 2, "%-16s  clue %d", fldLabels[i], c->invalidates);
                break;
            case F_FLAG_KEY:
                mvprintw(row, 2, "%-16s  %s  (T to toggle)",
                    fldLabels[i], (c->flags & CLUE_FLAG_KEY) ? "[X]" : "[ ]"); break;
            case F_FLAG_MISLEADING:
                mvprintw(row, 2, "%-16s  %s  (T to toggle)",
                    fldLabels[i], (c->flags & CLUE_FLAG_MISLEADING) ? "[X]" : "[ ]"); break;
            case F_FLAG_LINGERING:
                mvprintw(row, 2, "%-16s  %s  (T to toggle)",
                    fldLabels[i], (c->flags & CLUE_FLAG_LINGERING) ? "[X]" : "[ ]"); break;
        }
        if (i == selFld) attroff(A_REVERSE);
    }

    mvprintw(3 + F_COUNT + 1, 0,
        "KEY=must find to solve  MISLEADING=red herring  LINGERING=shown even on failure");
    mvprintw(3 + F_COUNT + 2, 0,
        "domain/affinity 0xFF=none  invalidates: grays out target clue in journal");
}

static void handleEdit(int ch) {
    ClueDef *c = &clues[selClue];
    switch (ch) {
        case KEY_UP:   if (selFld > 0) selFld--; break;
        case KEY_DOWN: if (selFld < F_COUNT - 1) selFld++; break;

        case '\n': case KEY_ENTER:
            if (selFld == F_TEXT) {
                if (editString(3 + F_TEXT, 20, c->text, 32)) dirty = 1;
            } else if (selFld == F_HINT) {
                if (editString(3 + F_HINT, 20, c->itemHint, 32)) dirty = 1;
            }
            break;

        case 't': case 'T':
            dirty = 1;
            switch (selFld) {
                case F_FLAG_KEY:        c->flags ^= CLUE_FLAG_KEY;        break;
                case F_FLAG_MISLEADING: c->flags ^= CLUE_FLAG_MISLEADING; break;
                case F_FLAG_LINGERING:  c->flags ^= CLUE_FLAG_LINGERING;  break;
                default: dirty = 0; break;
            }
            break;

        case '+': case '=':
            dirty = 1;
            switch (selFld) {
                case F_DIFF:   if (c->difficulty < 255) c->difficulty++;   break;
                case F_DOMAIN:
                    c->domain = (c->domain == 0xFF) ? 0 : (c->domain < 255 ? c->domain + 1 : 0xFF);
                    break;
                case F_AFF0: case F_AFF1: case F_AFF2: case F_AFF3: {
                    int j = selFld - F_AFF0;
                    c->actionAffinity[j] = (c->actionAffinity[j] == 0xFF) ? 0
                        : (c->actionAffinity[j] < 255 ? c->actionAffinity[j] + 1 : 0xFF);
                    break;
                }
                case F_REQ_ITEM:
                    c->requiredItem = (c->requiredItem == 0xFF) ? 0
                        : (c->requiredItem < 255 ? c->requiredItem + 1 : 0xFF);
                    break;
                case F_INVALIDATES:
                    c->invalidates = (c->invalidates == 0xFF) ? 0
                        : (c->invalidates < 255 ? c->invalidates + 1 : 0xFF);
                    break;
                default: dirty = 0; break;
            }
            break;

        case '-':
            dirty = 1;
            switch (selFld) {
                case F_DIFF:   if (c->difficulty > 0) c->difficulty--;   break;
                case F_DOMAIN:
                    c->domain = (c->domain == 0) ? 0xFF : (c->domain == 0xFF ? 254 : c->domain - 1);
                    break;
                case F_AFF0: case F_AFF1: case F_AFF2: case F_AFF3: {
                    int j = selFld - F_AFF0;
                    c->actionAffinity[j] = (c->actionAffinity[j] == 0) ? 0xFF
                        : (c->actionAffinity[j] == 0xFF ? 254 : c->actionAffinity[j] - 1);
                    break;
                }
                case F_REQ_ITEM:
                    c->requiredItem = (c->requiredItem == 0) ? 0xFF
                        : (c->requiredItem == 0xFF ? 254 : c->requiredItem - 1);
                    break;
                case F_INVALIDATES:
                    c->invalidates = (c->invalidates == 0) ? 0xFF
                        : (c->invalidates == 0xFF ? 254 : c->invalidates - 1);
                    break;
                default: dirty = 0; break;
            }
            break;

        case KEY_BACKSPACE: case 127: case 'q': case 'Q':
            screen = SCR_LIST; selFld = 0; break;
        case 's': case 'S': save(); break;
    }
}

/* ---- main ---- */

int main(void) {
    load();
    initscr(); cbreak(); noecho();
    keypad(stdscr, TRUE); curs_set(0);

    int running = 1;
    while (running) {
        switch (screen) {
            case SCR_LIST: drawList(); break;
            case SCR_EDIT: drawEdit(); break;
        }
        refresh();
        int ch = getch();
        if ((ch == 'q' || ch == 'Q') && screen == SCR_LIST) { running = 0; break; }
        switch (screen) {
            case SCR_LIST: handleList(ch); break;
            case SCR_EDIT: handleEdit(ch); break;
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
