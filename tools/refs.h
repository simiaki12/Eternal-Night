/*
 * refs.h — shared cross-reference resolution for the ncurses editors.
 *
 * Data files reference each other (an encounter names an NPC, a loot table
 * names an item, an action names a state). Editing those as raw numbers is
 * error-prone, so every editor renders them through here instead:
 *
 *     refLabel(REF_ENEMY, v)     -> "Bandit"      (display)
 *     refCycle(REF_ENEMY, v, +1) -> next id       (Left/Right arrows)
 *     refPick (REF_ENEMY, v)     -> chosen id     (Enter, list opens on v)
 *
 * Records are read straight from assets/data/*.dat on first use and cached,
 * so an editor always shows what the other editors last saved. The struct
 * layouts below mirror the game headers; each carries a static size assert so
 * a layout change breaks the build loudly rather than showing garbage.
 *
 * Editors compile standalone (one .c each), so this is header-only.
 */
#ifndef REFS_H
#define REFS_H

#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define REF_NONE_VAL 0xFF   /* the project-wide "unset" sentinel */
#define REF_MAX      64
#define REF_LABEL    28

typedef enum {
    REF_ITEM = 0,
    REF_ACTION,
    REF_ENEMY,
    REF_ENEMY_POOL,
    REF_LOOT_TABLE,
    REF_NPC,
    REF_QUEST,
    REF_CLUE,
    REF_CASE,
    REF_HUNT_ENC,
    REF_STATUS,
    REF_DIALOG,
    REF_QUEST_LOC,
    REF_STATE_COMBAT,   /* graph states — one kind per graph */
    REF_STATE_SOCIAL,
    REF_STATE_INVEST,
    REF_STATE_HUNT,
    REF_STATE_ENV,
    REF_KIND_COUNT
} RefKind;

typedef struct {
    uint8_t id;
    char    label[REF_LABEL];
} RefEntry;

typedef struct {
    RefEntry items[REF_MAX];
    int      count;
    int      loaded;
} RefList;

/* ---- how to find the id and the display text inside each record ---------
   All of these files are [1 byte count][N x record]. For records that carry
   no id of their own the index IS the id, which is what idOff = -1 means. */
typedef struct {
    const char *path;
    int         recSize;
    int         idOff;     /* -1 = use the array index */
    int         nameOff;
    int         nameLen;
} RefSpec;

/* Keep in sync with the game headers (sizes asserted in the editors that
   own each struct). */
static const RefSpec refSpecs[REF_KIND_COUNT] = {
    /* item          */ { "assets/data/items.dat",            64, -1, 0, 16 },
    /* action: variable-length records, filled by refLoadActions() */
    /* action        */ { NULL, 0, 0, 0, 0 },
    /* enemy         */ { "assets/data/enemies.dat",          56, 54, 0, 16 },
    /* enemy pool    */ { NULL, 0, 0, 0, 0 },   /* positional: "pool 1".. */
    /* loot table    */ { NULL, 0, 0, 0, 0 },   /* positional: "table 0".. */
    /* npc           */ { "assets/data/npcs.dat",             40, 37, 0, 16 },
    /* quest         */ { "assets/data/quests.dat",           48, -1, 0, 24 },
    /* clue          */ { "assets/data/clues.dat",            80,  0, 1, 28 },
    /* case          */ { "assets/data/cases.dat",            32,  0, 1, 24 },
    /* hunt enc      */ { "assets/data/hunt_encounters.dat",  44,  0, 1, 16 },
    /* status        */ { "assets/data/statuses.dat",         24, 0,  1, 12 },
    /* dialog: length-prefixed records, filled by refLoadDialogs() */
    /* dialog        */ { NULL, 0, 0, 0, 0 },
    /* quest loc: trailing section of quests.dat, filled by refLoadQuestLocs() */
    /* quest loc     */ { NULL, 0, 0, 0, 0 },
    /* states: filled by refLoadStates() from the 5-graph states.dat */
    /* combat        */ { NULL, 0, 0, 0, 0 },
    /* social        */ { NULL, 0, 0, 0, 0 },
    /* invest        */ { NULL, 0, 0, 0, 0 },
    /* hunt          */ { NULL, 0, 0, 0, 0 },
    /* env           */ { NULL, 0, 0, 0, 0 },
};

static RefList refCache[REF_KIND_COUNT];

static void refAdd(RefList *l, uint8_t id, const char *text, int len) {
    if (l->count >= REF_MAX) return;
    RefEntry *e = &l->items[l->count++];
    e->id = id;
    int n = 0;
    while (n < len && n < REF_LABEL - 1 && text[n]) { e->label[n] = text[n]; n++; }
    e->label[n] = '\0';
    if (!n) snprintf(e->label, REF_LABEL, "(%d)", id);
}

/* states.dat is [1 graphCount] then per graph [1 count][N x 24-byte StateDef] */
static void refLoadStates(void) {
    FILE *f = fopen("assets/data/states.dat", "rb");
    for (int g = 0; g < 5; g++) refCache[REF_STATE_COMBAT + g].loaded = 1;
    if (!f) return;
    uint8_t nGraphs = 0;
    if (fread(&nGraphs, 1, 1, f) != 1) { fclose(f); return; }
    for (int g = 0; g < nGraphs && g < 5; g++) {
        uint8_t n = 0;
        if (fread(&n, 1, 1, f) != 1) break;
        for (int i = 0; i < n; i++) {
            uint8_t rec[24];
            if (fread(rec, 1, 24, f) != 24) { fclose(f); return; }
            refAdd(&refCache[REF_STATE_COMBAT + g], (uint8_t)i, (char *)rec, 16);
        }
    }
    fclose(f);
}

/* actions.dat records are variable length: a 69-byte head followed by one
   36-byte matrix per bit set in graphMask (offset 62). */
static void refLoadActions(void) {
    RefList *l = &refCache[REF_ACTION];
    l->loaded = 1;
    FILE *f = fopen("assets/data/actions.dat", "rb");
    if (!f) return;
    uint8_t n = 0;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    for (int i = 0; i < n; i++) {
        uint8_t head[69];
        if (fread(head, 1, 69, f) != 69) break;
        refAdd(l, head[0], (char *)head + 3, 16);
        int skip = 0;
        for (int t = 0; t < 5; t++) if ((head[62] >> t) & 1) skip += 36;
        if (skip && fseek(f, skip, SEEK_CUR) != 0) break;
    }
    fclose(f);
}

/* dialog.dat is fully length-prefixed, so trees can only be counted by walking
   it: [1 count] then per tree [str speaker][1 nodes]( [str line][1 opts]( 5
   bytes + [str text] )* )*, where str is [1 len][len bytes]. The tree index is
   the id the rest of the data files store. */
static void refLoadDialogs(void) {
    RefList *l = &refCache[REF_DIALOG];
    l->loaded = 1;
    FILE *f = fopen("assets/data/dialog.dat", "rb");
    if (!f) return;
    uint8_t n = 0;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }

    for (int i = 0; i < n; i++) {
        uint8_t len = 0;
        char    speaker[REF_LABEL];
        if (fread(&len, 1, 1, f) != 1) break;
        int take = len < REF_LABEL - 1 ? len : REF_LABEL - 1;
        if (fread(speaker, 1, (size_t)take, f) != (size_t)take) break;
        speaker[take] = '\0';
        if (len > take && fseek(f, len - take, SEEK_CUR) != 0) break;
        refAdd(l, (uint8_t)i, speaker, REF_LABEL);

        uint8_t nodes = 0;
        if (fread(&nodes, 1, 1, f) != 1) break;
        int bad = 0;
        for (int j = 0; j < nodes && !bad; j++) {
            uint8_t lineLen = 0, opts = 0;
            if (fread(&lineLen, 1, 1, f) != 1) { bad = 1; break; }
            if (fseek(f, lineLen, SEEK_CUR) != 0) { bad = 1; break; }
            if (fread(&opts, 1, 1, f) != 1) { bad = 1; break; }
            for (int o = 0; o < opts; o++) {
                uint8_t textLen = 0;
                if (fseek(f, 5, SEEK_CUR) != 0)          { bad = 1; break; }
                if (fread(&textLen, 1, 1, f) != 1)       { bad = 1; break; }
                if (fseek(f, textLen, SEEK_CUR) != 0)    { bad = 1; break; }
            }
        }
        if (bad) break;
    }
    fclose(f);
}

/* Quest locations live in the optional second section of quests.dat, past the
   48-byte quest records: [1 locCount][N x 12-byte QuestLocation]. */
static void refLoadQuestLocs(void) {
    RefList *l = &refCache[REF_QUEST_LOC];
    l->loaded = 1;
    FILE *f = fopen("assets/data/quests.dat", "rb");
    if (!f) return;
    uint8_t nq = 0;
    if (fread(&nq, 1, 1, f) != 1)              { fclose(f); return; }
    if (fseek(f, nq * 48, SEEK_CUR) != 0)      { fclose(f); return; }
    uint8_t nl = 0;
    if (fread(&nl, 1, 1, f) != 1)              { fclose(f); return; }
    for (int i = 0; i < nl; i++) {
        uint8_t rec[12];
        if (fread(rec, 1, 12, f) != 12) break;
        char label[REF_LABEL];
        snprintf(label, sizeof(label), "%.8s (%d,%d)", (char *)rec, rec[8], rec[9]);
        refAdd(l, (uint8_t)i, label, REF_LABEL);
    }
    fclose(f);
}

static RefList *refGet(RefKind k) {
    RefList *l = &refCache[k];
    if (l->loaded) return l;

    if (k >= REF_STATE_COMBAT) { refLoadStates();    return l; }
    if (k == REF_ACTION)       { refLoadActions();   return l; }
    if (k == REF_DIALOG)       { refLoadDialogs();   return l; }
    if (k == REF_QUEST_LOC)    { refLoadQuestLocs(); return l; }
    l->loaded = 1;

    /* Positional kinds have no file of their own */
    if (k == REF_ENEMY_POOL) {
        for (int i = 0; i < 15; i++) {
            char b[REF_LABEL]; snprintf(b, sizeof(b), "pool %d", i + 1);
            refAdd(l, (uint8_t)(i + 1), b, REF_LABEL);
        }
        return l;
    }
    if (k == REF_LOOT_TABLE) {
        for (int i = 0; i < 16; i++) {
            char b[REF_LABEL]; snprintf(b, sizeof(b), "table %d", i);
            refAdd(l, (uint8_t)i, b, REF_LABEL);
        }
        return l;
    }

    const RefSpec *sp = &refSpecs[k];
    if (!sp->path) return l;
    FILE *f = fopen(sp->path, "rb");
    if (!f) return l;
    uint8_t n = 0;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return l; }
    uint8_t *rec = (uint8_t *)malloc(sp->recSize);
    for (int i = 0; i < n; i++) {
        if (fread(rec, 1, sp->recSize, f) != (size_t)sp->recSize) break;
        uint8_t id = sp->idOff < 0 ? (uint8_t)i : rec[sp->idOff];
        refAdd(l, id, (char *)rec + sp->nameOff, sp->nameLen);
    }
    free(rec);
    fclose(f);
    return l;
}

/* ---- public API --------------------------------------------------------- */

/* Display text for a stored value. Dangling references are shown loudly so
   broken data is visible instead of silently looking valid. */
static const char *refLabel(RefKind k, uint8_t id) {
    static char buf[REF_LABEL + 8];
    if (id == REF_NONE_VAL) return "(none)";
    RefList *l = refGet(k);
    for (int i = 0; i < l->count; i++)
        if (l->items[i].id == id) return l->items[i].label;
    snprintf(buf, sizeof(buf), "?? missing %d", id);
    return buf;
}

/* Step to the next/previous valid id. Wraps, and includes (none) as a stop
   so a reference can always be cleared without typing 255. */
static uint8_t refCycle(RefKind k, uint8_t cur, int dir) {
    RefList *l = refGet(k);
    if (l->count == 0) return cur;
    int at = -1;
    for (int i = 0; i < l->count; i++) if (l->items[i].id == cur) { at = i; break; }
    if (cur == REF_NONE_VAL) at = l->count;          /* (none) sits past the end */
    if (at < 0) at = 0;                              /* dangling: land on the first */
    int nx = at + (dir > 0 ? 1 : -1);
    if (nx < 0)            return REF_NONE_VAL;
    if (nx >= l->count)    return (nx == l->count) ? REF_NONE_VAL : l->items[0].id;
    return l->items[nx].id;
}

/* Scrollable picker centred on the current value. Enter selects, Esc keeps
   the old value, n clears it. Returns the chosen id. */
static uint8_t refPick(RefKind k, uint8_t cur) {
    RefList *l = refGet(k);
    if (l->count == 0) return cur;

    int sel = 0;
    for (int i = 0; i < l->count; i++) if (l->items[i].id == cur) { sel = i; break; }

    int rows = LINES - 8;
    if (rows < 5)          rows = 5;
    if (rows > l->count)   rows = l->count;
    int scroll = sel - rows / 2;
    if (scroll < 0)                 scroll = 0;
    if (scroll > l->count - rows)   scroll = l->count - rows;

    while (1) {
        clear();
        mvprintw(0, 0, "SELECT  (Up/Down move, Enter choose, N none, Esc cancel)");
        mvprintw(1, 0, "current: %s", refLabel(k, cur));
        for (int i = 0; i < rows; i++) {
            int idx = scroll + i;
            if (idx >= l->count) break;
            if (idx == sel) attron(A_REVERSE);
            mvprintw(i + 3, 2, "%-28s  id %d%s",
                     l->items[idx].label, l->items[idx].id,
                     l->items[idx].id == cur ? "  (current)" : "");
            if (idx == sel) attroff(A_REVERSE);
        }
        if (l->count > rows)
            mvprintw(rows + 4, 2, "-- %d of %d --", sel + 1, l->count);
        refresh();

        int ch = getch();
        if (ch == KEY_UP   && sel > 0)              sel--;
        if (ch == KEY_DOWN && sel < l->count - 1)   sel++;
        if (ch == KEY_PPAGE) { sel -= rows; if (sel < 0) sel = 0; }
        if (ch == KEY_NPAGE) { sel += rows; if (sel >= l->count) sel = l->count - 1; }
        if (ch == '\n' || ch == KEY_ENTER)          return l->items[sel].id;
        if (ch == 'n' || ch == 'N')                 return REF_NONE_VAL;
        if (ch == 27)                               return cur;

        if (sel < scroll)              scroll = sel;
        if (sel >= scroll + rows)      scroll = sel - rows + 1;
    }
}

/* ---- fields whose kind depends on a sibling field --------------------------
   A quest objective's targetId means an enemy, an item, a dialog tree or a raw
   map tile depending on its type. Those call sites pick the kind at runtime and
   need REF_RAW for the cases that really are a plain number, so the three
   wrappers below keep the branch out of every editor. */

#define REF_RAW ((RefKind)-1)

static const char *refLabelOr(RefKind k, uint8_t v) {
    static char buf[16];
    if (k == REF_RAW) { snprintf(buf, sizeof(buf), "%d", v); return buf; }
    return refLabel(k, v);
}

static uint8_t refCycleOr(RefKind k, uint8_t v, int dir) {
    if (k != REF_RAW) return refCycle(k, v, dir);
    if (dir > 0) return v < 255 ? (uint8_t)(v + 1) : v;
    return v > 0 ? (uint8_t)(v - 1) : v;
}

/* Returns v untouched for REF_RAW, so Enter is a no-op on a plain number
   rather than opening an empty list. */
static uint8_t refPickOr(RefKind k, uint8_t v) {
    return k == REF_RAW ? v : refPick(k, v);
}

/* Editors call this after saving a file other editors reference, so a picker
   opened later in the same session sees the new records. */
static void refInvalidate(void) {
    for (int i = 0; i < REF_KIND_COUNT; i++) {
        refCache[i].count  = 0;
        refCache[i].loaded = 0;
    }
}

#endif /* REFS_H */
