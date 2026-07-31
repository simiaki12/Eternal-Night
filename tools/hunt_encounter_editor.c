/* tools/hunt_encounter_editor.c — ncurses editor for hunt_encounters.dat
 *
 * Hunts no longer carry bespoke state machines: every group walks the shared
 * hunt morale graph from states.dat. What varies per hunt is which postures
 * the group can take (stateMask), how hard they are to move (tenacity), and
 * how their alert escalates once you start making noise.
 *
 * Navigation:
 *   Up/Down = field   +/- = change   Enter = edit text
 *   Tab     = next hunt   E = new   D = delete   S = save   Q = quit
 */

#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define HUNT_ENC_MAX     16
#define GRAPH_STATES_MAX  8
#define HUNT_FLAG_REPEATABLE (1<<0)

/* Hunt morale graph — mirror of seed_states.c */
#define HSTATE_COUNT 6
static const char *stateNames[HSTATE_COUNT] = {
    "Organized", "Disturbed", "Alerted", "Terrified", "Broken", "Preparing"
};

typedef struct {
    uint8_t id;
    char    name[24];
    uint8_t enemyPoolId;
    uint8_t enemyCount;
    uint8_t stateMask;
    uint8_t tenacity;
    uint8_t escalateEvery;
    uint8_t alertLimit;
    uint8_t escalateTo[GRAPH_STATES_MAX];
    uint8_t flags;
    uint8_t setFlag;
    uint8_t _pad[3];
} HuntEncounterDef;

typedef char _chk[(sizeof(HuntEncounterDef) == 44) ? 1 : -1];

static HuntEncounterDef encs[HUNT_ENC_MAX];
static int  encCount = 0;
static int  dirty    = 0;
static const char *outfile = "assets/data/hunt_encounters.dat";

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) return;
    uint8_t n;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > HUNT_ENC_MAX) n = HUNT_ENC_MAX;
    encCount = (int)fread(encs, sizeof(HuntEncounterDef), n, f);
    fclose(f);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)encCount;
    fwrite(&n, 1, 1, f);
    fwrite(encs, sizeof(HuntEncounterDef), encCount, f);
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

/* Fields: fixed head, then one mask toggle and one ladder entry per state */
enum { F_ID = 0, F_NAME, F_POOL, F_GROUP_SIZE, F_TENACITY,
       F_ESC_EVERY, F_ALERT_LIMIT, F_SETFLAG, F_REPEATABLE, F_HEAD_COUNT };
#define F_MASK0   F_HEAD_COUNT
#define F_LADDER0 (F_MASK0 + HSTATE_COUNT)
#define F_TOTAL   (F_LADDER0 + HSTATE_COUNT)

static const char *headNames[F_HEAD_COUNT] = {
    "ID", "Name", "Fallback enemy pool (FF=none)", "Group size",
    "Tenacity % (progress scaling, 0=100)",
    "Alert per escalation step (0=never)",
    "Alert limit - they charge (0=never)",
    "World flag on success (FF=none)",
    "Repeatable",
};

static void render(int idx, int sel, const char *status) {
    HuntEncounterDef *e = &encs[idx];
    clear();
    mvprintw(0, 0, "HUNT EDITOR - %s (%d/%d)%s",
             e->name[0] ? e->name : "(unnamed)", idx + 1, encCount, dirty ? " *" : "");
    mvprintw(1, 0, "Up/Down=field  +/-=change  Enter=text  Tab=next  E=new  D=del  S=save  Q=quit");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = 0; i < F_TOTAL; i++) {
        int row = i + 4;
        if (row >= LINES - 1) break;
        if (i == sel) attron(A_REVERSE);
        if (i < F_HEAD_COUNT) {
            switch (i) {
                case F_ID:      mvprintw(row, 2, "%-40s  %d", headNames[i], e->id); break;
                case F_NAME:    mvprintw(row, 2, "%-40s  %s", headNames[i], e->name); break;
                case F_POOL:
                    if (e->enemyPoolId == 0xFF) mvprintw(row, 2, "%-40s  none", headNames[i]);
                    else mvprintw(row, 2, "%-40s  %d", headNames[i], e->enemyPoolId);
                    break;
                case F_GROUP_SIZE:  mvprintw(row, 2, "%-40s  %d", headNames[i], e->enemyCount); break;
                case F_TENACITY:    mvprintw(row, 2, "%-40s  %d", headNames[i], e->tenacity); break;
                case F_ESC_EVERY:   mvprintw(row, 2, "%-40s  %d", headNames[i], e->escalateEvery); break;
                case F_ALERT_LIMIT: mvprintw(row, 2, "%-40s  %d", headNames[i], e->alertLimit); break;
                case F_SETFLAG:
                    if (e->setFlag == 0xFF) mvprintw(row, 2, "%-40s  none", headNames[i]);
                    else mvprintw(row, 2, "%-40s  %d", headNames[i], e->setFlag);
                    break;
                case F_REPEATABLE:
                    mvprintw(row, 2, "%-40s  %s", headNames[i],
                             (e->flags & HUNT_FLAG_REPEATABLE) ? "[X]" : "[ ]"); break;
            }
        } else if (i < F_LADDER0) {
            int s = i - F_MASK0;
            mvprintw(row, 2, "Can enter: %-29s  %s", stateNames[s],
                     (e->stateMask & (1u << s)) ? "[X]" : "[ ]");
        } else {
            int s = i - F_LADDER0;
            uint8_t to = e->escalateTo[s];
            mvprintw(row, 2, "Escalates %-10s -> %s", stateNames[s],
                     to < HSTATE_COUNT ? stateNames[to] : "(none)");
        }
        if (i == sel) attroff(A_REVERSE);
    }
    refresh();
}

static void bump(HuntEncounterDef *e, int sel, int dir) {
    if (sel < F_HEAD_COUNT) {
        switch (sel) {
            case F_ID:          e->id            = (uint8_t)(e->id + dir); break;
            case F_POOL:        e->enemyPoolId   = (uint8_t)(e->enemyPoolId + dir); break;
            case F_GROUP_SIZE:  e->enemyCount    = (uint8_t)(e->enemyCount + dir); break;
            case F_TENACITY:    e->tenacity      = (uint8_t)(e->tenacity + dir); break;
            case F_ESC_EVERY:   e->escalateEvery = (uint8_t)(e->escalateEvery + dir); break;
            case F_ALERT_LIMIT: e->alertLimit    = (uint8_t)(e->alertLimit + dir); break;
            case F_SETFLAG:     e->setFlag       = (uint8_t)(e->setFlag + dir); break;
            case F_REPEATABLE:  e->flags        ^= HUNT_FLAG_REPEATABLE; break;
            default: return;
        }
    } else if (sel < F_LADDER0) {
        e->stateMask ^= (uint8_t)(1u << (sel - F_MASK0));
    } else {
        int s = sel - F_LADDER0;
        uint8_t to = e->escalateTo[s];
        if (dir > 0) to = (to >= HSTATE_COUNT) ? 0 : (uint8_t)(to + 1);
        else         to = (to == 0 || to >= HSTATE_COUNT) ? 0xFF : (uint8_t)(to - 1);
        if (to >= HSTATE_COUNT) to = 0xFF;
        e->escalateTo[s] = to;
    }
    dirty = 1;
}

static void initDef(HuntEncounterDef *e, uint8_t id) {
    memset(e, 0, sizeof(*e));
    e->id          = id;
    e->stateMask   = 0x3F;
    e->tenacity    = 100;
    e->setFlag     = 0xFF;
    e->enemyPoolId = 0xFF;
    memset(e->escalateTo, 0xFF, GRAPH_STATES_MAX);
}

int main(void) {
    load();
    if (encCount == 0) { initDef(&encs[0], 0); encCount = 1; }

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
            case '\t':     idx = (idx + 1) % encCount; sel = 0; break;
            case '+': case '=': bump(&encs[idx], sel,  1); break;
            case '-': case '_': bump(&encs[idx], sel, -1); break;
            case 's': case 'S': save(); status = "Saved."; break;
            case 'e': case 'E':
                if (encCount < HUNT_ENC_MAX) {
                    initDef(&encs[encCount], (uint8_t)encCount);
                    idx = encCount++;
                    sel = 0; dirty = 1;
                }
                break;
            case 'd': case 'D':
                if (encCount > 1) {
                    for (int i = idx; i < encCount - 1; i++) encs[i] = encs[i + 1];
                    encCount--;
                    if (idx >= encCount) idx = encCount - 1;
                    dirty = 1;
                }
                break;
            case '\n': case KEY_ENTER:
                if (sel == F_NAME && editString(F_NAME + 4, 44, encs[idx].name, 24))
                    dirty = 1;
                break;
        }
    }
    endwin();
    return 0;
}
