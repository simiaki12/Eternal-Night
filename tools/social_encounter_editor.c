/*
 * social_encounter_editor.c — ncurses editor for assets/data/social_encounters.dat
 *
 * Binary format: [1 byte count][N × 8 bytes SocialEncounterDef]
 *
 * Navigation:
 *   SCR_LIST  Up/Down=select  N=new  D=delete  Enter=edit  S=save  Q=quit
 *   SCR_EDIT  Up/Down=field   +/-=change numeric  T=toggle flag  Bksp/Q=back
 */

#include <ncurses.h>
#include "refs.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Mirror of src/gameplay/encounter.h */
#define SE_DEF_MAX 64

#define SE_FLAG_ONE_SHOT      (1<<0)
#define SE_FLAG_DISP_OVERRIDE (1<<1)
#define SE_FLAG_HIDDEN        (1<<2)

typedef struct {
    uint16_t flags;
    uint8_t  id;
    uint8_t  npc_id;
    uint8_t  reward_partial;
    uint8_t  reward_full;
    uint8_t  disp_start;
    uint8_t  _pad;
} SocialEncounterDef;

typedef char _check_se_size[(sizeof(SocialEncounterDef) == 8) ? 1 : -1];

#define SCROLL_MARGIN 3

static void scroll_to(int sel, int *scroll, int visible) {
    if (sel - *scroll < SCROLL_MARGIN)               *scroll = sel - SCROLL_MARGIN;
    if (sel - *scroll > visible - SCROLL_MARGIN - 1)  *scroll = sel - visible + SCROLL_MARGIN + 1;
    if (*scroll < 0) *scroll = 0;
}

static SocialEncounterDef ses[SE_DEF_MAX];
static int                seCount = 0;
static int                dirty   = 0;
static const char        *outfile = "assets/data/social_encounters.dat";

/* ---- file I/O ---- */

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) { seCount = 0; return; }
    uint8_t n;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > SE_DEF_MAX) n = SE_DEF_MAX;
    seCount = (int)fread(ses, sizeof(SocialEncounterDef), n, f);
    fclose(f);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)seCount;
    fwrite(&n, 1, 1, f);
    fwrite(ses, sizeof(SocialEncounterDef), (size_t)seCount, f);
    fclose(f);
    dirty = 0;
}

/* ---- screens ---- */

typedef enum { SCR_LIST, SCR_EDIT } Screen;

static Screen screen    = SCR_LIST;
static int    selSe     = 0;
static int    scrollSe  = 0;
static int    selFld    = 0;

/* Fields in SCR_EDIT */
#define FLD_NPC_ID         0
#define FLD_REWARD_PARTIAL 1
#define FLD_REWARD_FULL    2
#define FLD_DISP_START     3
#define FLD_ONE_SHOT       4
#define FLD_DISP_OVERRIDE  5
#define FLD_HIDDEN         6
#define FLD_COUNT          7

static const char *fldLabels[] = {
    "NPC id",
    "Reward partial",
    "Reward full",
    "Disp start",
    "ONE_SHOT",
    "DISP_OVERRIDE",
    "HIDDEN",
};

/* ------ SCR_LIST ------ */

static void drawList(void) {
    clear();
    int visible = LINES - 3;
    scroll_to(selSe, &scrollSe, visible);
    mvprintw(0, 0, "SOCIAL ENCOUNTER EDITOR  [%s]  [%d/%d]",
        dirty ? "unsaved" : "saved", seCount > 0 ? selSe + 1 : 0, seCount);
    mvprintw(1, 0, "Enter=edit  N=new  D=delete  S=save  Q=quit");
    mvprintw(2, 0, "  %-4s  %-6s  %-8s  %-8s  %-8s  flags",
        "id", "npc_id", "p_reward", "f_reward", "disp");

    for (int i = scrollSe; i < seCount && i < scrollSe + visible; i++) {
        SocialEncounterDef *s = &ses[i];
        if (i == selSe) attron(A_REVERSE);
        char flags[32] = "";
        if (s->flags & SE_FLAG_ONE_SHOT)      strcat(flags, "ONE_SHOT ");
        if (s->flags & SE_FLAG_DISP_OVERRIDE) strcat(flags, "DISP_OVR ");
        if (s->flags & SE_FLAG_HIDDEN)        strcat(flags, "HIDDEN");
        mvprintw((i - scrollSe) + 3, 2, "[%2d]  npc:%-4d  p:%-4d  f:%-4d  disp:%-4d  %s",
            i, s->npc_id, s->reward_partial, s->reward_full, s->disp_start, flags);
        if (i == selSe) attroff(A_REVERSE);
    }
    if (seCount == 0)
        mvprintw(3, 2, "(no encounters — press N to add one)");
}

static void handleList(int ch) {
    switch (ch) {
        case KEY_UP:   if (selSe > 0) selSe--; break;
        case KEY_DOWN: if (selSe < seCount - 1) selSe++; break;
        case '\n': case KEY_ENTER:
            if (seCount > 0) { selFld = 0; screen = SCR_EDIT; }
            break;
        case 'n': case 'N':
            if (seCount < SE_DEF_MAX) {
                memset(&ses[seCount], 0, sizeof(SocialEncounterDef));
                ses[seCount].id = (uint8_t)seCount;
                ses[seCount].disp_start = 50;
                selSe = seCount++;
                dirty = 1;
            }
            break;
        case 'd': case 'D':
            if (seCount > 0) {
                for (int i = selSe; i < seCount - 1; i++)
                    ses[i] = ses[i + 1];
                /* re-stamp ids */
                for (int i = 0; i < seCount - 1; i++)
                    ses[i].id = (uint8_t)i;
                seCount--;
                if (selSe >= seCount && selSe > 0) selSe--;
                dirty = 1;
            }
            break;
        case 's': case 'S': save(); break;
    }
}

/* ------ SCR_EDIT ------ */

static void drawEdit(void) {
    clear();
    SocialEncounterDef *s = &ses[selSe];
    mvprintw(0, 0, "ENCOUNTER [%d]  npc_id:%d  [%s]",
        selSe, s->npc_id, dirty ? "unsaved" : "saved");
    mvprintw(1, 0, "Up/Down=field  +/-=change  T=toggle flag  Bksp/Q=back  S=save");

    for (int i = 0; i < FLD_COUNT; i++) {
        if (i == selFld) attron(A_REVERSE);
        switch (i) {
            case FLD_NPC_ID:
                mvprintw(3 + i, 2, "%-16s: %s", fldLabels[i],
                         refLabel(REF_NPC, s->npc_id)); break;
            case FLD_REWARD_PARTIAL:
                mvprintw(3 + i, 2, "%-16s: %d", fldLabels[i], s->reward_partial); break;
            case FLD_REWARD_FULL:
                mvprintw(3 + i, 2, "%-16s: %d", fldLabels[i], s->reward_full); break;
            case FLD_DISP_START:
                mvprintw(3 + i, 2, "%-16s: %d  (starting disposition 0-100)", fldLabels[i], s->disp_start); break;
            case FLD_ONE_SHOT:
                mvprintw(3 + i, 2, "%-16s: %s  (T to toggle)", fldLabels[i],
                    (s->flags & SE_FLAG_ONE_SHOT) ? "YES" : "no"); break;
            case FLD_DISP_OVERRIDE:
                mvprintw(3 + i, 2, "%-16s: %s  (T to toggle)", fldLabels[i],
                    (s->flags & SE_FLAG_DISP_OVERRIDE) ? "YES" : "no"); break;
            case FLD_HIDDEN:
                mvprintw(3 + i, 2, "%-16s: %s  (T to toggle)", fldLabels[i],
                    (s->flags & SE_FLAG_HIDDEN) ? "YES" : "no"); break;
        }
        if (i == selFld) attroff(A_REVERSE);
    }

    mvprintw(12, 0, "disp_start: disposition the player starts the encounter with (0=hostile, 100=friendly)");
    mvprintw(13, 0, "ONE_SHOT: encounter can only succeed/fail once per save");
    mvprintw(14, 0, "DISP_OVERRIDE: ignore standing; always use disp_start");
    mvprintw(15, 0, "HIDDEN: encounter not listed in journal until discovered");
}

static void handleEdit(int ch) {
    SocialEncounterDef *s = &ses[selSe];

    switch (ch) {
        case KEY_UP:   if (selFld > 0) selFld--; break;
        case KEY_DOWN: if (selFld < FLD_COUNT - 1) selFld++; break;
        case '\n': case KEY_ENTER:
            if (selFld == FLD_NPC_ID) { s->npc_id = refPick(REF_NPC, s->npc_id); dirty = 1; }
            break;
        case KEY_RIGHT:
            if (selFld == FLD_NPC_ID) { s->npc_id = refCycle(REF_NPC, s->npc_id,  1); dirty = 1; }
            break;
        case KEY_LEFT:
            if (selFld == FLD_NPC_ID) { s->npc_id = refCycle(REF_NPC, s->npc_id, -1); dirty = 1; }
            break;
        case 't': case 'T':
            dirty = 1;
            switch (selFld) {
                case FLD_ONE_SHOT:      s->flags ^= SE_FLAG_ONE_SHOT;      break;
                case FLD_DISP_OVERRIDE: s->flags ^= SE_FLAG_DISP_OVERRIDE; break;
                case FLD_HIDDEN:        s->flags ^= SE_FLAG_HIDDEN;        break;
                default: dirty = 0; break;
            }
            break;
        case '+': case '=':
            dirty = 1;
            switch (selFld) {
                case FLD_NPC_ID:         s->npc_id = refCycle(REF_NPC, s->npc_id,  1); break;
                case FLD_REWARD_PARTIAL: if (s->reward_partial < 255) s->reward_partial++; break;
                case FLD_REWARD_FULL:    if (s->reward_full < 255)    s->reward_full++;    break;
                case FLD_DISP_START:     if (s->disp_start < 100)     s->disp_start++;     break;
                default: dirty = 0; break;
            }
            break;
        case '-':
            dirty = 1;
            switch (selFld) {
                case FLD_NPC_ID:         s->npc_id = refCycle(REF_NPC, s->npc_id, -1); break;
                case FLD_REWARD_PARTIAL: if (s->reward_partial > 0)   s->reward_partial--; break;
                case FLD_REWARD_FULL:    if (s->reward_full > 0)      s->reward_full--;    break;
                case FLD_DISP_START:     if (s->disp_start > 0)       s->disp_start--;     break;
                default: dirty = 0; break;
            }
            break;
        case KEY_BACKSPACE: case 127: case 'q': case 'Q':
            screen = SCR_LIST;
            selFld = 0;
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
            case SCR_LIST: drawList(); break;
            case SCR_EDIT: drawEdit(); break;
        }
        refresh();

        int ch = getch();

        if ((ch == 'q' || ch == 'Q') && screen == SCR_LIST) {
            running = 0;
            break;
        }

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
