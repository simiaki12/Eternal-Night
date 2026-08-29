/*
 * action_editor.c — ncurses editor for assets/data/actions.dat
 *
 * Navigation:
 *   SCR_LIST  Up/Down=select  N=new  D=delete  Enter=edit  S=save  Q=quit
 *   SCR_EDIT  Up/Down=field   +/-=change numeric  Enter=edit text  Bksp=back
 */

#include <ncurses.h>
#include "refs.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- mirror of src/gameplay/actions.h and domains.h (keep in sync) ---- */
#define ACT_CTX_FIRST_TURN    (1<<0)
#define ACT_CTX_ENEMY_WEAPON  (1<<1)
#define ACT_CTX_EXECUTABLE    (1<<2)
#define ACT_CTX_CAN_STUN      (1<<3)
#define ACT_CTX_PLAYER_HURT   (1<<4)
#define ACT_CTX_REQUIRES_DARK (1<<5)
#define ACT_CTX_BLOCKED_HOLY  (1<<6)
#define ACT_CTX_ROUTED        (1<<7)

#define ACT_CAT_COMBAT        (1<<0)
#define ACT_CAT_SOCIAL        (1<<1)
#define ACT_CAT_INVESTIGATION (1<<2)
#define ACT_CAT_HUNT          (1<<3)
#define ACT_CAT_ENVIRONMENTAL (1<<4)

#define DOMAIN_COMBAT   0
#define DOMAIN_TRICKERY 1
#define DOMAIN_BLOOD    2
#define DOMAIN_CHARM    3
#define DOMAIN_NONE     0xFF

#define ACT_FLAG_STARTER     (1<<0)
#define ACT_FLAG_ALL_TARGETS (1<<1)

#define ACTION_MAX 64

typedef struct { uint8_t type, value, chance; } Effect;
#define GRAPH_STATES 6
typedef struct { uint8_t progress[GRAPH_STATES][GRAPH_STATES]; } TransMatrix;

#define ACT_TRANSITIONS 3
#define ENC_TYPE_COUNT  5
#define EFX_COUNT       10

typedef struct {
    uint8_t  id;
    uint8_t  contextFlags;
    uint8_t  baseWeight;
    char     name[16];
    char     imgName[8];
    char     desc[32];
    uint8_t  domain;
    uint8_t  encounterCat;
    uint8_t  actionFlags;
    uint8_t    graphMask;     /* bit per encounter type; derived on save */
    Effect     onPlay;
    Effect     fallback;
    TransMatrix mats[ENC_TYPE_COUNT]; /* [type].progress[from][to] */
} ActionDef; /* 98 bytes */

typedef char _check_size[(sizeof(ActionDef) == 249) ? 1 : -1];
#define ACT_DISK_HEAD 69

static const char *encTypeNames[ENC_TYPE_COUNT] = {
    "combat", "social", "invest", "hunt", "env"
};

/* Live from states.dat when the graph is known, so renamed states show up
   here without touching this file. */
static const char *stateName(int graph, uint8_t s) {
    if (graph < 0 || graph >= ENC_TYPE_COUNT) return "?";
    return refLabel((RefKind)(REF_STATE_COMBAT + graph), s);
}

static const char *efxNames[EFX_COUNT] = {
    "none", "progress", "prog_next", "heal_hp", "dmg_hp",
    "extra_act", "status+", "status-", "meter", "kill"
};

/* A graph counts as used when any cell is non-zero — derived rather than
   tracked, so there is no count to fall out of sync with the data. */
static uint8_t deriveGraphMask(const ActionDef *a) {
    uint8_t mask = 0;
    for (int t = 0; t < ENC_TYPE_COUNT; t++)
        for (int i = 0; i < GRAPH_STATES; i++)
            for (int j = 0; j < GRAPH_STATES; j++)
                if (a->mats[t].progress[i][j]) { mask |= (uint8_t)(1u << t); i = GRAPH_STATES; break; }
    return mask;
}

/* States actually defined for a graph, from states.dat via refs.h. */
static int graphStateCount(int g) {
    int n = refGet((RefKind)(REF_STATE_COMBAT + g))->count;
    return n > GRAPH_STATES ? GRAPH_STATES : n;
}
/* ----------------------------------------------------------------------- */

#define SCROLL_MARGIN 3

static void scroll_to(int sel, int *scroll, int visible) {
    if (sel - *scroll < SCROLL_MARGIN)               *scroll = sel - SCROLL_MARGIN;
    if (sel - *scroll > visible - SCROLL_MARGIN - 1)  *scroll = sel - visible + SCROLL_MARGIN + 1;
    if (*scroll < 0) *scroll = 0;
}

static ActionDef actions[ACTION_MAX];
static int       actionCount = 0;
static int       dirty       = 0;
static const char *outfile   = "assets/data/actions.dat";

/* ---- file I/O ---- */

static void sortById(void) {
    for (int i = 1; i < actionCount; i++) {
        ActionDef tmp = actions[i];
        int j = i - 1;
        while (j >= 0 && actions[j].id > tmp.id) { actions[j+1] = actions[j]; j--; }
        actions[j+1] = tmp;
    }
}

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) { actionCount = 0; return; }
    uint8_t n;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > ACTION_MAX) n = ACTION_MAX;
    actionCount = 0;
    for (int i = 0; i < n; i++) {
        ActionDef *a = &actions[actionCount];
        memset(a, 0, sizeof(*a));
        if (fread(a, 1, ACT_DISK_HEAD, f) != ACT_DISK_HEAD) break;
        int ok = 1;
        for (int t = 0; t < ENC_TYPE_COUNT; t++) {
            if (!((a->graphMask >> t) & 1)) continue;
            if (fread(&a->mats[t], 1, sizeof(TransMatrix), f) != sizeof(TransMatrix)) { ok = 0; break; }
        }
        if (!ok) break;
        actionCount++;
    }
    fclose(f);
    sortById();
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)actionCount;
    fwrite(&n, 1, 1, f);
    for (int i = 0; i < actionCount; i++) {
        ActionDef *a = &actions[i];
        a->graphMask = deriveGraphMask(a);
        fwrite(a, 1, ACT_DISK_HEAD, f);
        for (int t = 0; t < ENC_TYPE_COUNT; t++)
            if ((a->graphMask >> t) & 1)
                fwrite(&a->mats[t], 1, sizeof(TransMatrix), f);
    }
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
    F_DESC,
    F_IMG,
    F_WEIGHT,
    F_DOMAIN,
    F_ENCOUNTER_CAT,
    F_STARTER,
    F_ALL_TARGETS,
    F_CTX_FIRST_TURN,
    F_CTX_ENEMY_WEAPON,
    F_CTX_EXECUTABLE,
    F_CTX_CAN_STUN,
    F_CTX_PLAYER_HURT,
    F_CTX_REQUIRES_DARK,
    F_CTX_BLOCKED_HOLY,
    F_CTX_ROUTED,
    F_COUNT
} Field;

static const char *fieldNames[] = {
    "ID",
    "Name",
    "Description",
    "Image (sprite base)",
    "Base weight",
    "Domain (0=Combat 1=Trickery 2=Blood 3=Charm FF=none)",
    "Encounter cat (bit: 1=combat 2=social 4=invest 8=hunt 10=env)",
    "Starter (available without any domain unlock)",
    "Hits every enemy (combat)",
    "Ctx: first turn only",
    "Ctx: enemy has weapon",
    "Ctx: enemy executable",
    "Ctx: enemy stunnable",
    "Ctx: player hurt (<50%)",
    "Ctx: requires darkness",
    "Ctx: blocked on holy ground",
    "Ctx: hunt group routed (Terrified/Broken)",
};

static void renderEdit(ActionDef *a, int sel, const char *status) {
    clear();
    mvprintw(0, 0, "ACTION EDITOR — %s (id %d)", a->name[0] ? a->name : "(unnamed)", a->id);
    mvprintw(1, 0, "Up/Down=field  +/-=change  Enter=edit text  T=transitions  Bksp=back  S=save");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = 0; i < F_COUNT; i++) {
        if (i == sel) attron(A_REVERSE);
        int row = i + 4;
        switch (i) {
            case F_ID:     mvprintw(row, 2, "%-42s  %d",  fieldNames[i], a->id);         break;
            case F_NAME:   mvprintw(row, 2, "%-42s  %s",  fieldNames[i], a->name);       break;
            case F_DESC:   mvprintw(row, 2, "%-42s  %s",  fieldNames[i], a->desc);       break;
            case F_IMG:    mvprintw(row, 2, "%-42s  %s",  fieldNames[i], a->imgName);    break;
            case F_WEIGHT: mvprintw(row, 2, "%-42s  %d",  fieldNames[i], a->baseWeight); break;
            case F_DOMAIN:
                if (a->domain == DOMAIN_NONE)
                    mvprintw(row, 2, "%-42s  none (FF)", fieldNames[i]);
                else
                    mvprintw(row, 2, "%-42s  %d", fieldNames[i], a->domain);
                break;
            case F_ENCOUNTER_CAT: {
                char cats[48] = "";
                if (a->encounterCat & ACT_CAT_COMBAT)        strcat(cats, "combat ");
                if (a->encounterCat & ACT_CAT_SOCIAL)        strcat(cats, "social ");
                if (a->encounterCat & ACT_CAT_INVESTIGATION) strcat(cats, "invest ");
                if (a->encounterCat & ACT_CAT_HUNT)          strcat(cats, "hunt ");
                if (a->encounterCat & ACT_CAT_ENVIRONMENTAL) strcat(cats, "env ");
                if (!cats[0]) strcat(cats, "(universal)");
                mvprintw(row, 2, "%-42s  %s", fieldNames[i], cats);
                break;
            }
            case F_STARTER:
                mvprintw(row, 2, "%-42s  %s", fieldNames[i], (a->actionFlags & ACT_FLAG_STARTER) ? "[X]" : "[ ]"); break;
            case F_ALL_TARGETS:
                mvprintw(row, 2, "%-42s  %s", fieldNames[i], (a->actionFlags & ACT_FLAG_ALL_TARGETS) ? "[X]" : "[ ]"); break;
            case F_CTX_FIRST_TURN:
                mvprintw(row, 2, "%-42s  %s", fieldNames[i], (a->contextFlags & ACT_CTX_FIRST_TURN)   ? "[X]" : "[ ]"); break;
            case F_CTX_ENEMY_WEAPON:
                mvprintw(row, 2, "%-42s  %s", fieldNames[i], (a->contextFlags & ACT_CTX_ENEMY_WEAPON) ? "[X]" : "[ ]"); break;
            case F_CTX_EXECUTABLE:
                mvprintw(row, 2, "%-42s  %s", fieldNames[i], (a->contextFlags & ACT_CTX_EXECUTABLE)   ? "[X]" : "[ ]"); break;
            case F_CTX_CAN_STUN:
                mvprintw(row, 2, "%-42s  %s", fieldNames[i], (a->contextFlags & ACT_CTX_CAN_STUN)     ? "[X]" : "[ ]"); break;
            case F_CTX_PLAYER_HURT:
                mvprintw(row, 2, "%-42s  %s", fieldNames[i], (a->contextFlags & ACT_CTX_PLAYER_HURT)  ? "[X]" : "[ ]"); break;
            case F_CTX_REQUIRES_DARK:
                mvprintw(row, 2, "%-42s  %s", fieldNames[i], (a->contextFlags & ACT_CTX_REQUIRES_DARK) ? "[X]" : "[ ]"); break;
            case F_CTX_BLOCKED_HOLY:
                mvprintw(row, 2, "%-42s  %s", fieldNames[i], (a->contextFlags & ACT_CTX_BLOCKED_HOLY)  ? "[X]" : "[ ]"); break;
            case F_CTX_ROUTED:
                mvprintw(row, 2, "%-42s  %s", fieldNames[i], (a->contextFlags & ACT_CTX_ROUTED)        ? "[X]" : "[ ]"); break;
        }
        if (i == sel) attroff(A_REVERSE);
    }
    refresh();
}

/* ---- transitions / effects sub-screen ----------------------------------
 * One graph at a time, shown as a from x to grid: the cell at row `from`,
 * column `to` is the progress that edge banks. 0 means the route does not
 * exist from that state. Tab cycles which graph you are looking at.
 * ----------------------------------------------------------------------- */

#define T_FX_FIELDS 6   /* onPlay type/value/chance, fallback type/value/chance */

static int tGraph = 0;  /* graph being edited */
static int tRow = 0, tCol = 0;
static int tFocus = 0;  /* 0 = grid, 1 = effect fields */
static int tFxSel = 0;

static void renderTransitions(ActionDef *a, const char *status) {
    clear();
    mvprintw(0, 0, "TRANSITIONS - %s (id %d)", a->name[0] ? a->name : "(unnamed)", a->id);
    mvprintw(1, 0, "Arrows=move  +/-=change  PgUp/PgDn=+-10  Tab=graph  Enter=pick status  Bksp=back  S=save");
    if (status) mvprintw(2, 0, "%s", status);

    int n = graphStateCount(tGraph);
    mvprintw(4, 2, "Graph: %-8s  (%d states)   used by this action: %s",
             encTypeNames[tGraph], n,
             (deriveGraphMask(a) >> tGraph) & 1 ? "yes" : "no");

    /* Column headings — destinations */
    const int C0 = 16, CW = 9;
    for (int j = 0; j < n; j++)
        mvprintw(6, C0 + j * CW, "%-8.8s", stateName(tGraph, (uint8_t)j));
    mvprintw(6, 2, "from \\ to");

    for (int i = 0; i < n; i++) {
        int row = 7 + i;
        mvprintw(row, 2, "%-13.13s", stateName(tGraph, (uint8_t)i));
        for (int j = 0; j < n; j++) {
            int cur = (tFocus == 0 && i == tRow && j == tCol);
            uint8_t v = a->mats[tGraph].progress[i][j];
            if (cur) attron(A_REVERSE);
            if (v) mvprintw(row, C0 + j * CW, "%4d", v);
            else   mvprintw(row, C0 + j * CW, "   .");
            if (cur) attroff(A_REVERSE);
        }
    }

    /* Effects */
    int erow = 7 + n + 2;
    for (int i = 0; i < T_FX_FIELDS; i++) {
        Effect *e   = i < 3 ? &a->onPlay : &a->fallback;
        int     part = i % 3;
        const char *who = i < 3 ? "onPlay  " : "fallback";
        int cur = (tFocus == 1 && i == tFxSel);
        if (cur) attron(A_REVERSE);
        if (part == 0)
            mvprintw(erow + i, 2, "%s effect   %-14s", who,
                     e->type < EFX_COUNT ? efxNames[e->type] : "?");
        else if (part == 1) {
            if (e->type == 6 || e->type == 7)
                mvprintw(erow + i, 2, "%s value    %-14s", who, refLabel(REF_STATUS, e->value));
            else
                mvprintw(erow + i, 2, "%s value    %-14d", who, e->value);
        } else
            mvprintw(erow + i, 2, "%s chance   %-14d", who, e->chance);
        if (cur) attroff(A_REVERSE);
    }

    mvprintw(erow + T_FX_FIELDS + 1, 2,
             "A row reads: from this state, how much progress each destination banks.");
    refresh();
}

static void tAdjust(ActionDef *a, int dir, int big) {
    int step = big ? 10 : 1;
    dirty = 1;
    if (tFocus == 0) {
        int nv = (int)a->mats[tGraph].progress[tRow][tCol] + dir * step;
        a->mats[tGraph].progress[tRow][tCol] = (uint8_t)(nv < 0 ? 0 : nv > 100 ? 100 : nv);
        return;
    }
    Effect *e    = tFxSel < 3 ? &a->onPlay : &a->fallback;
    int     part = tFxSel % 3;
    if (part == 0) {
        e->type = (uint8_t)((e->type + (dir > 0 ? 1 : EFX_COUNT - 1)) % EFX_COUNT);
    } else {
        uint8_t *v  = part == 1 ? &e->value : &e->chance;
        int      hi = part == 1 ? 255 : 100;
        int nv = (int)*v + dir * step;
        *v = (uint8_t)(nv < 0 ? 0 : nv > hi ? hi : nv);
    }
}

static void screenTransitions(ActionDef *a) {
    const char *status = NULL;
    while (1) {
        int n = graphStateCount(tGraph);
        if (n < 1) n = 1;
        if (tRow >= n) tRow = n - 1;
        if (tCol >= n) tCol = n - 1;

        renderTransitions(a, status);
        status = NULL;
        int ch = getch();
        switch (ch) {
            case '\t':
                tGraph = (tGraph + 1) % ENC_TYPE_COUNT;
                tRow = tCol = 0;
                break;
            case KEY_UP:
                if (tFocus == 1) {
                    if (tFxSel > 0) tFxSel--;
                    else { tFocus = 0; tRow = n - 1; }
                } else if (tRow > 0) tRow--;
                break;
            case KEY_DOWN:
                if (tFocus == 0) {
                    if (tRow < n - 1) tRow++;
                    else { tFocus = 1; tFxSel = 0; }
                } else if (tFxSel < T_FX_FIELDS - 1) tFxSel++;
                break;
            case KEY_LEFT:
                if (tFocus == 0) { if (tCol > 0) tCol--; }
                else tAdjust(a, -1, 0);
                break;
            case KEY_RIGHT:
                if (tFocus == 0) { if (tCol < n - 1) tCol++; }
                else tAdjust(a, 1, 0);
                break;
            case '\n': case KEY_ENTER: {
                /* Enter opens the status picker on a status-valued effect */
                if (tFocus == 1 && (tFxSel == 1 || tFxSel == 4)) {
                    Effect *e = tFxSel == 1 ? &a->onPlay : &a->fallback;
                    if (e->type == 6 || e->type == 7) {
                        e->value = refPick(REF_STATUS, e->value);
                        dirty = 1;
                    } else {
                        status = "That effect's value is a number, not a status.";
                    }
                }
                break;
            }
            case '+': case '=': tAdjust(a,  1, 0); break;
            case '-':           tAdjust(a, -1, 0); break;
            case KEY_PPAGE:     tAdjust(a,  1, 1); break;
            case KEY_NPAGE:     tAdjust(a, -1, 1); break;
            case 's': case 'S': save(); status = "Saved."; break;
            case KEY_BACKSPACE: case 127: return;
        }
    }
}


static void screenEdit(int idx) {
    ActionDef  *a      = &actions[idx];
    int         sel    = 0;
    const char *status = NULL;

    while (1) {
        renderEdit(a, sel, status);
        status = NULL;
        int ch = getch();

        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--; break;
            case KEY_DOWN: if (sel < F_COUNT - 1) sel++; break;

            case KEY_BACKSPACE: case 127: return;

            case 's': case 'S': save(); status = "Saved."; break;

            case 't': case 'T': screenTransitions(a); break;

            case '\n': case KEY_ENTER:
                if (sel == F_NAME) {
                    if (editString(sel + 4, 28, a->name, 16)) dirty = 1;
                } else if (sel == F_DESC) {
                    if (editString(sel + 4, 28, a->desc, 32)) dirty = 1;
                } else if (sel == F_IMG) {
                    if (editString(sel + 4, 28, a->imgName, 8)) dirty = 1;
                }
                break;

            case '+': case '=':
                dirty = 1;
                switch (sel) {
                    case F_ID:              if (a->id           < 255) a->id++;           break;
                    case F_WEIGHT:          if (a->baseWeight   < 255) a->baseWeight++;   break;
                    case F_DOMAIN:          if (a->domain       < 254) a->domain++;
                                            else a->domain = DOMAIN_NONE;                 break;
                    case F_ENCOUNTER_CAT:   if (a->encounterCat < 255) a->encounterCat++; break;
                    case F_STARTER:           a->actionFlags  ^= ACT_FLAG_STARTER;         break;
                    case F_ALL_TARGETS:       a->actionFlags  ^= ACT_FLAG_ALL_TARGETS;     break;
                    case F_CTX_FIRST_TURN:    a->contextFlags ^= ACT_CTX_FIRST_TURN;      break;
                    case F_CTX_ENEMY_WEAPON:  a->contextFlags ^= ACT_CTX_ENEMY_WEAPON;    break;
                    case F_CTX_EXECUTABLE:    a->contextFlags ^= ACT_CTX_EXECUTABLE;      break;
                    case F_CTX_CAN_STUN:      a->contextFlags ^= ACT_CTX_CAN_STUN;        break;
                    case F_CTX_PLAYER_HURT:   a->contextFlags ^= ACT_CTX_PLAYER_HURT;     break;
                    case F_CTX_REQUIRES_DARK: a->contextFlags ^= ACT_CTX_REQUIRES_DARK;   break;
                    case F_CTX_BLOCKED_HOLY:  a->contextFlags ^= ACT_CTX_BLOCKED_HOLY;    break;
                    default: dirty = 0; break;
                }
                break;

            case '-':
                dirty = 1;
                switch (sel) {
                    case F_ID:              if (a->id         > 0) a->id--;           break;
                    case F_WEIGHT:          if (a->baseWeight > 1) a->baseWeight--;   break;
                    case F_DOMAIN:          if (a->domain     > 0 && a->domain != DOMAIN_NONE) a->domain--;
                                            else a->domain = DOMAIN_NONE;             break;
                    case F_ENCOUNTER_CAT:   if (a->encounterCat > 0) a->encounterCat--; break;
                    case F_STARTER:           a->actionFlags  ^= ACT_FLAG_STARTER;       break;
                    case F_CTX_FIRST_TURN:    a->contextFlags ^= ACT_CTX_FIRST_TURN;    break;
                    case F_CTX_ENEMY_WEAPON:  a->contextFlags ^= ACT_CTX_ENEMY_WEAPON;  break;
                    case F_CTX_EXECUTABLE:    a->contextFlags ^= ACT_CTX_EXECUTABLE;    break;
                    case F_CTX_CAN_STUN:      a->contextFlags ^= ACT_CTX_CAN_STUN;      break;
                    case F_CTX_PLAYER_HURT:   a->contextFlags ^= ACT_CTX_PLAYER_HURT;   break;
                    case F_CTX_REQUIRES_DARK: a->contextFlags ^= ACT_CTX_REQUIRES_DARK; break;
                    case F_CTX_BLOCKED_HOLY:  a->contextFlags ^= ACT_CTX_BLOCKED_HOLY;  break;
                    case F_CTX_ROUTED:        a->contextFlags ^= ACT_CTX_ROUTED;        break;
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
    mvprintw(0, 0, "ACTION LIST  [%s]  [%d/%d]", dirty ? "unsaved" : "saved",
        actionCount > 0 ? sel + 1 : 0, actionCount);
    mvprintw(1, 0, "Up/Down=select  Enter=edit  N=new  D=delete  S=save  Q=quit");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = scroll; i < actionCount && i < scroll + visible; i++) {
        if (i == sel) attron(A_REVERSE);
        char ctx[6] = "-----";
        if (actions[i].contextFlags & ACT_CTX_FIRST_TURN)   ctx[0] = 'F';
        if (actions[i].contextFlags & ACT_CTX_ENEMY_WEAPON) ctx[1] = 'W';
        if (actions[i].contextFlags & ACT_CTX_EXECUTABLE)   ctx[2] = 'X';
        if (actions[i].contextFlags & ACT_CTX_CAN_STUN)     ctx[3] = 'S';
        if (actions[i].contextFlags & ACT_CTX_PLAYER_HURT)  ctx[4] = 'H';
        char cat[6] = ".....";
        if (actions[i].encounterCat & ACT_CAT_COMBAT)        cat[0] = 'C';
        if (actions[i].encounterCat & ACT_CAT_SOCIAL)        cat[1] = 'S';
        if (actions[i].encounterCat & ACT_CAT_INVESTIGATION) cat[2] = 'I';
        if (actions[i].encounterCat & ACT_CAT_HUNT)          cat[3] = 'H';
        if (actions[i].encounterCat & ACT_CAT_ENVIRONMENTAL) cat[4] = 'E';
        /* Which graphs this action actually moves — one letter per type. */
        uint8_t gm = deriveGraphMask(&actions[i]);
        char gr[6] = ".....";
        if (gm & (1<<0)) gr[0] = 'c';
        if (gm & (1<<1)) gr[1] = 's';
        if (gm & (1<<2)) gr[2] = 'i';
        if (gm & (1<<3)) gr[3] = 'h';
        if (gm & (1<<4)) gr[4] = 'e';
        mvprintw((i - scroll) + 4, 2, "%2d  id:%-3d  %-16s  wt:%-3d  gr:[%s]  ctx:[%s]  cat:[%s]  img:%s",
            i,
            actions[i].id,
            actions[i].name[0] ? actions[i].name : "(unnamed)",
            actions[i].baseWeight,
            gr,
            ctx,
            cat,
            actions[i].imgName[0] ? actions[i].imgName : "--");
        if (i == sel) attroff(A_REVERSE);
    }

    if (actionCount == 0)
        mvprintw(4, 2, "(no actions — press N to add one)");

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
        if (sel >= actionCount && actionCount > 0) sel = actionCount - 1;
        if (sel < 0) sel = 0;
        scroll_to(sel, &scroll, LINES - 4);

        renderList(sel, scroll, status);
        status = NULL;
        int ch = getch();

        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--; break;
            case KEY_DOWN: if (sel < actionCount - 1) sel++; break;

            case '\n': case KEY_ENTER:
                if (actionCount > 0) screenEdit(sel);
                break;

            case 'n': case 'N':
                if (actionCount < ACTION_MAX) {
                    memset(&actions[actionCount], 0, sizeof(ActionDef));
                    actions[actionCount].baseWeight = 30;
                    sel = actionCount++;
                    dirty = 1;
                    screenEdit(sel);
                } else {
                    status = "Max actions reached.";
                }
                break;

            case 'd': case 'D':
                if (actionCount > 0) {
                    for (int i = sel; i < actionCount - 1; i++)
                        actions[i] = actions[i + 1];
                    actionCount--;
                    dirty = 1;
                    status = "Action deleted.";
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
