/*
 * action_editor.c — ncurses editor for assets/data/actions.dat
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
#define TRANSITION_SOURCES 8
typedef struct { uint8_t to; uint8_t progress[TRANSITION_SOURCES]; } Transition;

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
    Transition transitions[ACT_TRANSITIONS];
    Effect     onPlay;
    Effect     fallback;
    uint16_t   tCounts;       /* 3 bits per encounter-type index (0..4) */
} ActionDef; /* 98 bytes */

typedef char _check_size[(sizeof(ActionDef) == 98) ? 1 : -1];

static const char *encTypeNames[ENC_TYPE_COUNT] = {
    "combat", "social", "invest", "hunt", "env"
};

/* State names per graph — mirror of tools/seed_states.c */
static const char *stateNames[ENC_TYPE_COUNT][TRANSITION_SOURCES] = {
    { "Squaring Up", "Trading", "Staggered", "Frenzied", "Broken", 0, 0, 0 },
    { "Stranger", "Suspicious", "Fearful", "Trusting", "Hostile", "Greedy", 0, 0 },
    { "Cold Trail", "Familiarizing", "Connecting", "Breakthrough", "Unraveled", 0, 0, 0 },
    { "Organized", "Disturbed", "Alerted", "Terrified", "Broken", "Preparing", 0, 0 },
    { "Stable", "Escalating", "Critical", "Resolved", "Disaster", 0, 0, 0 },
};

static const char *stateName(int graph, uint8_t s) {
    if (graph < 0 || graph >= ENC_TYPE_COUNT || s >= TRANSITION_SOURCES) return "?";
    return stateNames[graph][s] ? stateNames[graph][s] : "-";
}

static const char *efxNames[EFX_COUNT] = {
    "none", "progress", "prog_next", "heal_hp", "dmg_hp",
    "extra_act", "status+", "status-", "meter", "kill"
};

#define TCOUNT(a, t)  ((uint8_t)(((a)->tCounts >> ((t) * 3)) & 7u))

static void setTCount(ActionDef *a, int t, uint8_t v) {
    a->tCounts = (uint16_t)((a->tCounts & ~(7u << (t * 3))) | ((v & 7u) << (t * 3)));
}

/* Graph a triplet slot belongs to, by prefix sums of tCounts; -1 = unassigned */
static int slotGraph(const ActionDef *a, int slot) {
    int acc = 0;
    for (int t = 0; t < ENC_TYPE_COUNT; t++) {
        acc += TCOUNT(a, t);
        if (slot < acc) return t;
    }
    return -1;
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
    actionCount = (int)fread(actions, sizeof(ActionDef), n, f);
    fclose(f);
    sortById();
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)actionCount;
    fwrite(&n, 1, 1, f);
    fwrite(actions, sizeof(ActionDef), actionCount, f);
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

/* ---- transitions / effects sub-screen ---- */

typedef enum {
    T_CNT0 = 0, T_CNT1, T_CNT2, T_CNT3, T_CNT4,   /* transitions per type */
    T_S0_DEST, T_S0_SRC, T_S0_VAL,
    T_S1_DEST, T_S1_SRC, T_S1_VAL,
    T_S2_DEST, T_S2_SRC, T_S2_VAL,
    T_OP_TYPE, T_OP_VAL, T_OP_CHANCE,             /* onPlay   */
    T_FB_TYPE, T_FB_VAL, T_FB_CHANCE,             /* fallback */
    T_COUNT
} TField;

/* Which source state each slot is currently being tuned for */
static uint8_t srcSel[ACT_TRANSITIONS];

static void renderTransitions(ActionDef *a, int sel) {
    clear();
    mvprintw(0, 0, "TRANSITIONS - %s (id %d)", a->name[0] ? a->name : "(unnamed)", a->id);
    mvprintw(1, 0, "Up/Down=field  +/-=change  PgUp/PgDn=+-10  Bksp=back  S=save");
    mvprintw(2, 0, "Each transition is one destination + the progress it banks from each source.");

    int total = 0;
    for (int t = 0; t < ENC_TYPE_COUNT; t++) total += TCOUNT(a, t);

    for (int i = 0; i < T_COUNT; i++) {
        int row = i + 4 + (i >= T_S0_DEST) + (i >= T_OP_TYPE) + (i >= T_FB_TYPE);
        if (i == sel) attron(A_REVERSE);
        if (i <= T_CNT4) {
            mvprintw(row, 2, "Transitions: %-35s  %d", encTypeNames[i], TCOUNT(a, i));
        } else if (i < T_OP_TYPE) {
            int slot = (i - T_S0_DEST) / 3, part = (i - T_S0_DEST) % 3;
            int g    = slotGraph(a, slot);
            Transition *tr = &a->transitions[slot];
            char label[52];
            if (part == 0) {
                snprintf(label, sizeof(label), "T%d (%s) destination", slot,
                         g >= 0 ? encTypeNames[g] : "unused");
                mvprintw(row, 2, "%-50s  %s", label, stateName(g, tr->to));
            } else if (part == 1) {
                snprintf(label, sizeof(label), "T%d tuning source", slot);
                mvprintw(row, 2, "%-50s  %s", label, stateName(g, srcSel[slot]));
            } else {
                snprintf(label, sizeof(label), "T%d progress from %s", slot,
                         stateName(g, srcSel[slot]));
                mvprintw(row, 2, "%-50s  %d", label, tr->progress[srcSel[slot]]);
            }
        } else {
            Effect *e = i < T_FB_TYPE ? &a->onPlay : &a->fallback;
            int part  = (i - T_OP_TYPE) % 3;
            const char *who = i < T_FB_TYPE ? "onPlay" : "fallback";
            if (part == 0)
                mvprintw(row, 2, "%s %-43s  %s", who, "effect",
                         e->type < EFX_COUNT ? efxNames[e->type] : "?");
            else if (part == 1)
                mvprintw(row, 2, "%s %-43s  %d", who, "value",  e->value);
            else
                mvprintw(row, 2, "%s %-43s  %d", who, "chance", e->chance);
        }
        if (i == sel) attroff(A_REVERSE);
    }

    /* Whole vector at a glance, so tuning one source keeps the rest visible */
    int row = T_COUNT + 8;
    for (int slot = 0; slot < ACT_TRANSITIONS; slot++) {
        int g = slotGraph(a, slot);
        if (g < 0) continue;
        char line[160];
        int  n = snprintf(line, sizeof(line), "T%d -> %-13s :", slot,
                          stateName(g, a->transitions[slot].to));
        for (int src = 0; src < TRANSITION_SOURCES; src++) {
            uint8_t v = a->transitions[slot].progress[src];
            if (!v) continue;
            n += snprintf(line + n, sizeof(line) - n, "  %s %d", stateName(g, src), v);
        }
        mvprintw(row++, 2, "%s", line);
    }
    mvprintw(row + 1, 2, "Slots used: %d / %d%s", total, ACT_TRANSITIONS,
             total > ACT_TRANSITIONS ? "  !! OVER" : "");
    refresh();
}

static void tfieldAdjust(ActionDef *a, int sel, int dir, int big) {
    int step = big ? 10 : 1;
    dirty = 1;
    if (sel <= T_CNT4) {
        int total = 0;
        for (int t = 0; t < ENC_TYPE_COUNT; t++) total += TCOUNT(a, t);
        uint8_t c = TCOUNT(a, sel);
        if (dir > 0 && c < ACT_TRANSITIONS && total < ACT_TRANSITIONS) setTCount(a, sel, c + 1);
        if (dir < 0 && c > 0)                                          setTCount(a, sel, c - 1);
    } else if (sel < T_OP_TYPE) {
        int slot = (sel - T_S0_DEST) / 3, part = (sel - T_S0_DEST) % 3;
        Transition *tr = &a->transitions[slot];
        if (part == 0) {
            int v = (int)tr->to + dir;
            tr->to = (uint8_t)(v < 0 ? TRANSITION_SOURCES - 1 : v % TRANSITION_SOURCES);
        } else if (part == 1) {
            int v = (int)srcSel[slot] + dir;
            srcSel[slot] = (uint8_t)(v < 0 ? TRANSITION_SOURCES - 1 : v % TRANSITION_SOURCES);
        } else {
            int nv = (int)tr->progress[srcSel[slot]] + dir * step;
            tr->progress[srcSel[slot]] = (uint8_t)(nv < 0 ? 0 : nv > 100 ? 100 : nv);
        }
    } else {
        Effect *e   = sel < T_FB_TYPE ? &a->onPlay : &a->fallback;
        int     part = (sel - T_OP_TYPE) % 3;
        if (part == 0) {
            e->type = (uint8_t)((e->type + (dir > 0 ? 1 : EFX_COUNT - 1)) % EFX_COUNT);
        } else {
            uint8_t *v  = part == 1 ? &e->value : &e->chance;
            int      hi = part == 1 ? 255 : 100;
            int nv = (int)*v + dir * step;
            *v = (uint8_t)(nv < 0 ? 0 : nv > hi ? hi : nv);
        }
    }
}

static void screenTransitions(ActionDef *a) {
    int sel = 0;
    while (1) {
        renderTransitions(a, sel);
        int ch = getch();
        switch (ch) {
            case KEY_UP:        if (sel > 0) sel--;           break;
            case KEY_DOWN:      if (sel < T_COUNT - 1) sel++; break;
            case '+': case '=': tfieldAdjust(a, sel,  1, 0);  break;
            case '-':           tfieldAdjust(a, sel, -1, 0);  break;
            case KEY_NPAGE:     tfieldAdjust(a, sel, -1, 1);  break;
            case KEY_PPAGE:     tfieldAdjust(a, sel,  1, 1);  break;
            case 's': case 'S': save();                       break;
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
        int trips = 0;
        for (int t = 0; t < ENC_TYPE_COUNT; t++) trips += TCOUNT(&actions[i], t);
        mvprintw((i - scroll) + 4, 2, "%2d  id:%-3d  %-16s  wt:%-3d  tr:%-2d  ctx:[%s]  cat:[%s]  img:%s",
            i,
            actions[i].id,
            actions[i].name[0] ? actions[i].name : "(unnamed)",
            actions[i].baseWeight,
            trips,
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
