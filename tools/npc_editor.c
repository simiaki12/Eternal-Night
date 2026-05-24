/*
 * npc_editor.c — ncurses editor for assets/npcs.dat
 *
 * Navigation:
 *   SCR_LIST  Up/Down=select  N=new  D=delete  Enter=edit  S=save  Q=quit
 *   SCR_EDIT  Up/Down=field   +/-=change numeric  Enter=edit text  Bksp=back
 */

#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Mirror of src/npcs.h and social.h */
#define NPC_DEF_MAX 32

#define NPC_TAG_FEARFUL   (1<<0)
#define NPC_TAG_NOBLE     (1<<1)
#define NPC_TAG_CRIMINAL  (1<<2)
#define NPC_TAG_CONNECTED (1<<3)
#define NPC_TAG_FEARLESS  (1<<4)

#define NPC_MOVE_DISMISS  (1<<0)
#define NPC_MOVE_THREATEN (1<<1)
#define NPC_MOVE_LEAVE    (1<<2)

typedef struct {
    char    name[16];
    char    imgName[3];
    uint8_t treeId;
    uint8_t x;
    uint8_t y;
    char    mapId[8];
    uint8_t resistance;
    uint8_t patience;
    uint8_t social_power;
    uint8_t move_mask;
    uint8_t tags;
    uint8_t base_standing;
    uint8_t npc_flags;
    uint8_t _pad[3];
} NpcDef;

typedef char check_size[(sizeof(NpcDef) == 40) ? 1 : -1];

#define SCROLL_MARGIN 3

static void scroll_to(int sel, int *scroll, int visible) {
    if (sel - *scroll < SCROLL_MARGIN)               *scroll = sel - SCROLL_MARGIN;
    if (sel - *scroll > visible - SCROLL_MARGIN - 1)  *scroll = sel - visible + SCROLL_MARGIN + 1;
    if (*scroll < 0) *scroll = 0;
}

static NpcDef npcs[NPC_DEF_MAX];
static int    npcCount = 0;
static int    dirty    = 0;
static const char *outfile = "assets/data/npcs.dat";

/* ---- file I/O ---- */

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) { npcCount = 0; return; }
    uint8_t n;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > NPC_DEF_MAX) n = NPC_DEF_MAX;
    npcCount = (int)fread(npcs, sizeof(NpcDef), n, f);
    fclose(f);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)npcCount;
    fwrite(&n, 1, 1, f);
    fwrite(npcs, sizeof(NpcDef), npcCount, f);
    fclose(f);
    dirty = 0;
}

/* ---- inline text input ---- */

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

/* ---- edit screen ---- */

typedef enum {
    F_NAME = 0,
    F_IMG,
    F_TREE,
    F_X,
    F_Y,
    F_MAPID,
    F_RESISTANCE,
    F_PATIENCE,
    F_SOCIAL_POWER,
    F_MOVE_MASK,
    F_TAGS,
    F_BASE_STANDING,
    F_COUNT
} Field;

static const char *fieldNames[] = {
    "Name", "Image (2 chars)", "Dialog Tree ID", "X (tile)", "Y (tile)", "Map ID (7 chars)",
    "Resistance (0-255)", "Patience (0=none)", "Social Power (0=passive)",
    "Move Mask (bits)", "Tags (bits)", "Base Standing (0-255)"
};

static void renderEdit(NpcDef *n, int sel, const char *status) {
    clear();
    mvprintw(0, 0, "NPC EDITOR — %s", n->name[0] ? n->name : "(unnamed)");
    mvprintw(1, 0, "Up/Down=field  +/-=change  Enter=edit text  Bksp=back  S=save");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = 0; i < F_COUNT; i++) {
        if (i == sel) attron(A_REVERSE);
        int row = i + 4;
        switch (i) {
            case F_NAME:  mvprintw(row, 2, "%-22s  %s",  fieldNames[i], n->name);   break;
            case F_IMG:   mvprintw(row, 2, "%-22s  %s",  fieldNames[i], n->imgName); break;
            case F_MAPID: mvprintw(row, 2, "%-22s  %s",  fieldNames[i], n->mapId);  break;
            case F_TREE:
                if (n->treeId == 0xFF)
                    mvprintw(row, 2, "%-22s  none", fieldNames[i]);
                else
                    mvprintw(row, 2, "%-22s  %d", fieldNames[i], n->treeId);
                break;
            case F_X:            mvprintw(row, 2, "%-22s  %d",   fieldNames[i], n->x);             break;
            case F_Y:            mvprintw(row, 2, "%-22s  %d",   fieldNames[i], n->y);             break;
            case F_RESISTANCE:   mvprintw(row, 2, "%-22s  %d",   fieldNames[i], n->resistance);    break;
            case F_PATIENCE:     mvprintw(row, 2, "%-22s  %d%s", fieldNames[i], n->patience,
                                     n->patience == 0 ? " (no limit)" : "");                       break;
            case F_SOCIAL_POWER: mvprintw(row, 2, "%-22s  %d%s", fieldNames[i], n->social_power,
                                     n->social_power == 0 ? " (passive)" : "");                    break;
            case F_MOVE_MASK:    mvprintw(row, 2, "%-22s  0x%02X  [%s%s%s]", fieldNames[i], n->move_mask,
                                     n->move_mask & NPC_MOVE_DISMISS  ? "DISMISS "  : "",
                                     n->move_mask & NPC_MOVE_THREATEN ? "THREATEN " : "",
                                     n->move_mask & NPC_MOVE_LEAVE    ? "LEAVE"     : "");         break;
            case F_TAGS:         mvprintw(row, 2, "%-22s  0x%02X  [%s%s%s%s%s]", fieldNames[i], n->tags,
                                     n->tags & NPC_TAG_FEARFUL   ? "FEARFUL "   : "",
                                     n->tags & NPC_TAG_NOBLE      ? "NOBLE "     : "",
                                     n->tags & NPC_TAG_CRIMINAL   ? "CRIMINAL "  : "",
                                     n->tags & NPC_TAG_CONNECTED  ? "CONNECTED " : "",
                                     n->tags & NPC_TAG_FEARLESS   ? "FEARLESS"   : "");            break;
            case F_BASE_STANDING:mvprintw(row, 2, "%-22s  %d",   fieldNames[i], n->base_standing); break;
        }
        if (i == sel) attroff(A_REVERSE);
    }
    refresh();
}

static void screenEdit(int idx) {
    NpcDef     *n   = &npcs[idx];
    int         sel = 0;
    const char *status = NULL;

    while (1) {
        renderEdit(n, sel, status);
        status = NULL;
        int ch = getch();

        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--; break;
            case KEY_DOWN: if (sel < F_COUNT - 1) sel++; break;

            case KEY_BACKSPACE: case 127: return;

            case 's': case 'S': save(); status = "Saved."; break;

            case '\n': case KEY_ENTER:
                if (sel == F_NAME)  { if (editString(sel + 4, 26, n->name,   16)) dirty = 1; }
                if (sel == F_IMG)   { if (editString(sel + 4, 26, n->imgName, 3)) dirty = 1; }
                if (sel == F_MAPID) { if (editString(sel + 4, 26, n->mapId,   8)) dirty = 1; }
                break;

            case '+': case '=':
                dirty = 1;
                switch (sel) {
                    case F_X:            if (n->x < 255)             n->x++;             break;
                    case F_Y:            if (n->y < 255)             n->y++;             break;
                    case F_RESISTANCE:   if (n->resistance < 255)    n->resistance++;    break;
                    case F_PATIENCE:     if (n->patience < 255)      n->patience++;      break;
                    case F_SOCIAL_POWER: if (n->social_power < 255)  n->social_power++;  break;
                    case F_MOVE_MASK:    n->move_mask = (n->move_mask + 1) & 0x07;       break;
                    case F_TAGS:         n->tags      = (n->tags      + 1) & 0x1F;       break;
                    case F_BASE_STANDING:if (n->base_standing < 255) n->base_standing++; break;
                    case F_TREE:
                        n->treeId = (n->treeId == 0xFF) ? 0
                                  : (n->treeId < 254)   ? n->treeId + 1
                                  : 0xFF;
                        break;
                    default: dirty = 0; break;
                }
                break;

            case '-':
                dirty = 1;
                switch (sel) {
                    case F_X:            if (n->x > 0)               n->x--;             break;
                    case F_Y:            if (n->y > 0)               n->y--;             break;
                    case F_RESISTANCE:   if (n->resistance > 0)      n->resistance--;    break;
                    case F_PATIENCE:     if (n->patience > 0)        n->patience--;      break;
                    case F_SOCIAL_POWER: if (n->social_power > 0)    n->social_power--;  break;
                    case F_MOVE_MASK:    n->move_mask = (n->move_mask - 1) & 0x07;       break;
                    case F_TAGS:         n->tags      = (n->tags      - 1) & 0x1F;       break;
                    case F_BASE_STANDING:if (n->base_standing > 0)   n->base_standing--; break;
                    case F_TREE:
                        n->treeId = (n->treeId == 0)    ? 0xFF
                                  : (n->treeId == 0xFF) ? 254
                                  : n->treeId - 1;
                        break;
                    default: dirty = 0; break;
                }
                break;
        }
    }
}

/* ---- list screen ---- */

static void renderList(int sel, int scroll, const char *status) {
    clear();
    int visible = LINES - 4;
    mvprintw(0, 0, "NPC LIST  [%s]  [%d/%d]", dirty ? "unsaved" : "saved",
        npcCount > 0 ? sel + 1 : 0, npcCount);
    mvprintw(1, 0, "Up/Down=select  Enter=edit  N=new  D=delete  S=save  Q=quit");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = scroll; i < npcCount && i < scroll + visible; i++) {
        if (i == sel) attron(A_REVERSE);
        mvprintw((i - scroll) + 4, 2, "%2d  %-15s  (%3d,%3d)  map:%-7s  tree:%s  img:%s",
            i,
            npcs[i].name[0] ? npcs[i].name : "(unnamed)",
            npcs[i].x, npcs[i].y,
            npcs[i].mapId[0] ? npcs[i].mapId : "--",
            npcs[i].treeId == 0xFF ? "none" : (char[4]){""},
            npcs[i].imgName[0] ? npcs[i].imgName : "--");
        if (npcs[i].treeId != 0xFF) {
            mvprintw((i - scroll) + 4, 63, "%d", npcs[i].treeId);
        }
        if (i == sel) attroff(A_REVERSE);
    }

    if (npcCount == 0)
        mvprintw(4, 2, "(no NPCs — press N to add one)");

    refresh();
}

int main(void) {
    load();

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    int  sel     = 0;
    int  scroll  = 0;
    int  running = 1;
    const char *status = NULL;

    while (running) {
        if (sel >= npcCount && npcCount > 0) sel = npcCount - 1;
        if (sel < 0) sel = 0;
        scroll_to(sel, &scroll, LINES - 4);

        renderList(sel, scroll, status);
        status = NULL;
        int ch = getch();

        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--; break;
            case KEY_DOWN: if (sel < npcCount - 1) sel++; break;

            case '\n': case KEY_ENTER:
                if (npcCount > 0) screenEdit(sel);
                break;

            case 'n': case 'N':
                if (npcCount < NPC_DEF_MAX) {
                    memset(&npcs[npcCount], 0, sizeof(NpcDef));
                    npcs[npcCount].treeId = 0xFF;
                    sel = npcCount++;
                    dirty = 1;
                    screenEdit(sel);
                } else {
                    status = "Max NPCs reached.";
                }
                break;

            case 'd': case 'D':
                if (npcCount > 0) {
                    for (int i = sel; i < npcCount - 1; i++)
                        npcs[i] = npcs[i + 1];
                    npcCount--;
                    dirty = 1;
                    status = "NPC deleted.";
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
        int c = getchar();
        if (c == 'y' || c == 'Y') save();
    }

    return 0;
}
