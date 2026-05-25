/* tools/hunt_encounter_editor.c — ncurses editor for hunt_encounters.dat
 *
 * Screens:
 *   SCR_LIST  — encounter list
 *   SCR_ENC   — encounter fields + state sub-list
 *   SCR_STATE — state fields + edge sub-list
 *   SCR_EDGE  — edge fields
 *
 * Navigation:
 *   Up/Down  = move selection
 *   Enter    = drill in / confirm text edit
 *   Esc      = go back one level
 *   E        = new encounter (SCR_LIST), new state (SCR_ENC), new edge (SCR_STATE)
 *   D        = delete selected item
 *   Tab      = toggle focus (enc / state list on SCR_ENC; state / edge list on SCR_STATE)
 *   +/-      = increment/decrement numeric fields
 *   T        = toggle flag bit
 *   S        = save
 *   Q        = quit (prompts to save)
 */

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- mirror of hunt_encounter.h ---- */
#define HUNT_ENC_MAX    16
#define HUNT_STATE_MAX   8
#define HUNT_EDGE_MAX    4
#define HUNT_ACTION_MAX  3

#define HUNT_STATE_TERMINAL (1<<0)
#define HUNT_STATE_SUCCESS  (1<<1)
#define HUNT_STATE_COMBAT   (1<<2)

#define HUNT_FLAG_REPEATABLE (1<<0)

typedef struct {
    uint8_t actionIds[HUNT_ACTION_MAX];
    uint8_t nextState;
    uint8_t setFlag;
    uint8_t _pad[3];
} HuntEdge;
typedef char _chk_e[(sizeof(HuntEdge)==8)?1:-1];

typedef struct {
    char     description[24];
    HuntEdge edges[HUNT_EDGE_MAX];
    uint8_t  edgeCount;
    uint8_t  turnBudget;
    uint8_t  timeoutNext;
    uint8_t  damage;
    uint8_t  flags;
    uint8_t  _pad[3];
} HuntStateDef;
typedef char _chk_s[(sizeof(HuntStateDef)==64)?1:-1];

typedef struct {
    uint8_t      id;
    char         name[24];
    uint8_t      enemyPoolId;
    uint8_t      enemyCount;
    uint8_t      stateCount;
    uint8_t      startState;
    uint8_t      flags;
    uint8_t      _pad[2];
    HuntStateDef states[HUNT_STATE_MAX];
} HuntEncounterDef;
typedef char _chk_enc[(sizeof(HuntEncounterDef)==544)?1:-1];
/* ------------------------------------ */

static HuntEncounterDef encs[HUNT_ENC_MAX];
static int              encCount = 0;
static int              dirty    = 0;

static const char *DAT = "assets/data/hunt_encounters.dat";

/* ---- load / save ---- */
static void loadDat(void) {
    FILE *f = fopen(DAT, "rb");
    if (!f) return;
    uint8_t n; fread(&n, 1, 1, f);
    if (n > HUNT_ENC_MAX) n = HUNT_ENC_MAX;
    encCount = n;
    fread(encs, sizeof(HuntEncounterDef), n, f);
    fclose(f);
}
static void saveDat(void) {
    FILE *f = fopen(DAT, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)encCount;
    fwrite(&n, 1, 1, f);
    fwrite(encs, sizeof(HuntEncounterDef), n, f);
    fclose(f);
    dirty = 0;
}

/* ---- screens ---- */
typedef enum { SCR_LIST, SCR_ENC, SCR_STATE, SCR_EDGE } Screen;
static Screen screen = SCR_LIST;

/* selection indices */
static int selEnc   = 0;
static int selState = 0;
static int selEdge  = 0;
/* field cursor within a screen */
static int selField = 0;
/* focus: 0=fields, 1=sub-list */
static int focus    = 0;

/* ---- helpers ---- */
static void cycleUp(uint8_t *v, uint8_t lo, uint8_t hi) {
    if (*v == 0xFF) *v = hi;
    else if (*v <= lo) *v = 0xFF;
    else (*v)--;
}
static void cycleDown(uint8_t *v, uint8_t lo, uint8_t hi) {
    if (*v == 0xFF) *v = lo;
    else if (*v >= hi) *v = 0xFF;
    else (*v)++;
}

/* inline text edit */
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
    mvprintw(0, 0, "HUNT ENCOUNTER EDITOR  [%d/%d]  S=save  Q=quit  E=new  D=delete",
             encCount > 0 ? selEnc + 1 : 0, encCount);
    for (int i = 0; i < encCount; i++) {
        if (i == selEnc) attron(A_REVERSE);
        mvprintw(2 + i, 2, "[%d] %-24s  pool=%d  enemies=%d  states=%d",
            encs[i].id, encs[i].name,
            encs[i].enemyPoolId, encs[i].enemyCount, encs[i].stateCount);
        if (i == selEnc) attroff(A_REVERSE);
    }
    refresh();
}

static void handleList(int ch) {
    switch (ch) {
        case KEY_UP:   if (selEnc > 0) selEnc--; break;
        case KEY_DOWN: if (selEnc < encCount - 1) selEnc++; break;
        case '\n': case KEY_ENTER:
            if (encCount > 0) { screen = SCR_ENC; selField = 0; focus = 0; }
            break;
        case 'e': case 'E':
            if (encCount < HUNT_ENC_MAX) {
                memset(&encs[encCount], 0, sizeof(HuntEncounterDef));
                encs[encCount].id         = (uint8_t)encCount;
                encs[encCount].enemyPoolId = 0xFF;
                encs[encCount].enemyCount  = 4;
                encs[encCount].startState  = 0;
                for (int s = 0; s < HUNT_STATE_MAX; s++) {
                    encs[encCount].states[s].timeoutNext = 0xFF;
                    for (int e = 0; e < HUNT_EDGE_MAX; e++) {
                        memset(encs[encCount].states[s].edges[e].actionIds, 0xFF, HUNT_ACTION_MAX);
                        encs[encCount].states[s].edges[e].nextState = 0xFF;
                        encs[encCount].states[s].edges[e].setFlag   = 0xFF;
                    }
                }
                selEnc = encCount++;
                dirty = 1;
            }
            break;
        case 'd': case 'D':
            if (encCount > 0) {
                for (int i = selEnc; i < encCount - 1; i++) encs[i] = encs[i+1];
                encCount--;
                if (selEnc >= encCount && selEnc > 0) selEnc--;
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

/* ---- SCR_ENC ---- */
/* fields: 0=id, 1=name, 2=enemyPoolId, 3=enemyCount, 4=startState, 5=flags */
#define ENC_FIELDS 6

static void drawEnc(void) {
    HuntEncounterDef *enc = &encs[selEnc];
    clear();
    mvprintw(0, 0, "ENCOUNTER [%d: %.24s]  Tab=toggle focus  Esc=back  E=new state  D=del state",
             selEnc, enc->name);

    /* field panel */
    const char *fnames[] = { "ID", "Name", "Enemy pool", "Enemy count",
                              "Start state", "Flags" };
    for (int i = 0; i < ENC_FIELDS; i++) {
        int sel = (focus == 0 && i == selField);
        if (sel) attron(A_REVERSE);
        switch (i) {
            case 0: mvprintw(2+i, 2, "%-14s %d", fnames[i], enc->id); break;
            case 1: mvprintw(2+i, 2, "%-14s %-24s", fnames[i], enc->name); break;
            case 2:
                if (enc->enemyPoolId == 0xFF)
                    mvprintw(2+i, 2, "%-14s none (0xFF)", fnames[i]);
                else
                    mvprintw(2+i, 2, "%-14s %d", fnames[i], enc->enemyPoolId);
                break;
            case 3: mvprintw(2+i, 2, "%-14s %d", fnames[i], enc->enemyCount); break;
            case 4: mvprintw(2+i, 2, "%-14s %d", fnames[i], enc->startState); break;
            case 5: mvprintw(2+i, 2, "%-14s %s",
                             fnames[i], (enc->flags & HUNT_FLAG_REPEATABLE) ? "[REPEATABLE]" : "[]");
                    break;
        }
        if (sel) attroff(A_REVERSE);
    }

    /* state sub-list */
    mvprintw(2, 40, "STATES [%d/%d]:", enc->stateCount, HUNT_STATE_MAX);
    for (int s = 0; s < enc->stateCount; s++) {
        int sel = (focus == 1 && s == selState);
        if (sel) attron(A_REVERSE);
        const HuntStateDef *st = &enc->states[s];
        char flags[24] = "";
        if (st->flags & HUNT_STATE_TERMINAL) strcat(flags, "T");
        if (st->flags & HUNT_STATE_SUCCESS)  strcat(flags, "S");
        if (st->flags & HUNT_STATE_COMBAT)   strcat(flags, "C");
        mvprintw(3+s, 40, "[%d] %-20s edges=%d %s",
                 s, st->description, st->edgeCount, flags);
        if (sel) attroff(A_REVERSE);
    }
    refresh();
}

static void handleEnc(int ch) {
    HuntEncounterDef *enc = &encs[selEnc];
    if (focus == 0) {
        switch (ch) {
            case KEY_UP:   if (selField > 0) selField--; break;
            case KEY_DOWN: if (selField < ENC_FIELDS-1) selField++; break;
            case '\t':  focus = 1; if (selState >= enc->stateCount && enc->stateCount > 0) selState = 0; break;
            case '\n': case KEY_ENTER:
                if (selField == 1) { editStr(2+selField, 16, enc->name, 24); dirty=1; }
                break;
            case '+': case '=':
                dirty=1;
                switch (selField) {
                    case 0: enc->id++; break;
                    case 2: cycleDown(&enc->enemyPoolId, 1, 15); break;
                    case 3: if (enc->enemyCount < 255) enc->enemyCount++; break;
                    case 4: if (enc->startState < enc->stateCount-1) enc->startState++; break;
                    case 5: enc->flags ^= HUNT_FLAG_REPEATABLE; break;
                }
                break;
            case '-': case '_':
                dirty=1;
                switch (selField) {
                    case 0: if (enc->id > 0) enc->id--; break;
                    case 2: cycleUp(&enc->enemyPoolId, 1, 15); break;
                    case 3: if (enc->enemyCount > 1) enc->enemyCount--; break;
                    case 4: if (enc->startState > 0) enc->startState--; break;
                    case 5: enc->flags ^= HUNT_FLAG_REPEATABLE; break;
                }
                break;
            case 27: /* Esc */ screen = SCR_LIST; break;
            case 's': case 'S': saveDat(); break;
        }
    } else {
        /* focus on state list */
        switch (ch) {
            case KEY_UP:   if (selState > 0) selState--; break;
            case KEY_DOWN: if (selState < enc->stateCount-1) selState++; break;
            case '\t':  focus = 0; break;
            case '\n': case KEY_ENTER:
                if (enc->stateCount > 0) { screen = SCR_STATE; selField = 0; focus = 0; selEdge = 0; }
                break;
            case 'e': case 'E':
                if (enc->stateCount < HUNT_STATE_MAX) {
                    HuntStateDef *st = &enc->states[enc->stateCount];
                    memset(st, 0, sizeof(*st));
                    st->timeoutNext = 0xFF;
                    for (int e = 0; e < HUNT_EDGE_MAX; e++) {
                        memset(st->edges[e].actionIds, 0xFF, HUNT_ACTION_MAX);
                        st->edges[e].nextState = 0xFF;
                        st->edges[e].setFlag   = 0xFF;
                    }
                    selState = enc->stateCount++;
                    dirty=1;
                }
                break;
            case 'd': case 'D':
                if (enc->stateCount > 0) {
                    for (int i = selState; i < enc->stateCount-1; i++)
                        enc->states[i] = enc->states[i+1];
                    enc->stateCount--;
                    if (selState >= enc->stateCount && selState > 0) selState--;
                    dirty=1;
                }
                break;
            case 27: focus = 0; break;
            case 's': case 'S': saveDat(); break;
        }
    }
}

/* ---- SCR_STATE ---- */
/* fields: 0=desc, 1=edgeCount, 2=turnBudget, 3=timeoutNext, 4=damage, 5=flags */
#define STATE_FIELDS 6

static void drawState(void) {
    HuntEncounterDef *enc = &encs[selEnc];
    HuntStateDef     *st  = &enc->states[selState];
    clear();
    mvprintw(0, 0, "STATE [%d: %.24s]  Tab=toggle focus  Esc=back  E=new edge  D=del edge",
             selState, st->description);

    const char *fnames[] = { "Description", "Edge count", "Turn budget",
                              "Timeout next", "Damage", "Flags" };
    for (int i = 0; i < STATE_FIELDS; i++) {
        int sel = (focus == 0 && i == selField);
        if (sel) attron(A_REVERSE);
        switch (i) {
            case 0: mvprintw(2+i, 2, "%-15s %-24s", fnames[i], st->description); break;
            case 1: mvprintw(2+i, 2, "%-15s %d", fnames[i], st->edgeCount); break;
            case 2: mvprintw(2+i, 2, "%-15s %d%s", fnames[i], st->turnBudget,
                             st->turnBudget == 0 ? " (none)" : ""); break;
            case 3:
                if (st->timeoutNext == 0xFF)
                    mvprintw(2+i, 2, "%-15s -> combat fallback", fnames[i]);
                else
                    mvprintw(2+i, 2, "%-15s -> state %d", fnames[i], st->timeoutNext);
                break;
            case 4: mvprintw(2+i, 2, "%-15s %d", fnames[i], st->damage); break;
            case 5: {
                char fb[32] = "[";
                if (st->flags & HUNT_STATE_TERMINAL) strcat(fb, "TERMINAL ");
                if (st->flags & HUNT_STATE_SUCCESS)  strcat(fb, "SUCCESS ");
                if (st->flags & HUNT_STATE_COMBAT)   strcat(fb, "COMBAT ");
                strcat(fb, "]");
                mvprintw(2+i, 2, "%-15s %s", fnames[i], fb);
                break;
            }
        }
        if (sel) attroff(A_REVERSE);
    }

    /* edge sub-list */
    mvprintw(2, 50, "EDGES [%d/%d]:", st->edgeCount, HUNT_EDGE_MAX);
    for (int e = 0; e < st->edgeCount; e++) {
        int sel = (focus == 1 && e == selEdge);
        if (sel) attron(A_REVERSE);
        const HuntEdge *edge = &st->edges[e];
        char acts[20] = "";
        for (int a = 0; a < HUNT_ACTION_MAX; a++) {
            if (edge->actionIds[a] == 0xFF) break;
            char tmp[8]; snprintf(tmp, 8, a==0 ? "%d" : ",%d", edge->actionIds[a]);
            strcat(acts, tmp);
        }
        if (edge->nextState == 0xFF)
            mvprintw(3+e, 50, "[%d] acts[%s] -> term", e, acts);
        else
            mvprintw(3+e, 50, "[%d] acts[%s] -> state %d", e, acts, edge->nextState);
        if (sel) attroff(A_REVERSE);
    }
    refresh();
}

static void handleState(int ch) {
    HuntEncounterDef *enc = &encs[selEnc];
    HuntStateDef     *st  = &enc->states[selState];
    if (focus == 0) {
        switch (ch) {
            case KEY_UP:   if (selField > 0) selField--; break;
            case KEY_DOWN: if (selField < STATE_FIELDS-1) selField++; break;
            case '\t':  focus = 1; if (selEdge >= st->edgeCount && st->edgeCount > 0) selEdge = 0; break;
            case '\n': case KEY_ENTER:
                if (selField == 0) { editStr(2, 17, st->description, 24); dirty=1; }
                break;
            case '+': case '=':
                dirty=1;
                switch (selField) {
                    case 1: if (st->edgeCount < HUNT_EDGE_MAX) st->edgeCount++; break;
                    case 2: if (st->turnBudget < 255) st->turnBudget++; break;
                    case 3: cycleDown(&st->timeoutNext, 0, HUNT_STATE_MAX-1); break;
                    case 4: if (st->damage < 255) st->damage++; break;
                    case 5: {
                        uint8_t bits[] = { HUNT_STATE_TERMINAL, HUNT_STATE_SUCCESS, HUNT_STATE_COMBAT };
                        int n = 3;
                        for (int i = 0; i < n; i++) if (!(st->flags & bits[i])) { st->flags |= bits[i]; break; }
                        break;
                    }
                }
                break;
            case '-': case '_':
                dirty=1;
                switch (selField) {
                    case 1: if (st->edgeCount > 0) st->edgeCount--; break;
                    case 2: if (st->turnBudget > 0) st->turnBudget--; break;
                    case 3: cycleUp(&st->timeoutNext, 0, HUNT_STATE_MAX-1); break;
                    case 4: if (st->damage > 0) st->damage--; break;
                    case 5: {
                        uint8_t bits[] = { HUNT_STATE_COMBAT, HUNT_STATE_SUCCESS, HUNT_STATE_TERMINAL };
                        int n = 3;
                        for (int i = 0; i < n; i++) if (st->flags & bits[i]) { st->flags &= ~bits[i]; break; }
                        break;
                    }
                }
                break;
            case 't': case 'T':
                dirty=1;
                switch (selField) {
                    case 5: st->flags ^= HUNT_STATE_TERMINAL; break;
                }
                break;
            case 27: screen = SCR_ENC; focus = 1; break;
            case 's': case 'S': saveDat(); break;
        }
    } else {
        /* focus on edge list */
        switch (ch) {
            case KEY_UP:   if (selEdge > 0) selEdge--; break;
            case KEY_DOWN: if (selEdge < st->edgeCount-1) selEdge++; break;
            case '\t':  focus = 0; break;
            case '\n': case KEY_ENTER:
                if (st->edgeCount > 0) { screen = SCR_EDGE; selField = 0; }
                break;
            case 'e': case 'E':
                if (st->edgeCount < HUNT_EDGE_MAX) {
                    HuntEdge *edge = &st->edges[st->edgeCount];
                    memset(edge->actionIds, 0xFF, HUNT_ACTION_MAX);
                    edge->nextState = 0xFF;
                    edge->setFlag   = 0xFF;
                    memset(edge->_pad, 0, 3);
                    selEdge = st->edgeCount++;
                    dirty=1;
                }
                break;
            case 'd': case 'D':
                if (st->edgeCount > 0) {
                    for (int i = selEdge; i < st->edgeCount-1; i++)
                        st->edges[i] = st->edges[i+1];
                    st->edgeCount--;
                    if (selEdge >= st->edgeCount && selEdge > 0) selEdge--;
                    dirty=1;
                }
                break;
            case 27: focus = 0; break;
            case 's': case 'S': saveDat(); break;
        }
    }
}

/* ---- SCR_EDGE ---- */
/* fields: 0=action0, 1=action1, 2=action2, 3=nextState, 4=setFlag */
#define EDGE_FIELDS 5

static void drawEdge(void) {
    HuntEdge *edge = &encs[selEnc].states[selState].edges[selEdge];
    clear();
    mvprintw(0, 0, "EDGE [enc=%d state=%d edge=%d]  Esc=back  +/-=change  S=save",
             selEnc, selState, selEdge);

    const char *fnames[] = { "Action slot 0", "Action slot 1", "Action slot 2",
                              "Next state", "Set flag" };
    for (int i = 0; i < EDGE_FIELDS; i++) {
        int sel = (i == selField);
        if (sel) attron(A_REVERSE);
        switch (i) {
            case 0: case 1: case 2:
                if (edge->actionIds[i] == 0xFF)
                    mvprintw(2+i, 2, "%-16s unused (0xFF)", fnames[i]);
                else
                    mvprintw(2+i, 2, "%-16s action %d", fnames[i], edge->actionIds[i]);
                break;
            case 3:
                if (edge->nextState == 0xFF)
                    mvprintw(2+i, 2, "%-16s terminal (0xFF)", fnames[i]);
                else
                    mvprintw(2+i, 2, "%-16s state %d", fnames[i], edge->nextState);
                break;
            case 4:
                if (edge->setFlag == 0xFF)
                    mvprintw(2+i, 2, "%-16s none (0xFF)", fnames[i]);
                else
                    mvprintw(2+i, 2, "%-16s flag %d", fnames[i], edge->setFlag);
                break;
        }
        if (sel) attroff(A_REVERSE);
    }
    mvprintw(LINES-2, 0, "Action IDs: Ambush=14 Track=21 SetTrap=17 BloodScent=26 BloodHowl=24 Massacre=36");
    refresh();
}

static void handleEdge(int ch) {
    HuntEdge *edge = &encs[selEnc].states[selState].edges[selEdge];
    switch (ch) {
        case KEY_UP:   if (selField > 0) selField--; break;
        case KEY_DOWN: if (selField < EDGE_FIELDS-1) selField++; break;
        case '+': case '=':
            dirty=1;
            switch (selField) {
                case 0: case 1: case 2:
                    cycleDown(&edge->actionIds[selField], 0, 63); break;
                case 3: cycleDown(&edge->nextState, 0, HUNT_STATE_MAX-1); break;
                case 4: cycleDown(&edge->setFlag, 0, 127); break;
            }
            break;
        case '-': case '_':
            dirty=1;
            switch (selField) {
                case 0: case 1: case 2:
                    cycleUp(&edge->actionIds[selField], 0, 63); break;
                case 3: cycleUp(&edge->nextState, 0, HUNT_STATE_MAX-1); break;
                case 4: cycleUp(&edge->setFlag, 0, 127); break;
            }
            break;
        case 27: screen = SCR_STATE; focus = 1; break;
        case 's': case 'S': saveDat(); break;
    }
}

/* ---- main ---- */
int main(void) {
    loadDat();
    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0);

    while (1) {
        switch (screen) {
            case SCR_LIST:  drawList();  break;
            case SCR_ENC:   drawEnc();   break;
            case SCR_STATE: drawState(); break;
            case SCR_EDGE:  drawEdge();  break;
        }
        int ch = getch();
        switch (screen) {
            case SCR_LIST:  handleList(ch);  break;
            case SCR_ENC:   handleEnc(ch);   break;
            case SCR_STATE: handleState(ch); break;
            case SCR_EDGE:  handleEdge(ch);  break;
        }
    }
}
