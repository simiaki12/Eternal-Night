/*
 * investigation_editor.c — ncurses editor for assets/data/investigations.dat
 *
 * Binary format: [1 byte count][N × 80 bytes InvestigationDef]
 *
 * Navigation:
 *   SCR_LIST  Up/Down=select  N=new  D=delete  Enter=edit  S=save  Q=quit
 *   SCR_EDIT  Up/Down=field   +/-=change numeric  Enter=edit text  T=toggle  Bksp/Q=back
 */

#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define INV_DEF_MAX    32
#define INV_CLUE_SLOTS  8

#define INV_FLAG_STAGED      (1<<0)
#define INV_FLAG_RETURNABLE  (1<<1)

static const char *pressureNames[] = {
    "Weather", "Crowd", "Darkness", "Decay"
};
#define PRESSURE_COUNT 4

typedef struct {
    uint8_t id;
    char    name[24];
    char    pressureText[36];
    uint8_t clueIds[INV_CLUE_SLOTS];
    uint8_t clueCount;
    uint8_t turnLimit;
    uint8_t keyMask;
    uint8_t rewardItem;
    uint8_t rewardQuest;
    uint8_t pressureType;
    uint8_t flags;
    uint8_t _pad[4];
} InvestigationDef;

typedef char _check_size[(sizeof(InvestigationDef) == 80) ? 1 : -1];

#define SCROLL_MARGIN 3

static void scroll_to(int sel, int *scroll, int visible) {
    if (sel - *scroll < SCROLL_MARGIN)               *scroll = sel - SCROLL_MARGIN;
    if (sel - *scroll > visible - SCROLL_MARGIN - 1)  *scroll = sel - visible + SCROLL_MARGIN + 1;
    if (*scroll < 0) *scroll = 0;
}

static InvestigationDef invs[INV_DEF_MAX];
static int              invCount = 0;
static int              dirty    = 0;
static const char      *outfile  = "assets/data/investigations.dat";

/* ---- file I/O ---- */

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) { invCount = 0; return; }
    uint8_t n;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > INV_DEF_MAX) n = INV_DEF_MAX;
    invCount = (int)fread(invs, sizeof(InvestigationDef), n, f);
    fclose(f);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)invCount;
    fwrite(&n, 1, 1, f);
    fwrite(invs, sizeof(InvestigationDef), (size_t)invCount, f);
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

static Screen screen  = SCR_LIST;
static int    selInv  = 0;
static int    scrollI = 0;
static int    selFld  = 0;

typedef enum {
    F_NAME = 0, F_PRESSURE_TEXT,
    F_CLUE0, F_CLUE1, F_CLUE2, F_CLUE3, F_CLUE4, F_CLUE5, F_CLUE6, F_CLUE7,
    F_CLUE_COUNT,
    F_TURN_LIMIT,
    F_KEY_MASK,
    F_REWARD_ITEM, F_REWARD_QUEST,
    F_PRESSURE_TYPE,
    F_FLAG_STAGED, F_FLAG_RETURNABLE,
    F_COUNT
} Field;

static const char *fldLabels[] = {
    "Name", "Pressure text",
    "Clue slot 0", "Clue slot 1", "Clue slot 2", "Clue slot 3",
    "Clue slot 4", "Clue slot 5", "Clue slot 6", "Clue slot 7",
    "Clue count",
    "Turn limit",
    "Key mask",
    "Reward item", "Reward quest",
    "Pressure type",
    "Flag: STAGED", "Flag: RETURNABLE",
};

/* ------ SCR_LIST ------ */

static void drawList(void) {
    clear();
    int visible = LINES - 3;
    scroll_to(selInv, &scrollI, visible);
    mvprintw(0, 0, "INVESTIGATION EDITOR  [%s]  [%d/%d]",
        dirty ? "unsaved" : "saved", invCount > 0 ? selInv + 1 : 0, invCount);
    mvprintw(1, 0, "Enter=edit  N=new  D=delete  S=save  Q=quit");
    mvprintw(2, 0, "  %-4s  %-5s  %-5s  %-24s  flags", "id", "turns", "clues", "name");

    for (int i = scrollI; i < invCount && i < scrollI + visible; i++) {
        InvestigationDef *inv = &invs[i];
        if (i == selInv) attron(A_REVERSE);
        char flags[16] = "";
        if (inv->flags & INV_FLAG_STAGED)     strcat(flags, "STG ");
        if (inv->flags & INV_FLAG_RETURNABLE) strcat(flags, "RET");
        mvprintw((i - scrollI) + 3, 2, "[%2d]  t:%-4d  c:%-4d  %-24s  %s",
            inv->id, inv->turnLimit, inv->clueCount, inv->name, flags);
        if (i == selInv) attroff(A_REVERSE);
    }
    if (invCount == 0)
        mvprintw(3, 2, "(no investigations — press N to add one)");
}

static void handleList(int ch) {
    switch (ch) {
        case KEY_UP:   if (selInv > 0) selInv--; break;
        case KEY_DOWN: if (selInv < invCount - 1) selInv++; break;
        case '\n': case KEY_ENTER:
            if (invCount > 0) { selFld = 0; screen = SCR_EDIT; }
            break;
        case 'n': case 'N':
            if (invCount < INV_DEF_MAX) {
                InvestigationDef *inv = &invs[invCount];
                memset(inv, 0, sizeof(InvestigationDef));
                inv->id          = (uint8_t)invCount;
                inv->turnLimit   = 6;
                inv->rewardItem  = 0xFF;
                inv->rewardQuest = 0xFF;
                for (int j = 0; j < INV_CLUE_SLOTS; j++)
                    inv->clueIds[j] = 0xFF;
                selInv = invCount++;
                dirty = 1;
            }
            break;
        case 'd': case 'D':
            if (invCount > 0) {
                for (int i = selInv; i < invCount - 1; i++)
                    invs[i] = invs[i + 1];
                for (int i = 0; i < invCount - 1; i++)
                    invs[i].id = (uint8_t)i;
                invCount--;
                if (selInv >= invCount && selInv > 0) selInv--;
                dirty = 1;
            }
            break;
        case 's': case 'S': save(); break;
    }
}

/* ------ SCR_EDIT ------ */

static void drawEdit(void) {
    clear();
    InvestigationDef *inv = &invs[selInv];
    mvprintw(0, 0, "INVESTIGATION [%d]  %.24s  [%s]",
        inv->id, inv->name[0] ? inv->name : "(unnamed)", dirty ? "unsaved" : "saved");
    mvprintw(1, 0, "Up/Down=field  +/-=change  Enter=edit text  T=toggle flag  Bksp/Q=back  S=save");

    /* Rebuild clueCount from non-0xFF slots */
    int autoCount = 0;
    for (int j = 0; j < INV_CLUE_SLOTS; j++)
        if (inv->clueIds[j] != 0xFF) autoCount++;

    for (int i = 0; i < F_COUNT; i++) {
        int row = 3 + i;
        if (i == selFld) attron(A_REVERSE);
        switch (i) {
            case F_NAME:
                mvprintw(row, 2, "%-16s  %.24s", fldLabels[i], inv->name); break;
            case F_PRESSURE_TEXT:
                mvprintw(row, 2, "%-16s  %.36s", fldLabels[i], inv->pressureText); break;
            case F_CLUE0: case F_CLUE1: case F_CLUE2: case F_CLUE3:
            case F_CLUE4: case F_CLUE5: case F_CLUE6: case F_CLUE7: {
                int j = i - F_CLUE0;
                int isKey = (inv->keyMask >> j) & 1;
                if (inv->clueIds[j] == 0xFF)
                    mvprintw(row, 2, "%-16s  none (0xFF)  key:%s",
                        fldLabels[i], isKey ? "Y" : "n");
                else
                    mvprintw(row, 2, "%-16s  %d  key:%s",
                        fldLabels[i], inv->clueIds[j], isKey ? "Y" : "n");
                break;
            }
            case F_CLUE_COUNT:
                mvprintw(row, 2, "%-16s  %d  (auto: %d non-0xFF slots)",
                    fldLabels[i], inv->clueCount, autoCount); break;
            case F_TURN_LIMIT:
                mvprintw(row, 2, "%-16s  %d", fldLabels[i], inv->turnLimit); break;
            case F_KEY_MASK:
                mvprintw(row, 2, "%-16s  0x%02X  (use +/- on Clue slot to toggle bit)",
                    fldLabels[i], inv->keyMask); break;
            case F_REWARD_ITEM:
                if (inv->rewardItem == 0xFF)
                    mvprintw(row, 2, "%-16s  none (0xFF)", fldLabels[i]);
                else
                    mvprintw(row, 2, "%-16s  %d", fldLabels[i], inv->rewardItem);
                break;
            case F_REWARD_QUEST:
                if (inv->rewardQuest == 0xFF)
                    mvprintw(row, 2, "%-16s  none (0xFF)", fldLabels[i]);
                else
                    mvprintw(row, 2, "%-16s  %d", fldLabels[i], inv->rewardQuest);
                break;
            case F_PRESSURE_TYPE:
                mvprintw(row, 2, "%-16s  %s (%d)",
                    fldLabels[i], pressureNames[inv->pressureType & 3], inv->pressureType);
                break;
            case F_FLAG_STAGED:
                mvprintw(row, 2, "%-16s  %s  (T to toggle)",
                    fldLabels[i], (inv->flags & INV_FLAG_STAGED) ? "[X]" : "[ ]"); break;
            case F_FLAG_RETURNABLE:
                mvprintw(row, 2, "%-16s  %s  (T to toggle)",
                    fldLabels[i], (inv->flags & INV_FLAG_RETURNABLE) ? "[X]" : "[ ]"); break;
        }
        if (i == selFld) attroff(A_REVERSE);
    }

    mvprintw(3 + F_COUNT + 1, 0,
        "Clue slot +/-: cycle clue ID (0xFF=none)  Bitmask: +/- on slot row toggles key bit");
    mvprintw(3 + F_COUNT + 2, 0,
        "STAGED=scene has misleading clues  RETURNABLE=player may re-enter after abandoning");
    mvprintw(3 + F_COUNT + 3, 0,
        "Set Clue count to match filled slots (or use auto value shown above)");
}

static void handleEdit(int ch) {
    InvestigationDef *inv = &invs[selInv];
    switch (ch) {
        case KEY_UP:   if (selFld > 0) selFld--; break;
        case KEY_DOWN: if (selFld < F_COUNT - 1) selFld++; break;

        case '\n': case KEY_ENTER:
            if (selFld == F_NAME) {
                if (editString(3 + F_NAME, 20, inv->name, 24)) dirty = 1;
            } else if (selFld == F_PRESSURE_TEXT) {
                if (editString(3 + F_PRESSURE_TEXT, 20, inv->pressureText, 36)) dirty = 1;
            }
            break;

        case 't': case 'T':
            dirty = 1;
            switch (selFld) {
                case F_FLAG_STAGED:     inv->flags ^= INV_FLAG_STAGED;     break;
                case F_FLAG_RETURNABLE: inv->flags ^= INV_FLAG_RETURNABLE; break;
                default: dirty = 0; break;
            }
            break;

        case '+': case '=':
            dirty = 1;
            if (selFld >= F_CLUE0 && selFld <= F_CLUE7) {
                int j = selFld - F_CLUE0;
                /* + on clue slot: if at end of range, wrap to 0xFF; else increment */
                if (inv->clueIds[j] == 0xFF) inv->clueIds[j] = 0;
                else if (inv->clueIds[j] < 255) inv->clueIds[j]++;
                else inv->clueIds[j] = 0xFF;
                /* Sync key mask toggle: pressing + when on a clue slot also toggles key bit */
                /* (users can use T for flags; leave + for cycling ID) */
            } else {
                switch (selFld) {
                    case F_CLUE_COUNT:
                        if (inv->clueCount < INV_CLUE_SLOTS) inv->clueCount++; break;
                    case F_TURN_LIMIT:
                        if (inv->turnLimit < 255) inv->turnLimit++; break;
                    case F_KEY_MASK:
                        if (inv->keyMask < 0xFF) inv->keyMask++; break;
                    case F_REWARD_ITEM:
                        inv->rewardItem = (inv->rewardItem == 0xFF) ? 0
                            : (inv->rewardItem < 255 ? inv->rewardItem + 1 : 0xFF);
                        break;
                    case F_REWARD_QUEST:
                        inv->rewardQuest = (inv->rewardQuest == 0xFF) ? 0
                            : (inv->rewardQuest < 255 ? inv->rewardQuest + 1 : 0xFF);
                        break;
                    case F_PRESSURE_TYPE:
                        inv->pressureType = (uint8_t)((inv->pressureType + 1) % PRESSURE_COUNT); break;
                    default: dirty = 0; break;
                }
            }
            break;

        case '-':
            dirty = 1;
            if (selFld >= F_CLUE0 && selFld <= F_CLUE7) {
                int j = selFld - F_CLUE0;
                if (inv->clueIds[j] == 0) inv->clueIds[j] = 0xFF;
                else if (inv->clueIds[j] == 0xFF) inv->clueIds[j] = 254;
                else inv->clueIds[j]--;
            } else {
                switch (selFld) {
                    case F_CLUE_COUNT:
                        if (inv->clueCount > 0) inv->clueCount--; break;
                    case F_TURN_LIMIT:
                        if (inv->turnLimit > 1) inv->turnLimit--; break;
                    case F_KEY_MASK:
                        if (inv->keyMask > 0) inv->keyMask--; break;
                    case F_REWARD_ITEM:
                        inv->rewardItem = (inv->rewardItem == 0) ? 0xFF
                            : (inv->rewardItem == 0xFF ? 254 : inv->rewardItem - 1);
                        break;
                    case F_REWARD_QUEST:
                        inv->rewardQuest = (inv->rewardQuest == 0) ? 0xFF
                            : (inv->rewardQuest == 0xFF ? 254 : inv->rewardQuest - 1);
                        break;
                    case F_PRESSURE_TYPE:
                        inv->pressureType = (uint8_t)((inv->pressureType + PRESSURE_COUNT - 1) % PRESSURE_COUNT);
                        break;
                    default: dirty = 0; break;
                }
            }
            break;

        /* K = toggle key bit for current clue slot */
        case 'k': case 'K':
            if (selFld >= F_CLUE0 && selFld <= F_CLUE7) {
                int j = selFld - F_CLUE0;
                inv->keyMask ^= (uint8_t)(1 << j);
                dirty = 1;
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
