/*
 * status_editor.c — ncurses editor for assets/data/statuses.dat
 *
 * Navigation:
 *   LIST  Up/Down=select  N=new  D=delete  Enter=edit  S=save  Q=quit
 *   EDIT  Up/Down=field   +/-=change  PgUp/PgDn=+-10  Enter=edit text  Bksp=back
 */

#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- mirror of src/gameplay/statuses.h and effects.h (keep in sync) ---- */
typedef struct { uint8_t type, value, chance; } Effect;

#define DUR_TURNS 0
#define DUR_STEPS 1
#define DUR_PERM  2

#define STFX_COUNT 6
#define EFX_COUNT  9

#define STATUS_NEGATIVE (1<<0)

typedef struct {
    uint8_t id;
    char    name[12];
    char    icon[2];
    uint8_t durType;
    uint8_t duration;
    uint8_t fxType;
    uint8_t fxValue;   /* signed (int8_t) */
    uint8_t fxValue2;
    uint8_t flags;
    Effect  onExpire;
} StatusDef; /* 24 bytes */

typedef char _check_size[(sizeof(StatusDef) == 24) ? 1 : -1];

#define STATUS_DEF_MAX 32

static const char *durNames[]  = { "turns (encounter)", "steps (world)", "permanent" };
static const char *stfxNames[] = { "none", "progress_all", "progress_dom",
                                   "threat_mod", "hp_tick", "weight_dom" };
static const char *efxNames[]  = { "none", "progress", "prog_next", "heal_hp",
                                   "dmg_hp", "extra_act", "status+", "status-", "meter" };
/* ----------------------------------------------------------------------- */

static StatusDef  statuses[STATUS_DEF_MAX];
static int        statusCount = 0;
static int        dirty       = 0;
static const char *outfile    = "assets/data/statuses.dat";

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) { statusCount = 0; return; }
    uint8_t n;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > STATUS_DEF_MAX) n = STATUS_DEF_MAX;
    statusCount = (int)fread(statuses, sizeof(StatusDef), n, f);
    fclose(f);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)statusCount;
    fwrite(&n, 1, 1, f);
    fwrite(statuses, sizeof(StatusDef), statusCount, f);
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
    F_ID = 0,
    F_NAME,
    F_ICON,
    F_DURTYPE,
    F_DURATION,
    F_FXTYPE,
    F_FXVALUE,
    F_FXVALUE2,
    F_NEGATIVE,
    F_EXP_TYPE,
    F_EXP_VALUE,
    F_EXP_CHANCE,
    F_COUNT
} Field;

static const char *fieldNames[] = {
    "ID",
    "Name",
    "Icon (1-2 glyphs)",
    "Duration clock",
    "Duration (turns/steps)",
    "Passive effect",
    "Effect value (signed)",
    "Effect value2 (domain)",
    "Negative (curable, drawn red)",
    "On expire: effect",
    "On expire: value",
    "On expire: chance",
};

static void renderEdit(StatusDef *s, int sel, const char *status) {
    clear();
    mvprintw(0, 0, "STATUS EDITOR — %s (id %d)", s->name[0] ? s->name : "(unnamed)", s->id);
    mvprintw(1, 0, "Up/Down=field  +/-=change  PgUp/PgDn=+-10  Enter=edit text  Bksp=back  S=save");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = 0; i < F_COUNT; i++) {
        if (i == sel) attron(A_REVERSE);
        int row = i + 4;
        switch (i) {
            case F_ID:      mvprintw(row, 2, "%-34s  %d",  fieldNames[i], s->id);   break;
            case F_NAME:    mvprintw(row, 2, "%-34s  %s",  fieldNames[i], s->name); break;
            case F_ICON:    mvprintw(row, 2, "%-34s  %c%c", fieldNames[i],
                                     s->icon[0] ? s->icon[0] : ' ',
                                     s->icon[1] ? s->icon[1] : ' ');                break;
            case F_DURTYPE: mvprintw(row, 2, "%-34s  %s",  fieldNames[i],
                                     s->durType <= DUR_PERM ? durNames[s->durType] : "?"); break;
            case F_DURATION:mvprintw(row, 2, "%-34s  %d",  fieldNames[i], s->duration);   break;
            case F_FXTYPE:  mvprintw(row, 2, "%-34s  %s",  fieldNames[i],
                                     s->fxType < STFX_COUNT ? stfxNames[s->fxType] : "?"); break;
            case F_FXVALUE: mvprintw(row, 2, "%-34s  %+d", fieldNames[i], (int8_t)s->fxValue); break;
            case F_FXVALUE2:mvprintw(row, 2, "%-34s  %d",  fieldNames[i], s->fxValue2);   break;
            case F_NEGATIVE:mvprintw(row, 2, "%-34s  %s",  fieldNames[i],
                                     (s->flags & STATUS_NEGATIVE) ? "[X]" : "[ ]");       break;
            case F_EXP_TYPE:mvprintw(row, 2, "%-34s  %s",  fieldNames[i],
                                     s->onExpire.type < EFX_COUNT ? efxNames[s->onExpire.type] : "?"); break;
            case F_EXP_VALUE: mvprintw(row, 2, "%-34s  %d", fieldNames[i], s->onExpire.value);  break;
            case F_EXP_CHANCE:mvprintw(row, 2, "%-34s  %d", fieldNames[i], s->onExpire.chance); break;
        }
        if (i == sel) attroff(A_REVERSE);
    }
    refresh();
}

static void adjust(StatusDef *s, int sel, int dir, int big) {
    int step = big ? 10 : 1;
    dirty = 1;
    switch (sel) {
        case F_ID:       s->id = (uint8_t)(s->id + dir);                        break;
        case F_DURTYPE:  s->durType = (uint8_t)((s->durType + (dir > 0 ? 1 : 2)) % 3); break;
        case F_DURATION: {
            int nv = (int)s->duration + dir * step;
            s->duration = (uint8_t)(nv < 0 ? 0 : nv > 255 ? 255 : nv);
            break;
        }
        case F_FXTYPE:
            s->fxType = (uint8_t)((s->fxType + (dir > 0 ? 1 : STFX_COUNT - 1)) % STFX_COUNT);
            break;
        case F_FXVALUE: {
            int nv = (int)(int8_t)s->fxValue + dir * step;
            s->fxValue = (uint8_t)(int8_t)(nv < -128 ? -128 : nv > 127 ? 127 : nv);
            break;
        }
        case F_FXVALUE2: {
            int nv = (int)s->fxValue2 + dir;
            s->fxValue2 = (uint8_t)(nv < 0 ? 0 : nv > 13 ? 13 : nv);
            break;
        }
        case F_NEGATIVE: s->flags ^= STATUS_NEGATIVE; break;
        case F_EXP_TYPE:
            s->onExpire.type = (uint8_t)((s->onExpire.type + (dir > 0 ? 1 : EFX_COUNT - 1)) % EFX_COUNT);
            break;
        case F_EXP_VALUE: {
            int nv = (int)s->onExpire.value + dir * step;
            s->onExpire.value = (uint8_t)(nv < 0 ? 0 : nv > 255 ? 255 : nv);
            break;
        }
        case F_EXP_CHANCE: {
            int nv = (int)s->onExpire.chance + dir * step;
            s->onExpire.chance = (uint8_t)(nv < 0 ? 0 : nv > 100 ? 100 : nv);
            break;
        }
        default: dirty = 0; break;
    }
}

static void screenEdit(int idx) {
    StatusDef  *s      = &statuses[idx];
    int         sel    = 0;
    const char *status = NULL;

    while (1) {
        renderEdit(s, sel, status);
        status = NULL;
        int ch = getch();
        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--;           break;
            case KEY_DOWN: if (sel < F_COUNT - 1) sel++; break;
            case KEY_BACKSPACE: case 127: return;
            case 's': case 'S': save(); status = "Saved."; break;
            case '\n': case KEY_ENTER:
                if (sel == F_NAME) {
                    if (editString(sel + 4, 40, s->name, 12)) dirty = 1;
                } else if (sel == F_ICON) {
                    char tmp[3] = { s->icon[0], s->icon[1], 0 };
                    if (editString(sel + 4, 40, tmp, 3)) {
                        s->icon[0] = tmp[0]; s->icon[1] = tmp[1]; dirty = 1;
                    }
                }
                break;
            case '+': case '=': adjust(s, sel,  1, 0); break;
            case '-':           adjust(s, sel, -1, 0); break;
            case KEY_PPAGE:     adjust(s, sel,  1, 1); break;
            case KEY_NPAGE:     adjust(s, sel, -1, 1); break;
        }
    }
}

/* ---- list screen ---- */

static void renderList(int sel, const char *status) {
    clear();
    mvprintw(0, 0, "STATUS LIST  [%s]  [%d/%d]", dirty ? "unsaved" : "saved",
        statusCount > 0 ? sel + 1 : 0, statusCount);
    mvprintw(1, 0, "Up/Down=select  Enter=edit  N=new  D=delete  S=save  Q=quit");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = 0; i < statusCount; i++) {
        if (i == sel) attron(A_REVERSE);
        mvprintw(i + 4, 2, "%2d  id:%-3d  %-12s  [%c%c]  %-9s dur:%-3d  %s%+d  %s",
            i, statuses[i].id,
            statuses[i].name[0] ? statuses[i].name : "(unnamed)",
            statuses[i].icon[0] ? statuses[i].icon[0] : ' ',
            statuses[i].icon[1] ? statuses[i].icon[1] : ' ',
            statuses[i].durType <= DUR_PERM ? durNames[statuses[i].durType] : "?",
            statuses[i].duration,
            statuses[i].fxType < STFX_COUNT ? stfxNames[statuses[i].fxType] : "?",
            (int8_t)statuses[i].fxValue,
            (statuses[i].flags & STATUS_NEGATIVE) ? "NEG" : "");
        if (i == sel) attroff(A_REVERSE);
    }

    if (statusCount == 0)
        mvprintw(4, 2, "(no statuses — press N to add one)");

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
    int  running = 1;
    const char *status = NULL;

    while (running) {
        if (sel >= statusCount && statusCount > 0) sel = statusCount - 1;
        if (sel < 0) sel = 0;

        renderList(sel, status);
        status = NULL;
        int ch = getch();

        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--;                 break;
            case KEY_DOWN: if (sel < statusCount - 1) sel++;   break;

            case '\n': case KEY_ENTER:
                if (statusCount > 0) screenEdit(sel);
                break;

            case 'n': case 'N':
                if (statusCount < STATUS_DEF_MAX) {
                    memset(&statuses[statusCount], 0, sizeof(StatusDef));
                    statuses[statusCount].id = (uint8_t)statusCount;
                    sel = statusCount++;
                    dirty = 1;
                    screenEdit(sel);
                } else {
                    status = "Max statuses reached.";
                }
                break;

            case 'd': case 'D':
                if (statusCount > 0) {
                    for (int i = sel; i < statusCount - 1; i++)
                        statuses[i] = statuses[i + 1];
                    statusCount--;
                    dirty = 1;
                    status = "Status deleted.";
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
