/*
 * env_encounter_editor.c — ncurses editor for assets/data/env_encounters.dat
 *
 * Binary format: [1 byte count][N × 544 bytes EnvEncounterDef]
 *
 * Navigation:
 *   SCR_LIST  Up/Down=select  N=new  D=delete  Enter=edit  S=save  Q=quit
 *   SCR_ENC   Up/Down=field   Enter=edit text  +/-=change  S=save  Bksp/Q=back
 *             Tab=cycle to state list   N=new state  D=delete state  Enter on state=edit
 *   SCR_STATE Up/Down=field   Enter=edit text  +/-=change  K=toggle key edge
 *             Tab=cycle to edge list    N=new edge   D=delete edge   Enter on edge=edit
 *   SCR_EDGE  Up/Down=field   +/-=change  Bksp/Q=back
 */

#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ENV_ENC_MAX    32
#define ENV_STATE_MAX   8
#define ENV_EDGE_MAX    4
#define ENV_ACTION_MAX  3

#define ENV_STATE_TERMINAL (1<<0)
#define ENV_STATE_SUCCESS  (1<<1)
#define ENV_FLAG_REPEATABLE (1<<0)
#define ENV_FLAG_STORY      (1<<1)

typedef struct {
    uint8_t actionIds[ENV_ACTION_MAX];
    uint8_t nextState;
    uint8_t setFlag;
    uint8_t rewardItem;
    uint8_t _pad[2];
} EnvEdge;

typedef struct {
    char    description[24];
    EnvEdge edges[ENV_EDGE_MAX];
    uint8_t edgeCount;
    uint8_t turnBudget;
    uint8_t timeoutNext;
    uint8_t damage;
    uint8_t progressGain;
    uint8_t flags;
    uint8_t _pad[2];
} EnvStateDef;

typedef struct {
    uint8_t     id;
    char        name[24];
    uint8_t     progressGoal;
    uint8_t     stateCount;
    uint8_t     startState;
    uint8_t     flags;
    uint8_t     _pad[3];
    EnvStateDef states[ENV_STATE_MAX];
} EnvEncounterDef;

typedef char _ce[(sizeof(EnvEdge)         ==   8) ? 1 : -1];
typedef char _cs[(sizeof(EnvStateDef)     ==  64) ? 1 : -1];
typedef char _cn[(sizeof(EnvEncounterDef) == 544) ? 1 : -1];

#define SCROLL_MARGIN 3

static void scroll_to(int sel, int *scroll, int visible) {
    if (sel - *scroll < SCROLL_MARGIN)               *scroll = sel - SCROLL_MARGIN;
    if (sel - *scroll > visible - SCROLL_MARGIN - 1)  *scroll = sel - visible + SCROLL_MARGIN + 1;
    if (*scroll < 0) *scroll = 0;
}

static EnvEncounterDef encs[ENV_ENC_MAX];
static int             encCount = 0;
static int             dirty    = 0;
static const char     *outfile  = "assets/data/env_encounters.dat";

/* ---- file I/O ---- */

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) { encCount = 0; return; }
    uint8_t n;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > ENV_ENC_MAX) n = ENV_ENC_MAX;
    encCount = (int)fread(encs, sizeof(EnvEncounterDef), n, f);
    fclose(f);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)encCount;
    fwrite(&n, 1, 1, f);
    fwrite(encs, sizeof(EnvEncounterDef), (size_t)encCount, f);
    fclose(f);
    dirty = 0;
}

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

typedef enum { SCR_LIST, SCR_ENC, SCR_STATE, SCR_EDGE } Screen;

static Screen screen   = SCR_LIST;
static int    selEnc   = 0, scrollEnc = 0;
static int    selEncFld = 0;
static int    selSt    = 0;               /* selected state index */
static int    selStFld = 0;
static int    selEdge  = 0;               /* selected edge index */
static int    selEdgeFld = 0;

/* Whether focus is on the field list or the sub-list (state/edge list) in SCR_ENC / SCR_STATE */
static int    focusOnSublist = 0;

/* ================================================================
 * SCR_LIST
 * ================================================================ */

static void drawList(void) {
    clear();
    int visible = LINES - 3;
    scroll_to(selEnc, &scrollEnc, visible);
    mvprintw(0, 0, "ENV ENCOUNTER EDITOR  [%s]  [%d/%d]",
        dirty ? "unsaved" : "saved", encCount > 0 ? selEnc + 1 : 0, encCount);
    mvprintw(1, 0, "Enter=edit  N=new  D=delete  S=save  Q=quit");
    mvprintw(2, 0, "  %-4s  %-5s  %-4s  %-24s  flags", "id", "prog", "sts", "name");

    for (int i = scrollEnc; i < encCount && i < scrollEnc + visible; i++) {
        EnvEncounterDef *e = &encs[i];
        if (i == selEnc) attron(A_REVERSE);
        char flags[16] = "";
        if (e->flags & ENV_FLAG_REPEATABLE) strcat(flags, "RPT ");
        if (e->flags & ENV_FLAG_STORY)      strcat(flags, "STY");
        mvprintw((i - scrollEnc) + 3, 2, "[%2d]  p:%-4d  s:%-3d  %-24s  %s",
            e->id, e->progressGoal, e->stateCount, e->name, flags);
        if (i == selEnc) attroff(A_REVERSE);
    }
    if (encCount == 0)
        mvprintw(3, 2, "(no encounters — press N to add one)");
}

static void handleList(int ch) {
    switch (ch) {
        case KEY_UP:   if (selEnc > 0) selEnc--; break;
        case KEY_DOWN: if (selEnc < encCount - 1) selEnc++; break;
        case '\n': case KEY_ENTER:
            if (encCount > 0) { selEncFld = 0; focusOnSublist = 0; selSt = 0; screen = SCR_ENC; }
            break;
        case 'n': case 'N':
            if (encCount < ENV_ENC_MAX) {
                EnvEncounterDef *e = &encs[encCount];
                memset(e, 0, sizeof(EnvEncounterDef));
                e->id          = (uint8_t)encCount;
                e->progressGoal = 60;
                e->startState  = 0;
                /* init all edges to unused */
                for (int s = 0; s < ENV_STATE_MAX; s++) {
                    for (int ed = 0; ed < ENV_EDGE_MAX; ed++) {
                        memset(e->states[s].edges[ed].actionIds, 0xFF, ENV_ACTION_MAX);
                        e->states[s].edges[ed].nextState  = 0xFF;
                        e->states[s].edges[ed].setFlag    = 0xFF;
                        e->states[s].edges[ed].rewardItem = 0xFF;
                    }
                    e->states[s].timeoutNext = 0xFF;
                    e->states[s].progressGain = 20;
                }
                selEnc = encCount++;
                dirty = 1;
            }
            break;
        case 'd': case 'D':
            if (encCount > 0) {
                for (int i = selEnc; i < encCount - 1; i++) encs[i] = encs[i + 1];
                for (int i = 0; i < encCount - 1; i++) encs[i].id = (uint8_t)i;
                encCount--;
                if (selEnc >= encCount && selEnc > 0) selEnc--;
                dirty = 1;
            }
            break;
        case 's': case 'S': save(); break;
    }
}

/* ================================================================
 * SCR_ENC — encounter header + state list
 * ================================================================ */

typedef enum { EF_NAME=0, EF_PROG_GOAL, EF_START_ST, EF_REPEATABLE, EF_STORY, EF_COUNT } EncField;
static const char *encFldLbl[] = { "Name", "Progress goal", "Start state", "REPEATABLE", "STORY" };

static void drawEnc(void) {
    clear();
    EnvEncounterDef *e = &encs[selEnc];
    mvprintw(0, 0, "ENCOUNTER [%d]  %.24s  [%s]", e->id,
        e->name[0] ? e->name : "(unnamed)", dirty ? "unsaved" : "saved");
    mvprintw(1, 0, "Tab=switch focus  S=save  Bksp/Q=back");

    /* ---- left: encounter fields ---- */
    for (int i = 0; i < EF_COUNT; i++) {
        int row = 3 + i;
        int hi = !focusOnSublist && (i == selEncFld);
        if (hi) attron(A_REVERSE);
        switch (i) {
            case EF_NAME:       mvprintw(row,2,"%-16s  %.24s", encFldLbl[i], e->name); break;
            case EF_PROG_GOAL:  mvprintw(row,2,"%-16s  %d", encFldLbl[i], e->progressGoal); break;
            case EF_START_ST:   mvprintw(row,2,"%-16s  %d", encFldLbl[i], e->startState); break;
            case EF_REPEATABLE: mvprintw(row,2,"%-16s  %s  (T=toggle)", encFldLbl[i],
                (e->flags & ENV_FLAG_REPEATABLE) ? "[X]" : "[ ]"); break;
            case EF_STORY:      mvprintw(row,2,"%-16s  %s  (T=toggle)", encFldLbl[i],
                (e->flags & ENV_FLAG_STORY) ? "[X]" : "[ ]"); break;
        }
        if (hi) attroff(A_REVERSE);
    }

    /* ---- right: state list ---- */
    mvprintw(3, 36, "STATES (%d)  [N=add  D=del  Enter=edit]", e->stateCount);
    for (int s = 0; s < e->stateCount; s++) {
        int row = 4 + s;
        int hi = focusOnSublist && (s == selSt);
        if (hi) attron(A_REVERSE);
        EnvStateDef *st = &e->states[s];
        char fl[8] = "";
        if (st->flags & ENV_STATE_TERMINAL) strcat(fl, "T");
        if (st->flags & ENV_STATE_SUCCESS)  strcat(fl, "S");
        mvprintw(row, 38, "[%d] %-22s  dmg:%-3d  tmo:%-3d  %s",
            s, st->description, st->damage,
            st->timeoutNext == 0xFF ? 255 : st->timeoutNext, fl);
        if (hi) attroff(A_REVERSE);
    }
    if (e->stateCount == 0)
        mvprintw(4, 38, "(no states)");
}

static void handleEnc(int ch) {
    EnvEncounterDef *e = &encs[selEnc];

    if (ch == '\t') { focusOnSublist = !focusOnSublist; return; }

    if (focusOnSublist) {
        switch (ch) {
            case KEY_UP:   if (selSt > 0) selSt--; break;
            case KEY_DOWN: if (selSt < e->stateCount - 1) selSt++; break;
            case '\n': case KEY_ENTER:
                if (e->stateCount > 0) { selStFld = 0; focusOnSublist = 0; selEdge = 0; screen = SCR_STATE; }
                break;
            case 'n': case 'N':
                if (e->stateCount < ENV_STATE_MAX) {
                    EnvStateDef *st = &e->states[e->stateCount];
                    memset(st, 0, sizeof(EnvStateDef));
                    st->timeoutNext  = 0xFF;
                    st->progressGain = 20;
                    for (int ed = 0; ed < ENV_EDGE_MAX; ed++) {
                        memset(st->edges[ed].actionIds, 0xFF, ENV_ACTION_MAX);
                        st->edges[ed].nextState  = 0xFF;
                        st->edges[ed].setFlag    = 0xFF;
                        st->edges[ed].rewardItem = 0xFF;
                    }
                    selSt = e->stateCount++;
                    e->stateCount = (uint8_t)(e->stateCount > ENV_STATE_MAX ? ENV_STATE_MAX : e->stateCount);
                    dirty = 1;
                }
                break;
            case 'd': case 'D':
                if (e->stateCount > 0) {
                    for (int i = selSt; i < e->stateCount - 1; i++)
                        e->states[i] = e->states[i + 1];
                    e->stateCount--;
                    if (selSt >= e->stateCount && selSt > 0) selSt--;
                    dirty = 1;
                }
                break;
        }
        return;
    }

    /* field focus */
    switch (ch) {
        case KEY_UP:   if (selEncFld > 0) selEncFld--; break;
        case KEY_DOWN: if (selEncFld < EF_COUNT - 1) selEncFld++; break;
        case '\n': case KEY_ENTER:
            if (selEncFld == EF_NAME) { if (editString(3+EF_NAME,20,e->name,24)) dirty=1; }
            break;
        case 't': case 'T':
            dirty = 1;
            if (selEncFld == EF_REPEATABLE) e->flags ^= ENV_FLAG_REPEATABLE;
            else if (selEncFld == EF_STORY) e->flags ^= ENV_FLAG_STORY;
            else dirty = 0;
            break;
        case '+': case '=':
            dirty = 1;
            if (selEncFld == EF_PROG_GOAL) { if (e->progressGoal < 255) e->progressGoal++; }
            else if (selEncFld == EF_START_ST) { if (e->startState < e->stateCount - 1) e->startState++; }
            else dirty = 0;
            break;
        case '-':
            dirty = 1;
            if (selEncFld == EF_PROG_GOAL) { if (e->progressGoal > 1) e->progressGoal--; }
            else if (selEncFld == EF_START_ST) { if (e->startState > 0) e->startState--; }
            else dirty = 0;
            break;
        case KEY_BACKSPACE: case 127: case 'q': case 'Q':
            screen = SCR_LIST; selEncFld = 0; focusOnSublist = 0; break;
        case 's': case 'S': save(); break;
    }
}

/* ================================================================
 * SCR_STATE — state fields + edge list
 * ================================================================ */

typedef enum {
    SF_DESC=0, SF_TURN_BUDGET, SF_TIMEOUT_NEXT, SF_DAMAGE,
    SF_PROG_GAIN, SF_TERMINAL, SF_SUCCESS,
    SF_COUNT
} StateField;
static const char *stFldLbl[] = {
    "Description", "Turn budget", "Timeout next", "Damage",
    "Prog gain", "TERMINAL", "SUCCESS"
};

static void drawState(void) {
    clear();
    EnvEncounterDef *e  = &encs[selEnc];
    EnvStateDef     *st = &e->states[selSt];
    mvprintw(0, 0, "STATE [%d]  %.24s  enc:[%d]  [%s]",
        selSt, st->description[0] ? st->description : "(no desc)",
        selEnc, dirty ? "unsaved" : "saved");
    mvprintw(1, 0, "Tab=switch focus  K on edge=toggle key  S=save  Bksp/Q=back");

    /* ---- left: state fields ---- */
    for (int i = 0; i < SF_COUNT; i++) {
        int row = 3 + i;
        int hi = !focusOnSublist && (i == selStFld);
        if (hi) attron(A_REVERSE);
        switch (i) {
            case SF_DESC:
                mvprintw(row,2,"%-14s  %.24s", stFldLbl[i], st->description); break;
            case SF_TURN_BUDGET:
                mvprintw(row,2,"%-14s  %d  (0=none)", stFldLbl[i], st->turnBudget); break;
            case SF_TIMEOUT_NEXT:
                if (st->timeoutNext == 0xFF)
                    mvprintw(row,2,"%-14s  stay (0xFF)", stFldLbl[i]);
                else
                    mvprintw(row,2,"%-14s  -> state %d", stFldLbl[i], st->timeoutNext);
                break;
            case SF_DAMAGE:
                mvprintw(row,2,"%-14s  %d  HP on entry", stFldLbl[i], st->damage); break;
            case SF_PROG_GAIN:
                mvprintw(row,2,"%-14s  %d  per fired edge", stFldLbl[i], st->progressGain); break;
            case SF_TERMINAL:
                mvprintw(row,2,"%-14s  %s  (T=toggle)", stFldLbl[i],
                    (st->flags & ENV_STATE_TERMINAL) ? "[X]" : "[ ]"); break;
            case SF_SUCCESS:
                mvprintw(row,2,"%-14s  %s  (T=toggle)", stFldLbl[i],
                    (st->flags & ENV_STATE_SUCCESS) ? "[X]" : "[ ]"); break;
        }
        if (hi) attroff(A_REVERSE);
    }

    /* ---- right: edge list ---- */
    mvprintw(3, 36, "EDGES (%d)  [N=add  D=del  Enter=edit]", st->edgeCount);
    for (int ed = 0; ed < st->edgeCount; ed++) {
        int row = 4 + ed;
        int hi = focusOnSublist && (ed == selEdge);
        if (hi) attron(A_REVERSE);
        EnvEdge *edge = &st->edges[ed];
        char acts[16] = "";
        int pos = 0;
        for (int a = 0; a < ENV_ACTION_MAX; a++) {
            if (edge->actionIds[a] == 0xFF) continue;
            pos += snprintf(acts + pos, sizeof(acts) - pos, "%d ", edge->actionIds[a]);
        }
        if (edge->nextState == 0xFF)
            mvprintw(row, 38, "[%d] acts:[%-6s]  -> RESOLVE  flag:%d  item:%d",
                ed, acts,
                edge->setFlag == 0xFF ? -1 : edge->setFlag,
                edge->rewardItem == 0xFF ? -1 : edge->rewardItem);
        else
            mvprintw(row, 38, "[%d] acts:[%-6s]  -> state %d",
                ed, acts, edge->nextState);
        if (hi) attroff(A_REVERSE);
    }
    if (st->edgeCount == 0)
        mvprintw(4, 38, "(no edges)");
}

static void handleState(int ch) {
    EnvEncounterDef *e  = &encs[selEnc];
    EnvStateDef     *st = &e->states[selSt];

    if (ch == '\t') { focusOnSublist = !focusOnSublist; return; }

    if (focusOnSublist) {
        switch (ch) {
            case KEY_UP:   if (selEdge > 0) selEdge--; break;
            case KEY_DOWN: if (selEdge < st->edgeCount - 1) selEdge++; break;
            case '\n': case KEY_ENTER:
                if (st->edgeCount > 0) { selEdgeFld = 0; screen = SCR_EDGE; }
                break;
            case 'n': case 'N':
                if (st->edgeCount < ENV_EDGE_MAX) {
                    EnvEdge *ed = &st->edges[st->edgeCount];
                    memset(ed->actionIds, 0xFF, ENV_ACTION_MAX);
                    ed->nextState  = 0xFF;
                    ed->setFlag    = 0xFF;
                    ed->rewardItem = 0xFF;
                    selEdge = st->edgeCount++;
                    dirty = 1;
                }
                break;
            case 'd': case 'D':
                if (st->edgeCount > 0) {
                    for (int i = selEdge; i < st->edgeCount - 1; i++)
                        st->edges[i] = st->edges[i + 1];
                    st->edgeCount--;
                    if (selEdge >= st->edgeCount && selEdge > 0) selEdge--;
                    dirty = 1;
                }
                break;
        }
        return;
    }

    switch (ch) {
        case KEY_UP:   if (selStFld > 0) selStFld--; break;
        case KEY_DOWN: if (selStFld < SF_COUNT - 1) selStFld++; break;
        case '\n': case KEY_ENTER:
            if (selStFld == SF_DESC) { if (editString(3+SF_DESC,18,st->description,24)) dirty=1; }
            break;
        case 't': case 'T':
            dirty = 1;
            if (selStFld == SF_TERMINAL) st->flags ^= ENV_STATE_TERMINAL;
            else if (selStFld == SF_SUCCESS) st->flags ^= ENV_STATE_SUCCESS;
            else dirty = 0;
            break;
        case '+': case '=':
            dirty = 1;
            switch (selStFld) {
                case SF_TURN_BUDGET:   if (st->turnBudget < 255) st->turnBudget++;   break;
                case SF_TIMEOUT_NEXT:
                    st->timeoutNext = (st->timeoutNext == 0xFF) ? 0
                        : (st->timeoutNext < ENV_STATE_MAX - 1 ? st->timeoutNext + 1 : 0xFF);
                    break;
                case SF_DAMAGE:        if (st->damage < 255) st->damage++;           break;
                case SF_PROG_GAIN:     if (st->progressGain < 255) st->progressGain++; break;
                default: dirty = 0; break;
            }
            break;
        case '-':
            dirty = 1;
            switch (selStFld) {
                case SF_TURN_BUDGET:   if (st->turnBudget > 0) st->turnBudget--;     break;
                case SF_TIMEOUT_NEXT:
                    st->timeoutNext = (st->timeoutNext == 0) ? 0xFF
                        : (st->timeoutNext == 0xFF ? (uint8_t)(ENV_STATE_MAX - 1) : st->timeoutNext - 1);
                    break;
                case SF_DAMAGE:        if (st->damage > 0) st->damage--;             break;
                case SF_PROG_GAIN:     if (st->progressGain > 0) st->progressGain--; break;
                default: dirty = 0; break;
            }
            break;
        case KEY_BACKSPACE: case 127: case 'q': case 'Q':
            screen = SCR_ENC; selStFld = 0; focusOnSublist = 1; break;
        case 's': case 'S': save(); break;
    }
}

/* ================================================================
 * SCR_EDGE — edge fields
 * ================================================================ */

typedef enum {
    EDF_ACT0=0, EDF_ACT1, EDF_ACT2,
    EDF_NEXT_STATE,
    EDF_SET_FLAG, EDF_REWARD_ITEM,
    EDF_COUNT
} EdgeField;
static const char *edgeFldLbl[] = {
    "Action ID 0", "Action ID 1", "Action ID 2",
    "Next state",
    "Set world flag", "Reward item",
};

static void drawEdge(void) {
    clear();
    EnvStateDef *st   = &encs[selEnc].states[selSt];
    EnvEdge     *edge = &st->edges[selEdge];
    mvprintw(0, 0, "EDGE [%d]  state:[%d]  enc:[%d]  [%s]",
        selEdge, selSt, selEnc, dirty ? "unsaved" : "saved");
    mvprintw(1, 0, "Up/Down=field  +/-=change  Bksp/Q=back  S=save");

    for (int i = 0; i < EDF_COUNT; i++) {
        int row = 3 + i;
        if (i == selEdgeFld) attron(A_REVERSE);
        switch (i) {
            case EDF_ACT0: case EDF_ACT1: case EDF_ACT2: {
                int j = i - EDF_ACT0;
                if (edge->actionIds[j] == 0xFF)
                    mvprintw(row,2,"%-16s  none (0xFF)", edgeFldLbl[i]);
                else
                    mvprintw(row,2,"%-16s  %d", edgeFldLbl[i], edge->actionIds[j]);
                break;
            }
            case EDF_NEXT_STATE:
                if (edge->nextState == 0xFF)
                    mvprintw(row,2,"%-16s  RESOLVE (terminal)", edgeFldLbl[i]);
                else
                    mvprintw(row,2,"%-16s  -> state %d", edgeFldLbl[i], edge->nextState);
                break;
            case EDF_SET_FLAG:
                if (edge->setFlag == 0xFF)
                    mvprintw(row,2,"%-16s  none (0xFF)", edgeFldLbl[i]);
                else
                    mvprintw(row,2,"%-16s  flag %d", edgeFldLbl[i], edge->setFlag);
                break;
            case EDF_REWARD_ITEM:
                if (edge->rewardItem == 0xFF)
                    mvprintw(row,2,"%-16s  none (0xFF)", edgeFldLbl[i]);
                else
                    mvprintw(row,2,"%-16s  item %d", edgeFldLbl[i], edge->rewardItem);
                break;
        }
        if (i == selEdgeFld) attroff(A_REVERSE);
    }

    mvprintw(3 + EDF_COUNT + 1, 0,
        "nextState 0xFF = terminal resolve (end encounter here)");
    mvprintw(3 + EDF_COUNT + 2, 0,
        "setFlag/rewardItem only apply when nextState == 0xFF (terminal edge)");
}

static uint8_t cycleUp(uint8_t v)   { return v == 0xFF ? 0   : (v < 254 ? v + 1 : 0xFF); }
static uint8_t cycleDown(uint8_t v) { return v == 0    ? 0xFF : (v == 0xFF ? 254 : v - 1); }

static void handleEdge(int ch) {
    EnvEdge *edge = &encs[selEnc].states[selSt].edges[selEdge];

    switch (ch) {
        case KEY_UP:   if (selEdgeFld > 0) selEdgeFld--; break;
        case KEY_DOWN: if (selEdgeFld < EDF_COUNT - 1) selEdgeFld++; break;
        case '+': case '=':
            dirty = 1;
            switch (selEdgeFld) {
                case EDF_ACT0: edge->actionIds[0] = cycleUp(edge->actionIds[0]); break;
                case EDF_ACT1: edge->actionIds[1] = cycleUp(edge->actionIds[1]); break;
                case EDF_ACT2: edge->actionIds[2] = cycleUp(edge->actionIds[2]); break;
                case EDF_NEXT_STATE: edge->nextState  = cycleUp(edge->nextState);  break;
                case EDF_SET_FLAG:   edge->setFlag    = cycleUp(edge->setFlag);    break;
                case EDF_REWARD_ITEM:edge->rewardItem = cycleUp(edge->rewardItem); break;
                default: dirty = 0; break;
            }
            break;
        case '-':
            dirty = 1;
            switch (selEdgeFld) {
                case EDF_ACT0: edge->actionIds[0] = cycleDown(edge->actionIds[0]); break;
                case EDF_ACT1: edge->actionIds[1] = cycleDown(edge->actionIds[1]); break;
                case EDF_ACT2: edge->actionIds[2] = cycleDown(edge->actionIds[2]); break;
                case EDF_NEXT_STATE: edge->nextState  = cycleDown(edge->nextState);  break;
                case EDF_SET_FLAG:   edge->setFlag    = cycleDown(edge->setFlag);    break;
                case EDF_REWARD_ITEM:edge->rewardItem = cycleDown(edge->rewardItem); break;
                default: dirty = 0; break;
            }
            break;
        case KEY_BACKSPACE: case 127: case 'q': case 'Q':
            screen = SCR_STATE; selEdgeFld = 0; focusOnSublist = 1; break;
        case 's': case 'S': save(); break;
    }
}

/* ================================================================
 * main
 * ================================================================ */

int main(void) {
    load();
    initscr(); cbreak(); noecho();
    keypad(stdscr, TRUE); curs_set(0);

    int running = 1;
    while (running) {
        switch (screen) {
            case SCR_LIST:  drawList();  break;
            case SCR_ENC:   drawEnc();   break;
            case SCR_STATE: drawState(); break;
            case SCR_EDGE:  drawEdge();  break;
        }
        refresh();
        int ch = getch();
        if ((ch == 'q' || ch == 'Q') && screen == SCR_LIST) { running = 0; break; }
        switch (screen) {
            case SCR_LIST:  handleList(ch);  break;
            case SCR_ENC:   handleEnc(ch);   break;
            case SCR_STATE: handleState(ch); break;
            case SCR_EDGE:  handleEdge(ch);  break;
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
