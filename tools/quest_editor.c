/*
 * quest_editor.c — ncurses editor for quests.dat
 *
 * Binary format:
 *   [1 byte]        uint8_t quest count
 *   [N × 48 bytes]  QuestDef structs (direct memcpy, no pointers)
 *
 * Navigation:
 *   SCR_QUESTS   → SCR_QUEST     (Enter)
 *   SCR_QUEST    → SCR_OBJECTIVE (Enter on an objective row)
 *   Any screen: Bksp/Q = go up one level (Q at top = quit)
 */

#include <ncurses.h>
#include "refs.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- mirror of src/quests.h (kept in sync manually) ---- */
#define MAX_QUESTS    128
#define QUEST_MAX_OBJ   4

#define OBJ_KILL           0
#define OBJ_GET_ITEM       1
#define OBJ_VISIT_ZONE     2
#define OBJ_TALK_NPC       3
#define OBJ_VISIT_LOCATION 4
#define OBJ_TYPE_COUNT     5

#define TRIG_ALWAYS   0
#define TRIG_DIALOG   1
#define TRIG_ZONE     2
#define TRIG_ITEM     3
#define TRIG_KILL     4
#define TRIG_LOCATION 5
#define TRIG_COUNT    6

#define QUEST_MAIN   (1<<0)
#define QUEST_HIDDEN (1<<1)

typedef struct {
    uint8_t type;
    uint8_t targetId;
    uint8_t required;
    uint8_t _pad;
} QuestObjective;

typedef struct {
    char           name[24];
    uint8_t        objectiveCount;
    uint8_t        rewardXp;
    uint8_t        rewardItemId;
    uint8_t        flags;
    QuestObjective objectives[4];
    uint8_t        startType;
    uint8_t        startId;
    uint8_t        _pad[2];
} QuestDef;

#define QUEST_LOCATION_MAX 64

typedef struct {
    char    mapId[8];
    uint8_t x, y;
    uint8_t _pad[2];
} QuestLocation;

typedef char _cq[(sizeof(QuestDef)      == 48) ? 1 : -1];
typedef char _cl[(sizeof(QuestLocation) == 12) ? 1 : -1];
/* ------------------------------------------------------ */

/* Which file a target/start id points into, given the type beside it. The
   editor renders and edits those ids through refs.h using this. */
static RefKind objTargetKind(uint8_t type) {
    switch (type) {
        case OBJ_KILL:           return REF_ENEMY;
        case OBJ_GET_ITEM:       return REF_ITEM;
        case OBJ_TALK_NPC:       return REF_DIALOG;
        case OBJ_VISIT_LOCATION: return REF_QUEST_LOC;
        default:                 return REF_RAW;   /* VISIT_ZONE: raw map tile */
    }
}

static RefKind startIdKind(uint8_t trig) {
    switch (trig) {
        case TRIG_DIALOG:   return REF_DIALOG;
        case TRIG_ITEM:     return REF_ITEM;
        case TRIG_KILL:     return REF_ENEMY;
        case TRIG_LOCATION: return REF_QUEST_LOC;
        default:            return REF_RAW;   /* ALWAYS / ZONE: unused or tile */
    }
}

/* Changing the type reinterprets the id against a different file, so the old
   number is meaningless — clear it rather than leave a plausible-looking
   reference to the wrong record. */
static uint8_t defaultForKind(RefKind k) {
    return k == REF_RAW ? 0 : REF_NONE_VAL;
}

static const char *outfile = "assets/data/quests.dat";

#define SCROLL_MARGIN 3

static void scroll_to(int sel, int *scroll, int visible) {
    if (sel - *scroll < SCROLL_MARGIN)               *scroll = sel - SCROLL_MARGIN;
    if (sel - *scroll > visible - SCROLL_MARGIN - 1)  *scroll = sel - visible + SCROLL_MARGIN + 1;
    if (*scroll < 0) *scroll = 0;
}

static QuestDef quests[MAX_QUESTS];
static int      questCount = 0;
static int      dirty      = 0;

static const char *objTypeNames[OBJ_TYPE_COUNT] = {
    "Kill", "Get Item", "Visit Zone", "Talk NPC", "Visit Location"
};

static const char *trigNames[TRIG_COUNT] = {
    "Always", "Dialog", "Zone", "Item", "Kill", "Location"
};

/* ---- file I/O ---- */

/* quests.dat carries a second section of named locations after the quests.
 * This editor never edits them, but it must round-trip them: writing back only
 * the quest section would silently delete every OBJ_VISIT_LOCATION target. */
static QuestLocation locations[QUEST_LOCATION_MAX];
static int           locationCount = 0;

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) { questCount = 0; return; }

    uint8_t n = 0;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > MAX_QUESTS) n = MAX_QUESTS;

    size_t got = fread(quests, sizeof(QuestDef), n, f);
    questCount = (int)got;

    uint8_t nl = 0;
    if (fread(&nl, 1, 1, f) == 1) {
        if (nl > QUEST_LOCATION_MAX) nl = QUEST_LOCATION_MAX;
        locationCount = (int)fread(locations, sizeof(QuestLocation), nl, f);
    }
    fclose(f);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)questCount;
    fwrite(&n, 1, 1, f);
    fwrite(quests, sizeof(QuestDef), (size_t)questCount, f);
    uint8_t nl = (uint8_t)locationCount;
    fwrite(&nl, 1, 1, f);
    fwrite(locations, sizeof(QuestLocation), (size_t)locationCount, f);
    fclose(f);
    dirty = 0;
    refInvalidate();   /* this editor reads locations back out of the file it just wrote */
}

/* ---- text editing ---- */

static void editStr(int row, int col, char *buf, int maxLen) {
    int len = (int)strlen(buf);
    int pos = len;
    curs_set(1);
    for (;;) {
        mvhline(row, col, ' ', maxLen + 1);
        mvprintw(row, col, "%s", buf);
        move(row, col + pos);
        refresh();

        int ch = getch();
        if (ch == '\n' || ch == KEY_ENTER) break;
        if (ch == 27) break;
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

/* ---- screens ---- */

typedef enum { SCR_QUESTS, SCR_QUEST, SCR_OBJECTIVE } Screen;

static Screen screen  = SCR_QUESTS;
static int    selQ    = 0;   /* selected quest index */
static int    scrollQ = 0;   /* scroll offset for quest list */
static int    selField = 0;  /* field in SCR_QUEST: 0-5 = quest fields, 6+ = objectives */
static int    selObj  = 0;   /* selected objective in SCR_OBJECTIVE */
static int    selObjField = 0; /* field in SCR_OBJECTIVE */

/* ------ SCR_QUESTS ------ */

static void drawQuests(void) {
    clear();
    int visible = LINES - 3;
    scroll_to(selQ, &scrollQ, visible);
    mvprintw(0, 0, "QUEST EDITOR  [%s]  [%d/%d]",
        dirty ? "unsaved" : "saved", questCount > 0 ? selQ + 1 : 0, questCount);
    mvprintw(1, 0, "Enter=open  A=add  D=delete  E=edit name  S=save  Q=quit");

    for (int i = scrollQ; i < questCount && i < scrollQ + visible; i++) {
        QuestDef *q = &quests[i];
        if (i == selQ) attron(A_REVERSE);

        char flagBuf[16] = "";
        if (q->flags & QUEST_MAIN)   strcat(flagBuf, "MAIN ");
        if (q->flags & QUEST_HIDDEN) strcat(flagBuf, "HIDE ");

        char rewardBuf[32];
        if (q->rewardItemId == REF_NONE_VAL)
            snprintf(rewardBuf, sizeof(rewardBuf), "%dxp", q->rewardXp);
        else
            snprintf(rewardBuf, sizeof(rewardBuf), "%dxp+%s",
                     q->rewardXp, refLabel(REF_ITEM, q->rewardItemId));

        mvprintw((i - scrollQ) + 3, 2, "#%-3d  %-23s  %-10s  %-8s  %s  obj:%d",
            i, q->name, flagBuf, trigNames[q->startType < TRIG_COUNT ? q->startType : 0],
            rewardBuf, q->objectiveCount);

        if (i == selQ) attroff(A_REVERSE);
    }
    if (questCount == 0)
        mvprintw(3, 2, "(no quests — press A to add one)");
}

static void handleQuests(int ch) {
    switch (ch) {
        case KEY_UP:   if (selQ > 0) selQ--; break;
        case KEY_DOWN: if (selQ < questCount - 1) selQ++; break;
        case '\n': case KEY_ENTER:
            if (questCount > 0) { selField = 0; screen = SCR_QUEST; }
            break;
        case 'a': case 'A':
            if (questCount < MAX_QUESTS) {
                QuestDef *q = &quests[questCount];
                memset(q, 0, sizeof(*q));
                strncpy(q->name, "New Quest", 23);
                q->rewardItemId = 0xFF;
                q->startType = TRIG_ALWAYS;
                selQ = questCount++;
                dirty = 1;
                scroll_to(selQ, &scrollQ, LINES - 3);
                editStr((selQ - scrollQ) + 3, 7, quests[selQ].name, 24);
            }
            break;
        case 'd': case 'D':
            if (questCount > 0) {
                for (int i = selQ; i < questCount - 1; i++)
                    quests[i] = quests[i + 1];
                questCount--;
                if (selQ >= questCount && selQ > 0) selQ--;
                dirty = 1;
            }
            break;
        case 'e': case 'E':
            if (questCount > 0) {
                editStr((selQ - scrollQ) + 3, 7, quests[selQ].name, 24);
                dirty = 1;
            }
            break;
        case 's': case 'S': save(); break;
        case 'q': case 'Q': break;
    }
}

/* ------ SCR_QUEST ------ */
/* Fields 0-5: quest-level. Fields 6..6+objCount-1: objectives. */

#define QUEST_FIELD_COUNT 6

static void drawQuest(void) {
    clear();
    QuestDef *q = &quests[selQ];
    int totalFields = QUEST_FIELD_COUNT + q->objectiveCount;

    mvprintw(0, 0, "QUEST #%d: %s  [%s]", selQ, q->name, dirty ? "unsaved" : "saved");
    mvprintw(1, 0, "Up/Down=field  +/-=change  Enter=pick/edit  A=add obj  D=del obj  Bksp/Q=back  S=save");

    const char *labels[QUEST_FIELD_COUNT] = {
        "Name", "Flags", "Reward XP", "Reward Item", "Start type", "Start ID"
    };

    for (int i = 0; i < QUEST_FIELD_COUNT; i++) {
        int row = 3 + i;
        if (i == selField) attron(A_REVERSE);
        switch (i) {
            case 0:
                mvprintw(row, 2, "%-12s: %s", labels[i], q->name);
                break;
            case 1: {
                char fb[32] = "none";
                if (q->flags) {
                    fb[0] = '\0';
                    if (q->flags & QUEST_MAIN)   strcat(fb, "MAIN ");
                    if (q->flags & QUEST_HIDDEN) strcat(fb, "HIDDEN");
                }
                mvprintw(row, 2, "%-12s: %s  (+/- to toggle MAIN/HIDDEN)", labels[i], fb);
                break;
            }
            case 2:
                mvprintw(row, 2, "%-12s: %d", labels[i], q->rewardXp);
                break;
            case 3:
                if (q->rewardItemId == 0xFF)
                    mvprintw(row, 2, "%-12s: none (0xFF)", labels[i]);
                else
                    mvprintw(row, 2, "%-12s: %s", labels[i], refLabel(REF_ITEM, q->rewardItemId));
                break;
            case 4:
                mvprintw(row, 2, "%-12s: %s (%d)",
                    labels[i],
                    trigNames[q->startType < TRIG_COUNT ? q->startType : 0],
                    q->startType);
                break;
            case 5: {
                RefKind k = startIdKind(q->startType);
                if (q->startType == TRIG_ALWAYS)
                    mvprintw(row, 2, "%-12s: (unused)", labels[i]);
                else
                    mvprintw(row, 2, "%-12s: %s", labels[i], refLabelOr(k, q->startId));
                break;
            }
        }
        if (i == selField) attroff(A_REVERSE);
    }

    /* Objectives section */
    int divRow = 3 + QUEST_FIELD_COUNT + 1;
    mvprintw(divRow, 2, "Objectives: (%d/%d)  A=add  D=del selected  Enter=edit",
        q->objectiveCount, QUEST_MAX_OBJ);

    for (int j = 0; j < q->objectiveCount; j++) {
        int fIdx = QUEST_FIELD_COUNT + j;
        int row  = divRow + 1 + j;
        if (fIdx == selField) attron(A_REVERSE);
        QuestObjective *o = &q->objectives[j];
        mvprintw(row, 4, "[%d] %-14s  %-28s  required:%-3d",
            j,
            objTypeNames[o->type < OBJ_TYPE_COUNT ? o->type : 0],
            refLabelOr(objTargetKind(o->type), o->targetId),
            o->required);
        if (fIdx == selField) attroff(A_REVERSE);
    }
    if (q->objectiveCount == 0)
        mvprintw(divRow + 1, 4, "(no objectives)");

    (void)totalFields;
}

static void handleQuest(int ch) {
    QuestDef *q = &quests[selQ];
    int totalFields = QUEST_FIELD_COUNT + q->objectiveCount;

    switch (ch) {
        case KEY_UP:
            if (selField > 0) selField--;
            break;
        case KEY_DOWN:
            if (selField < totalFields - 1) selField++;
            break;
        case '\n': case KEY_ENTER:
            if (selField >= QUEST_FIELD_COUNT && selField < totalFields) {
                selObj = selField - QUEST_FIELD_COUNT;
                selObjField = 0;
                screen = SCR_OBJECTIVE;
            } else if (selField == 0) {
                editStr(3, 16, q->name, 24);
                dirty = 1;
            } else if (selField == 3) {
                q->rewardItemId = refPick(REF_ITEM, q->rewardItemId);
                dirty = 1;
            } else if (selField == 5 && q->startType != TRIG_ALWAYS) {
                q->startId = refPickOr(startIdKind(q->startType), q->startId);
                dirty = 1;
            }
            break;
        case 'e': case 'E':
            if (selField == 0) {
                editStr(3, 16, q->name, 24);
                dirty = 1;
            }
            break;
        case '+': case '=':
            dirty = 1;
            switch (selField) {
                case 1:
                    /* Cycle flags: none → MAIN → HIDDEN → MAIN|HIDDEN → none */
                    q->flags = (q->flags + 1) & (QUEST_MAIN | QUEST_HIDDEN);
                    break;
                case 2: if (q->rewardXp < 255) q->rewardXp++; break;
                case 3:
                    q->rewardItemId = refCycle(REF_ITEM, q->rewardItemId, 1);
                    break;
                case 4:
                    q->startType = (uint8_t)((q->startType + 1) % TRIG_COUNT);
                    q->startId   = defaultForKind(startIdKind(q->startType));
                    break;
                case 5:
                    q->startId = refCycleOr(startIdKind(q->startType), q->startId, 1);
                    break;
            }
            break;
        case '-':
            dirty = 1;
            switch (selField) {
                case 1:
                    q->flags = (q->flags == 0)
                        ? (QUEST_MAIN | QUEST_HIDDEN)
                        : (q->flags - 1) & (QUEST_MAIN | QUEST_HIDDEN);
                    break;
                case 2: if (q->rewardXp > 0) q->rewardXp--; break;
                case 3:
                    q->rewardItemId = refCycle(REF_ITEM, q->rewardItemId, -1);
                    break;
                case 4:
                    q->startType = (uint8_t)((q->startType + TRIG_COUNT - 1) % TRIG_COUNT);
                    q->startId   = defaultForKind(startIdKind(q->startType));
                    break;
                case 5:
                    q->startId = refCycleOr(startIdKind(q->startType), q->startId, -1);
                    break;
            }
            break;
        case 'a': case 'A':
            if (q->objectiveCount < QUEST_MAX_OBJ) {
                QuestObjective *o = &q->objectives[q->objectiveCount];
                memset(o, 0, sizeof(*o));
                o->required  = 1;
                o->targetId  = defaultForKind(objTargetKind(o->type));
                selField = QUEST_FIELD_COUNT + q->objectiveCount;
                q->objectiveCount++;
                dirty = 1;
            }
            break;
        case 'd': case 'D':
            if (selField >= QUEST_FIELD_COUNT && q->objectiveCount > 0) {
                int j = selField - QUEST_FIELD_COUNT;
                for (int k = j; k < q->objectiveCount - 1; k++)
                    q->objectives[k] = q->objectives[k + 1];
                q->objectiveCount--;
                if (selField >= QUEST_FIELD_COUNT + q->objectiveCount && selField > QUEST_FIELD_COUNT)
                    selField--;
                dirty = 1;
            }
            break;
        case KEY_BACKSPACE: case 127: case 'q': case 'Q':
            screen = SCR_QUESTS;
            selField = 0;
            break;
        case 's': case 'S': save(); break;
    }
}

/* ------ SCR_OBJECTIVE ------ */
/* Fields: 0=type, 1=targetId, 2=required */

static void drawObjective(void) {
    clear();
    QuestDef      *q = &quests[selQ];
    QuestObjective *o = &q->objectives[selObj];

    mvprintw(0, 0, "OBJECTIVE #%d  quest: %s  [%s]",
        selObj, q->name, dirty ? "unsaved" : "saved");
    mvprintw(1, 0, "Up/Down=field  +/-=change  Enter=pick from list  Bksp/Q=back  S=save");

    const char *labels[] = { "Type", "Target", "Required" };
    RefKind k = objTargetKind(o->type);

    for (int i = 0; i < 3; i++) {
        if (i == selObjField) attron(A_REVERSE);
        switch (i) {
            case 0:
                mvprintw(3 + i, 2, "%-10s: %s (%d)",
                    labels[i],
                    objTypeNames[o->type < OBJ_TYPE_COUNT ? o->type : 0],
                    o->type);
                break;
            case 1:
                mvprintw(3 + i, 2, "%-10s: %s", labels[i], refLabelOr(k, o->targetId));
                break;
            case 2:
                mvprintw(3 + i, 2, "%-10s: %d", labels[i], o->required);
                break;
        }
        if (i == selObjField) attroff(A_REVERSE);
    }

    /* What the target means for the type currently selected */
    static const char *targetHelp[OBJ_TYPE_COUNT] = {
        "an enemy from enemies.dat",
        "an item from items.dat",
        "a raw map loc tile value (0x01-0x0F=enemy pool, 0xFE=town)",
        "a dialog tree from dialog.dat",
        "a named location from the quests.dat location section",
    };
    mvprintw(7, 0, "Target is %s.", targetHelp[o->type < OBJ_TYPE_COUNT ? o->type : 0]);
    if (k == REF_RAW)
        mvprintw(8, 0, "No list to browse for this type - use +/- to set the number.");
    else
        mvprintw(8, 0, "Changing the type clears the target, since it would point at another file.");
}

static void handleObjective(int ch) {
    QuestObjective *o = &quests[selQ].objectives[selObj];

    switch (ch) {
        case KEY_UP:   if (selObjField > 0) selObjField--; break;
        case KEY_DOWN: if (selObjField < 2) selObjField++; break;
        case '\n': case KEY_ENTER:
            if (selObjField == 1) {
                o->targetId = refPickOr(objTargetKind(o->type), o->targetId);
                dirty = 1;
            }
            break;
        case '+': case '=':
            dirty = 1;
            switch (selObjField) {
                case 0:
                    o->type     = (uint8_t)((o->type + 1) % OBJ_TYPE_COUNT);
                    o->targetId = defaultForKind(objTargetKind(o->type));
                    break;
                case 1: o->targetId = refCycleOr(objTargetKind(o->type), o->targetId, 1); break;
                case 2: if (o->required < 255) o->required++; break;
            }
            break;
        case '-':
            dirty = 1;
            switch (selObjField) {
                case 0:
                    o->type     = (uint8_t)((o->type + OBJ_TYPE_COUNT - 1) % OBJ_TYPE_COUNT);
                    o->targetId = defaultForKind(objTargetKind(o->type));
                    break;
                case 1: o->targetId = refCycleOr(objTargetKind(o->type), o->targetId, -1); break;
                case 2: if (o->required > 1) o->required--; break;
            }
            break;
        case KEY_BACKSPACE: case 127: case 'q': case 'Q':
            screen = SCR_QUEST;
            selObjField = 0;
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
            case SCR_QUESTS:    drawQuests();    break;
            case SCR_QUEST:     drawQuest();     break;
            case SCR_OBJECTIVE: drawObjective(); break;
        }
        refresh();

        int ch = getch();

        if ((ch == 'q' || ch == 'Q') && screen == SCR_QUESTS) {
            running = 0;
            break;
        }

        switch (screen) {
            case SCR_QUESTS:    handleQuests(ch);    break;
            case SCR_QUEST:     handleQuest(ch);     break;
            case SCR_OBJECTIVE: handleObjective(ch); break;
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
