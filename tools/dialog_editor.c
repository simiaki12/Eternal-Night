/*
 * dialog_editor.c — ncurses editor for dialog.dat
 *
 * Binary format:
 *   [1 byte]  uint8_t tree count
 *   [N × 4049 bytes]  DialogTree structs (direct memcpy, no pointers)
 *
 * Navigation:
 *   SCR_TREES  → SCR_NODES (Enter)
 *   SCR_NODES  → SCR_NODE  (Enter)
 *   SCR_NODE   → SCR_OPTION (Enter on an option row)
 *   Any screen: Backspace/Q = go up one level (Q at top = quit)
 */

#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- mirror of src/world/dialog.h (kept in sync manually) ---- */
#define DIALOG_MAX_OPTIONS  4
#define DIALOG_MAX_NODES   16
#define DIALOG_MAX_TREES   64

/* Domain constants — keep in sync with src/gameplay/domains.h */
#define DOMAIN_COMBAT    0
#define DOMAIN_TRICKERY  1
#define DOMAIN_BLOOD     2
#define DOMAIN_CHARM     3
#define DOMAIN_COUNT    14
#define NO_DOMAIN       0xFF

/* Dialog effect types — keep in sync with src/world/dialog.h */
#define DLGFX_NONE             0
#define DLGFX_SOCIAL_ENCOUNTER 1

static const char *domainNames[] = {
    "Combat", "Trickery", "Blood", "Charm",
    "Fusion A", "Fusion B", "Fusion C", "Fusion D", "Fusion E", "Fusion F",
    "Story A",  "Story B",  "Story C",  "Story D"
};

static const char *effectNames[] = { "None", "Social Encounter" };
#define DLGFX_COUNT 2

typedef struct {
    uint8_t requiredDomain;
    uint8_t requiredLevel;
    int8_t  nextNode;
    uint8_t effect_type;
    uint8_t effect_arg;
    char    text[41];
} DialogOption;

typedef struct {
    char         line[74];
    uint8_t      optionCount;
    DialogOption options[DIALOG_MAX_OPTIONS];
} DialogNode;

typedef struct {
    char       speakerName[32];
    uint8_t    nodeCount;
    DialogNode nodes[DIALOG_MAX_NODES];
} DialogTree;
/* -------------------------------------------------------------- */

static const char *outfile = "assets/data/dialog.dat";

static DialogTree trees[DIALOG_MAX_TREES];
static int        treeCount = 0;
static int        dirty     = 0;

/* ---- packed format helpers ---- */

static int rdByte(const uint8_t *d, uint32_t sz, uint32_t *pos, uint8_t *out) {
    if (*pos >= sz) return 0;
    *out = d[(*pos)++];
    return 1;
}

static int rdStr(const uint8_t *d, uint32_t sz, uint32_t *pos, char *out, int maxLen) {
    uint8_t len;
    if (!rdByte(d, sz, pos, &len)) return 0;
    if (*pos + len > sz) return 0;
    int n = (len < (uint8_t)(maxLen - 1)) ? len : (uint8_t)(maxLen - 1);
    memcpy(out, d + *pos, n);
    out[n] = '\0';
    *pos += len;
    return 1;
}

/* ---- file I/O ---- */

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) { treeCount = 0; return; }

    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz < 1) { fclose(f); return; }

    uint8_t *buf = malloc((size_t)fsz);
    if (!buf) { fclose(f); return; }
    (void)fread(buf, 1, (size_t)fsz, f);
    fclose(f);

    uint32_t pos  = 0;
    uint32_t size = (uint32_t)fsz;
    uint8_t  count = buf[pos++];
    if (count > DIALOG_MAX_TREES) count = DIALOG_MAX_TREES;
    treeCount = 0;

    for (int i = 0; i < (int)count; i++) {
        DialogTree *t = &trees[i];
        memset(t, 0, sizeof(*t));

        if (!rdStr(buf, size, &pos, t->speakerName, 32)) goto done;

        uint8_t nc;
        if (!rdByte(buf, size, &pos, &nc)) goto done;
        if (nc > DIALOG_MAX_NODES) nc = DIALOG_MAX_NODES;
        t->nodeCount = nc;

        for (int j = 0; j < (int)nc; j++) {
            DialogNode *n = &t->nodes[j];
            if (!rdStr(buf, size, &pos, n->line, 74)) goto done;

            uint8_t oc;
            if (!rdByte(buf, size, &pos, &oc)) goto done;
            if (oc > DIALOG_MAX_OPTIONS) oc = DIALOG_MAX_OPTIONS;
            n->optionCount = oc;

            for (int k = 0; k < (int)oc; k++) {
                DialogOption *o = &n->options[k];
                if (!rdByte(buf, size, &pos, &o->requiredDomain))       goto done;
                if (!rdByte(buf, size, &pos, &o->requiredLevel))         goto done;
                if (!rdByte(buf, size, &pos, (uint8_t *)&o->nextNode))   goto done;
                if (!rdByte(buf, size, &pos, &o->effect_type))           goto done;
                if (!rdByte(buf, size, &pos, &o->effect_arg))            goto done;
                if (!rdStr(buf, size, &pos, o->text, 41))                goto done;
            }
        }
        treeCount++;
    }

done:
    free(buf);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;

    uint8_t n = (uint8_t)treeCount;
    fwrite(&n, 1, 1, f);

    for (int i = 0; i < treeCount; i++) {
        DialogTree *t = &trees[i];
        uint8_t nameLen = (uint8_t)strlen(t->speakerName);
        fwrite(&nameLen,      1, 1,       f);
        fwrite(t->speakerName, 1, nameLen, f);
        fwrite(&t->nodeCount,  1, 1,       f);

        for (int j = 0; j < (int)t->nodeCount; j++) {
            DialogNode *nd = &t->nodes[j];
            uint8_t lineLen = (uint8_t)strlen(nd->line);
            fwrite(&lineLen,      1, 1,       f);
            fwrite(nd->line,      1, lineLen, f);
            fwrite(&nd->optionCount, 1, 1,    f);

            for (int k = 0; k < (int)nd->optionCount; k++) {
                DialogOption *o = &nd->options[k];
                uint8_t textLen = (uint8_t)strlen(o->text);
                fwrite(&o->requiredDomain, 1, 1,       f);
                fwrite(&o->requiredLevel,  1, 1,       f);
                fwrite(&o->nextNode,       1, 1,       f);
                fwrite(&o->effect_type,    1, 1,       f);
                fwrite(&o->effect_arg,     1, 1,       f);
                fwrite(&textLen,           1, 1,       f);
                fwrite(o->text,            1, textLen, f);
            }
        }
    }

    fclose(f);
    dirty = 0;
}

/* ---- text editing ---- */

/* Edit a fixed-length string field in-place.
 * Draws the field at (row, col) with a cursor; returns on Enter or Esc. */
static void editStr(int row, int col, char *buf, int maxLen) {
    int len = (int)strlen(buf);
    int pos = len;
    curs_set(1);
    for (;;) {
        /* Draw current content */
        mvhline(row, col, ' ', maxLen + 1);
        mvprintw(row, col, "%s", buf);
        move(row, col + pos);
        refresh();

        int ch = getch();
        if (ch == '\n' || ch == KEY_ENTER) break;
        if (ch == 27 /* ESC */) break;
        if ((ch == KEY_BACKSPACE || ch == 127 || ch == '\b') && pos > 0) {
            memmove(buf + pos - 1, buf + pos, (size_t)(len - pos + 1));
            pos--; len--;
        } else if (ch >= 32 && ch < 127 && len < maxLen - 1) {
            memmove(buf + pos + 1, buf + pos, (size_t)(len - pos + 1));
            buf[pos] = (char)ch;
            pos++; len++;
        }
    }
    curs_set(0);
}

/* ---- domain cycle helper ---- */

static const char *domainLabel(uint8_t d) {
    if (d == NO_DOMAIN) return "None";
    if (d < DOMAIN_COUNT) return domainNames[d];
    return "???";
}

static uint8_t domainNext(uint8_t d) {
    if (d == NO_DOMAIN) return 0;
    if (d + 1 >= DOMAIN_COUNT) return NO_DOMAIN;
    return d + 1;
}

static uint8_t domainPrev(uint8_t d) {
    if (d == NO_DOMAIN) return (uint8_t)(DOMAIN_COUNT - 1);
    if (d == 0) return NO_DOMAIN;
    return d - 1;
}

/* ---- screens ---- */

typedef enum { SCR_TREES, SCR_NODES, SCR_NODE, SCR_OPTION } Screen;

static Screen screen   = SCR_TREES;
static int    selTree  = 0;
static int    selNode  = 0;
static int    selOpt   = 0;   /* option index within node */

/* ------ SCR_TREES ------ */
static void drawTrees(void) {
    clear();
    mvprintw(0, 0, "DIALOG EDITOR  [%s]  trees: %d/%d",
        dirty ? "unsaved" : "saved", treeCount, DIALOG_MAX_TREES);
    mvprintw(1, 0, "Enter=open  A=add  D=delete  S=save  Q=quit");

    for (int i = 0; i < treeCount; i++) {
        if (i == selTree) attron(A_REVERSE);
        mvprintw(i + 3, 2, "%-30s  nodes:%d", trees[i].speakerName, trees[i].nodeCount);
        if (i == selTree) attroff(A_REVERSE);
    }
    if (treeCount == 0)
        mvprintw(3, 2, "(no trees — press A to add one)");
}

static void handleTrees(int ch) {
    switch (ch) {
        case KEY_UP:   if (selTree > 0) selTree--; break;
        case KEY_DOWN: if (selTree < treeCount - 1) selTree++; break;
        case '\n': case KEY_ENTER:
            if (treeCount > 0) { selNode = 0; screen = SCR_NODES; }
            break;
        case 'a': case 'A':
            if (treeCount < DIALOG_MAX_TREES) {
                memset(&trees[treeCount], 0, sizeof(DialogTree));
                strncpy(trees[treeCount].speakerName, "New Speaker", 31);
                selTree = treeCount++;
                dirty = 1;
                /* immediately edit name */
                mvprintw(selTree + 3, 2, "%-30s", "");
                editStr(selTree + 3, 2, trees[selTree].speakerName, 32);
            }
            break;
        case 'd': case 'D':
            if (treeCount > 0) {
                for (int i = selTree; i < treeCount - 1; i++)
                    trees[i] = trees[i + 1];
                treeCount--;
                if (selTree >= treeCount && selTree > 0) selTree--;
                dirty = 1;
            }
            break;
        case 'e': case 'E':
            if (treeCount > 0)
                editStr(selTree + 3, 2, trees[selTree].speakerName, 32);
            dirty = 1;
            break;
        case 's': case 'S': save(); break;
        case 'q': case 'Q': break; /* handled in main loop */
    }
}

/* ------ SCR_NODES ------ */
static void drawNodes(void) {
    clear();
    DialogTree *t = &trees[selTree];
    mvprintw(0, 0, "TREE: %s  [%s]  nodes: %d/%d",
        t->speakerName, dirty ? "unsaved" : "saved", t->nodeCount, DIALOG_MAX_NODES);
    mvprintw(1, 0, "Enter=open  A=add  D=delete  E=edit text  Bksp/Q=back");

    for (int i = 0; i < t->nodeCount; i++) {
        if (i == selNode) attron(A_REVERSE);
        /* truncate line to 50 chars for display */
        char preview[51];
        strncpy(preview, t->nodes[i].line, 50);
        preview[50] = '\0';
        mvprintw(i + 3, 2, "[%2d] opts:%d  \"%s\"", i, t->nodes[i].optionCount, preview);
        if (i == selNode) attroff(A_REVERSE);
    }
    if (t->nodeCount == 0)
        mvprintw(3, 2, "(no nodes — press A to add one)");
}

static void handleNodes(int ch) {
    DialogTree *t = &trees[selTree];
    switch (ch) {
        case KEY_UP:   if (selNode > 0) selNode--; break;
        case KEY_DOWN: if (selNode < t->nodeCount - 1) selNode++; break;
        case '\n': case KEY_ENTER:
            if (t->nodeCount > 0) { selOpt = 0; screen = SCR_NODE; }
            break;
        case 'a': case 'A':
            if (t->nodeCount < DIALOG_MAX_NODES) {
                memset(&t->nodes[t->nodeCount], 0, sizeof(DialogNode));
                strncpy(t->nodes[t->nodeCount].line, "...", 73);
                selNode = t->nodeCount++;
                dirty = 1;
            }
            break;
        case 'd': case 'D':
            if (t->nodeCount > 0) {
                for (int i = selNode; i < t->nodeCount - 1; i++)
                    t->nodes[i] = t->nodes[i + 1];
                t->nodeCount--;
                if (selNode >= t->nodeCount && selNode > 0) selNode--;
                dirty = 1;
            }
            break;
        case 'e': case 'E':
            if (t->nodeCount > 0) {
                int row = selNode + 3;
                mvprintw(row, 2, "[%2d] opts:%d  \"", selNode, t->nodes[selNode].optionCount);
                int col = 2 + 13;
                editStr(row, col, t->nodes[selNode].line, 74);
                dirty = 1;
            }
            break;
        case KEY_BACKSPACE: case 127: case 'q': case 'Q':
            screen = SCR_TREES;
            break;
        case 's': case 'S': save(); break;
    }
}

/* ------ SCR_NODE ------ */
/* Shows NPC line at top + list of options; Enter on option opens SCR_OPTION */
static void drawNode(void) {
    clear();
    DialogTree *t = &trees[selTree];
    DialogNode *n = &t->nodes[selNode];

    mvprintw(0, 0, "NODE %d  tree:%s  [%s]  opts:%d/%d",
        selNode, t->speakerName, dirty ? "unsaved" : "saved",
        n->optionCount, DIALOG_MAX_OPTIONS);
    mvprintw(1, 0, "Enter=edit opt  A=add opt  D=del opt  E=edit NPC line  Bksp/Q=back");

    /* NPC line row */
    int npcRow = 3;
    if (selOpt == -1) attron(A_REVERSE);
    mvprintw(npcRow, 0, "NPC: \"%s\"", n->line);
    if (selOpt == -1) attroff(A_REVERSE);

    /* Options */
    for (int i = 0; i < n->optionCount; i++) {
        int row = npcRow + 2 + i;
        if (i == selOpt) attron(A_REVERSE);
        DialogOption *o = &n->options[i];
        char domBuf[24];
        if (o->requiredDomain == NO_DOMAIN)
            snprintf(domBuf, sizeof(domBuf), "none");
        else
            snprintf(domBuf, sizeof(domBuf), "%s>=%d",
                o->requiredDomain < DOMAIN_COUNT ? domainNames[o->requiredDomain] : "???",
                o->requiredLevel);
        char fxBuf[24];
        if (o->effect_type == DLGFX_NONE)
            snprintf(fxBuf, sizeof(fxBuf), "-");
        else
            snprintf(fxBuf, sizeof(fxBuf), "%s(%d)",
                o->effect_type < DLGFX_COUNT ? effectNames[o->effect_type] : "???",
                o->effect_arg);
        mvprintw(row, 2, "[%d] next:%-3d  req:%-14s  fx:%-16s  \"%s\"",
            i, (int)o->nextNode, domBuf, fxBuf, o->text);
        if (i == selOpt) attroff(A_REVERSE);
    }
    if (n->optionCount == 0)
        mvprintw(npcRow + 2, 2, "(no options)");
}

static void handleNode(int ch) {
    DialogTree *t = &trees[selTree];
    DialogNode *n = &t->nodes[selNode];

    switch (ch) {
        case KEY_UP:
            selOpt--;
            if (selOpt < 0) selOpt = n->optionCount - 1;
            break;
        case KEY_DOWN:
            selOpt = (n->optionCount > 0) ? (selOpt + 1) % n->optionCount : 0;
            break;
        case '\n': case KEY_ENTER:
            if (selOpt >= 0 && selOpt < n->optionCount)
                screen = SCR_OPTION;
            break;
        case 'e': case 'E': {
            int row = 3;
            mvprintw(row, 5, "\"");
            editStr(row, 6, n->line, 74);
            dirty = 1;
            break;
        }
        case 'a': case 'A':
            if (n->optionCount < DIALOG_MAX_OPTIONS) {
                memset(&n->options[n->optionCount], 0, sizeof(DialogOption));
                n->options[n->optionCount].requiredDomain = NO_DOMAIN;
                n->options[n->optionCount].nextNode = -1;
                strncpy(n->options[n->optionCount].text, "...", 40);
                selOpt = n->optionCount++;
                dirty = 1;
            }
            break;
        case 'd': case 'D':
            if (n->optionCount > 0 && selOpt >= 0) {
                for (int i = selOpt; i < n->optionCount - 1; i++)
                    n->options[i] = n->options[i + 1];
                n->optionCount--;
                if (selOpt >= n->optionCount && selOpt > 0) selOpt--;
                dirty = 1;
            }
            break;
        case KEY_BACKSPACE: case 127: case 'q': case 'Q':
            screen = SCR_NODES;
            break;
        case 's': case 'S': save(); break;
    }
}

/* ------ SCR_OPTION ------ */

/* Fields: 0=text, 1=domain, 2=level, 3=nextNode, 4=effect_type, 5=effect_arg */
static int selField = 0;
#define OPT_FIELD_COUNT 6

static void drawOption(void) {
    clear();
    DialogTree  *t = &trees[selTree];
    DialogNode  *n = &t->nodes[selNode];
    DialogOption *o = &n->options[selOpt];

    mvprintw(0, 0, "OPTION %d  node:%d  tree:%s  [%s]",
        selOpt, selNode, t->speakerName, dirty ? "unsaved" : "saved");
    mvprintw(1, 0, "Up/Down=field  E=edit text  +/-=change  Bksp/Q=back  S=save");

    const char *labels[] = {
        "Player text", "Req. domain", "Req. level", "Next node",
        "Effect", "Effect arg"
    };
    for (int i = 0; i < OPT_FIELD_COUNT; i++) {
        if (i == selField) attron(A_REVERSE);
        switch (i) {
            case 0: mvprintw(3 + i, 2, "%-14s: %s",  labels[i], o->text); break;
            case 1: mvprintw(3 + i, 2, "%-14s: %s",  labels[i], domainLabel(o->requiredDomain)); break;
            case 2: mvprintw(3 + i, 2, "%-14s: %d",  labels[i], o->requiredLevel); break;
            case 3: mvprintw(3 + i, 2, "%-14s: %d",  labels[i], (int)o->nextNode); break;
            case 4: mvprintw(3 + i, 2, "%-14s: %s",  labels[i],
                        o->effect_type < DLGFX_COUNT ? effectNames[o->effect_type] : "???"); break;
            case 5: mvprintw(3 + i, 2, "%-14s: %d",  labels[i], o->effect_arg); break;
        }
        if (i == selField) attroff(A_REVERSE);
    }
    mvprintw(10, 0, "nextNode: node index in this tree, -1 = close dialog");
    mvprintw(11, 0, "effect applied after navigation; overrides state if it starts an encounter");
}

static void handleOption(int ch) {
    DialogOption *o = &trees[selTree].nodes[selNode].options[selOpt];

    switch (ch) {
        case KEY_UP:   if (selField > 0) selField--; break;
        case KEY_DOWN: if (selField < OPT_FIELD_COUNT - 1) selField++; break;
        case 'e': case 'E':
            if (selField == 0) {
                editStr(3, 18, o->text, 41);
                dirty = 1;
            }
            break;
        case '+': case '=':
            dirty = 1;
            switch (selField) {
                case 1: o->requiredDomain = domainNext(o->requiredDomain); break;
                case 2: if (o->requiredLevel < 20) o->requiredLevel++; break;
                case 3: if (o->nextNode < DIALOG_MAX_NODES - 1) o->nextNode++; break;
                case 4: o->effect_type = (o->effect_type + 1) % DLGFX_COUNT; break;
                case 5: if (o->effect_arg < 255) o->effect_arg++; break;
                default: dirty = 0; break;
            }
            break;
        case '-':
            dirty = 1;
            switch (selField) {
                case 1: o->requiredDomain = domainPrev(o->requiredDomain); break;
                case 2: if (o->requiredLevel > 0) o->requiredLevel--; break;
                case 3: if (o->nextNode > -1) o->nextNode--; break;
                case 4: o->effect_type = (o->effect_type + DLGFX_COUNT - 1) % DLGFX_COUNT; break;
                case 5: if (o->effect_arg > 0) o->effect_arg--; break;
                default: dirty = 0; break;
            }
            break;
        case KEY_BACKSPACE: case 127: case 'q': case 'Q':
            screen = SCR_NODE;
            selField = 0;
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
            case SCR_TREES:  drawTrees();  break;
            case SCR_NODES:  drawNodes();  break;
            case SCR_NODE:   drawNode();   break;
            case SCR_OPTION: drawOption(); break;
        }
        refresh();

        int ch = getch();

        /* Q at top level = quit */
        if ((ch == 'q' || ch == 'Q') && screen == SCR_TREES) {
            running = 0;
            break;
        }

        switch (screen) {
            case SCR_TREES:  handleTrees(ch);  break;
            case SCR_NODES:  handleNodes(ch);  break;
            case SCR_NODE:   handleNode(ch);   break;
            case SCR_OPTION: handleOption(ch); break;
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
