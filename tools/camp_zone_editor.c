/* tools/camp_zone_editor.c — ncurses editor for camp_zones.dat
 *
 * Screens:
 *   SCR_LIST — camp zone list
 *   SCR_ZONE — zone fields
 *
 * Navigation:
 *   Up/Down  = move selection
 *   Enter    = drill in / confirm text edit
 *   Esc      = go back
 *   E        = new zone
 *   D        = delete selected zone
 *   +/-      = increment/decrement numeric fields
 *   S        = save
 *   Q        = quit (prompts to save)
 */

#include <ncurses.h>
#include "refs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CAMP_ZONE_MAX 64
#define CAMP_FLAG_REPEATABLE (1<<0)

typedef struct {
    char    mapId[8];
    uint8_t leftX, rightX, topY, bottomY;
    uint8_t huntEncId;
    uint8_t clearedFlag;
    uint8_t flags;
    uint8_t _pad[1];
} CampZone;

typedef char _chk_zone[(sizeof(CampZone)==16)?1:-1];

static CampZone zones[CAMP_ZONE_MAX];
static int      zoneCount = 0;
static int      dirty     = 0;

static const char *DAT = "assets/data/camp_zones.dat";

static void loadDat(void) {
    FILE *f = fopen(DAT, "rb");
    if (!f) return;
    uint8_t n; fread(&n, 1, 1, f);
    if (n > CAMP_ZONE_MAX) n = CAMP_ZONE_MAX;
    zoneCount = n;
    fread(zones, sizeof(CampZone), n, f);
    fclose(f);
}
static void saveDat(void) {
    FILE *f = fopen(DAT, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)zoneCount;
    fwrite(&n, 1, 1, f);
    fwrite(zones, sizeof(CampZone), n, f);
    fclose(f);
    dirty = 0;
}

typedef enum { SCR_LIST, SCR_ZONE } Screen;
static Screen screen  = SCR_LIST;
static int    selZone  = 0;
static int    selField = 0;

static void editStr(int row, int col, char *buf, int maxLen) {
    echo(); curs_set(1);
    mvprintw(row, col, "%*s", maxLen, "");
    move(row, col);
    char tmp[128] = {0};
    if (getnstr(tmp, maxLen - 1) == OK)
        strncpy(buf, tmp, (size_t)maxLen - 1);
    noecho(); curs_set(0);
}

/* ---- SCR_LIST ---- */
static void drawList(void) {
    clear();
    mvprintw(0, 0, "CAMP ZONE EDITOR  [%d/%d]  S=save  Q=quit  E=new  D=delete",
             zoneCount > 0 ? selZone + 1 : 0, zoneCount);
    for (int i = 0; i < zoneCount; i++) {
        if (i == selZone) attron(A_REVERSE);
        const CampZone *z = &zones[i];
        mvprintw(2+i, 2, "[%d] map=%-8s  (%d,%d)-(%-2d,%-2d)  hunt=%d  flag=%s",
            i, z->mapId,
            z->leftX, z->topY, z->rightX, z->bottomY,
            z->huntEncId,
            z->clearedFlag == 0xFF ? "none" : (char[8]){""}); /* placeholder */
        if (z->clearedFlag != 0xFF) {
            mvprintw(2+i, 62, "%d", z->clearedFlag);
        } else {
            mvprintw(2+i, 62, "none");
        }
        if (i == selZone) attroff(A_REVERSE);
    }
    refresh();
}

static void handleList(int ch) {
    switch (ch) {
        case KEY_UP:   if (selZone > 0) selZone--; break;
        case KEY_DOWN: if (selZone < zoneCount - 1) selZone++; break;
        case '\n': case KEY_ENTER:
            if (zoneCount > 0) { screen = SCR_ZONE; selField = 0; }
            break;
        case 'e': case 'E':
            if (zoneCount < CAMP_ZONE_MAX) {
                memset(&zones[zoneCount], 0, sizeof(CampZone));
                zones[zoneCount].huntEncId   = 0;
                zones[zoneCount].clearedFlag = 0xFF;
                zones[zoneCount].flags       = 0;
                selZone = zoneCount++;
                dirty = 1;
            }
            break;
        case 'd': case 'D':
            if (zoneCount > 0) {
                for (int i = selZone; i < zoneCount - 1; i++) zones[i] = zones[i+1];
                zoneCount--;
                if (selZone >= zoneCount && selZone > 0) selZone--;
                dirty = 1;
            }
            break;
        case 's': case 'S': saveDat(); break;
        case 'q': case 'Q':
            if (dirty) {
                mvprintw(LINES-1, 0, "Save before quit? (y/n): ");
                refresh(); int c = getch();
                if (c == 'y' || c == 'Y') saveDat();
            }
            endwin(); exit(0);
    }
}

/* ---- SCR_ZONE ---- */
/* fields: 0=mapId, 1=leftX, 2=rightX, 3=topY, 4=bottomY, 5=huntEncId, 6=clearedFlag, 7=flags */
#define ZONE_FIELDS 8

static void drawZone(void) {
    CampZone *z = &zones[selZone];
    clear();
    mvprintw(0, 0, "CAMP ZONE [%d]  Esc=back  +/-=change  Enter=edit text  S=save", selZone);

    const char *fnames[] = {
        "Map ID", "Left X", "Right X", "Top Y", "Bottom Y",
        "Hunt enc ID", "Cleared flag", "Flags"
    };
    for (int i = 0; i < ZONE_FIELDS; i++) {
        int sel = (i == selField);
        if (sel) attron(A_REVERSE);
        switch (i) {
            case 0: mvprintw(2+i, 2, "%-14s %-8s", fnames[i], z->mapId); break;
            case 1: mvprintw(2+i, 2, "%-14s %d", fnames[i], z->leftX); break;
            case 2: mvprintw(2+i, 2, "%-14s %d", fnames[i], z->rightX); break;
            case 3: mvprintw(2+i, 2, "%-14s %d", fnames[i], z->topY); break;
            case 4: mvprintw(2+i, 2, "%-14s %d", fnames[i], z->bottomY); break;
            case 5: mvprintw(2+i, 2, "%-14s %s", fnames[i], refLabel(REF_HUNT_ENC, z->huntEncId)); break;
            case 6:
                if (z->clearedFlag == 0xFF)
                    mvprintw(2+i, 2, "%-14s none (0xFF)", fnames[i]);
                else
                    mvprintw(2+i, 2, "%-14s flag %d", fnames[i], z->clearedFlag);
                break;
            case 7:
                mvprintw(2+i, 2, "%-14s %s", fnames[i],
                    (z->flags & CAMP_FLAG_REPEATABLE) ? "[REPEATABLE]" : "[]");
                break;
        }
        if (sel) attroff(A_REVERSE);
    }
    mvprintw(LINES-2, 0, "Map ID must match the map file base name, e.g. 'world' for assets/maps/world.bin");
    refresh();
}

static void handleZone(int ch) {
    CampZone *z = &zones[selZone];
    switch (ch) {
        case KEY_UP:   if (selField > 0) selField--; break;
        case KEY_DOWN: if (selField < ZONE_FIELDS-1) selField++; break;
        case '\n': case KEY_ENTER:
            if (selField == 0) { editStr(2, 16, z->mapId, 8); dirty=1; }
            else if (selField == 5) { z->huntEncId = refPick(REF_HUNT_ENC, z->huntEncId); dirty=1; }
            break;
        case KEY_RIGHT:
            if (selField == 5) { z->huntEncId = refCycle(REF_HUNT_ENC, z->huntEncId,  1); dirty=1; }
            break;
        case KEY_LEFT:
            if (selField == 5) { z->huntEncId = refCycle(REF_HUNT_ENC, z->huntEncId, -1); dirty=1; }
            break;
        case '+': case '=':
            dirty=1;
            switch (selField) {
                case 1: if (z->leftX  < 255) z->leftX++;  break;
                case 2: if (z->rightX < 255) z->rightX++; break;
                case 3: if (z->topY   < 255) z->topY++;   break;
                case 4: if (z->bottomY< 255) z->bottomY++;break;
                case 5: z->huntEncId = refCycle(REF_HUNT_ENC, z->huntEncId,  1); break;
                case 6:
                    if (z->clearedFlag == 0xFF) z->clearedFlag = 0;
                    else if (z->clearedFlag < 127) z->clearedFlag++;
                    else z->clearedFlag = 0xFF;
                    break;
                case 7: z->flags ^= CAMP_FLAG_REPEATABLE; break;
            }
            break;
        case '-': case '_':
            dirty=1;
            switch (selField) {
                case 1: if (z->leftX  > 0) z->leftX--;  break;
                case 2: if (z->rightX > 0) z->rightX--; break;
                case 3: if (z->topY   > 0) z->topY--;   break;
                case 4: if (z->bottomY> 0) z->bottomY--;break;
                case 5: z->huntEncId = refCycle(REF_HUNT_ENC, z->huntEncId, -1); break;
                case 6:
                    if (z->clearedFlag == 0xFF) z->clearedFlag = 127;
                    else if (z->clearedFlag > 0) z->clearedFlag--;
                    else z->clearedFlag = 0xFF;
                    break;
                case 7: z->flags ^= CAMP_FLAG_REPEATABLE; break;
            }
            break;
        case 27: screen = SCR_LIST; break;
        case 's': case 'S': saveDat(); break;
    }
}

/* ---- main ---- */
int main(void) {
    loadDat();
    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0);

    while (1) {
        switch (screen) {
            case SCR_LIST: drawList(); break;
            case SCR_ZONE: drawZone(); break;
        }
        int ch = getch();
        if (screen == SCR_LIST) handleList(ch);
        else                    handleZone(ch);
    }
}
