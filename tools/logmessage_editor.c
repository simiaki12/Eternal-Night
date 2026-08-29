/*
 * logmessage_editor.c — ncurses editor for assets/data/log_messages.dat
 *
 * Navigation:
 *   SCR_LIST  Up/Down=select  N=new  D=delete  Enter=edit  S=save  Q=quit
 *   SCR_EDIT  Up/Down=field   +/-=cycle/increment  E=edit text  Bksp/Q=back
 */

#include <ncurses.h>
#include "refs.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MSG_MAX 128

/* ---- enum mirrors ---- */
#define LOGTRIG_COUNT   6
#define ENEMYCOND_COUNT 6
#define LOGFX_COUNT     8
#define LOGFXTGT_COUNT  5

static const char *trigNames[]    = { "Action","EnemyKilled","CombatStart","TurnEnd","PlayerHurt","PlayerLowHP" };
static const char *condNames[]    = { "None","Size>=","HP%<=","HP%>=","HasFlag","Count>=" };
static const char *fxNames[]     = { "None","AddMod","RemoveMod","EnemyFlee","EnemyStun","EnemyAtkDown","HealPlayer","DmgPlayer" };
static const char *fxtgtNames[]  = { "Focused","Weakest","Biggest","Random","All" };

typedef struct {
    char    text[28];
    uint8_t trigger;
    uint8_t actionId;
    uint8_t enemyDefId;
    uint8_t encounterType;
    uint8_t modRequired;
    uint8_t chance;
    uint8_t enemyCond;
    uint8_t enemyCondVal;
    uint8_t effectType;
    uint8_t effectTarget;
    uint8_t effectValue;
    uint8_t _pad;
} LogMessage; /* 40 bytes */

static const char *outfile = "assets/data/log_messages.dat";

#define SCROLL_MARGIN 3

static void scroll_to(int sel, int *scroll, int visible) {
    if (sel - *scroll < SCROLL_MARGIN)               *scroll = sel - SCROLL_MARGIN;
    if (sel - *scroll > visible - SCROLL_MARGIN - 1)  *scroll = sel - visible + SCROLL_MARGIN + 1;
    if (*scroll < 0) *scroll = 0;
}

static LogMessage entries[LOG_MSG_MAX];
static int        entryCount = 0;
static int        dirty      = 0;

/* ---- file I/O ---- */

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) { entryCount = 0; return; }
    uint8_t n = 0;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > LOG_MSG_MAX) n = LOG_MSG_MAX;
    size_t got = fread(entries, sizeof(LogMessage), n, f);
    entryCount = (int)got;
    fclose(f);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)entryCount;
    fwrite(&n, 1, 1, f);
    fwrite(entries, sizeof(LogMessage), (size_t)entryCount, f);
    fclose(f);
    dirty = 0;
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
        if (ch == 27) break; /* ESC */
        if ((ch == KEY_BACKSPACE || ch == 127) && pos > 0) {
            memmove(buf + pos - 1, buf + pos, (size_t)(len - pos + 1));
            len--; pos--;
        } else if (ch == KEY_LEFT  && pos > 0)   pos--;
        else if (ch == KEY_RIGHT && pos < len) pos++;
        else if (ch >= 32 && ch < 127 && len < maxLen - 1) {
            memmove(buf + pos + 1, buf + pos, (size_t)(len - pos + 1));
            buf[pos++] = (char)ch;
            len++;
        }
    }
    curs_set(0);
}

/* ---- screens ---- */

typedef enum { SCR_LIST, SCR_EDIT } Screen;

static Screen screen      = SCR_LIST;
static int    listSel     = 0;
static int    listScroll  = 0;
static int    editField   = 0;
#define EDIT_FIELDS 11

static const char *fieldLabel(int f) {
    switch (f) {
        case 0:  return "Text";
        case 1:  return "Trigger";
        case 2:  return "ActionId  (0xFF=any)";
        case 3:  return "EnemyDefId(0xFF=any)";
        case 4:  return "EnctrType (0xFF=any)";
        case 5:  return "ModRequired";
        case 6:  return "Chance(0-100)";
        case 7:  return "EnemyCond";
        case 8:  return "CondVal";
        case 9:  return "EffectType";
        case 10: return "EffectTarget";
        case 11: return "EffectValue";
        default: return "?";
    }
}

static void fieldStr(int f, const LogMessage *m, char *out, int sz) {
    switch (f) {
        case 0:  snprintf(out, sz, "%s", m->text); break;
        case 1:  snprintf(out, sz, "%s (%d)", m->trigger < LOGTRIG_COUNT ? trigNames[m->trigger] : "?", m->trigger); break;
        /* 0xFF is "matches anything" here, not an unset reference, so it gets
           its own word instead of refLabel's "(none)". */
        case 2:  m->actionId == 0xFF ? snprintf(out, sz, "any")
                                     : snprintf(out, sz, "%s", refLabel(REF_ACTION, m->actionId)); break;
        case 3:  m->enemyDefId == 0xFF ? snprintf(out, sz, "any")
                                       : snprintf(out, sz, "%s", refLabel(REF_ENEMY, m->enemyDefId)); break;
        case 4:  m->encounterType == 0xFF ? snprintf(out, sz, "any") : snprintf(out, sz, "%d", m->encounterType); break;
        case 5:  snprintf(out, sz, "0x%02X", m->modRequired); break;
        case 6:  snprintf(out, sz, "%d%%", m->chance); break;
        case 7:  snprintf(out, sz, "%s (%d)", m->enemyCond < ENEMYCOND_COUNT ? condNames[m->enemyCond] : "?", m->enemyCond); break;
        case 8:  snprintf(out, sz, "%d", m->enemyCondVal); break;
        case 9:  snprintf(out, sz, "%s (%d)", m->effectType < LOGFX_COUNT ? fxNames[m->effectType] : "?", m->effectType); break;
        case 10: snprintf(out, sz, "%s (%d)", m->effectTarget < LOGFXTGT_COUNT ? fxtgtNames[m->effectTarget] : "?", m->effectTarget); break;
        case 11: snprintf(out, sz, "%d", m->effectValue); break;
        default: out[0] = 0;
    }
}

static void fieldCycle(int f, LogMessage *m, int delta) {
    switch (f) {
        case 1: m->trigger      = (uint8_t)((m->trigger + LOGTRIG_COUNT + delta) % LOGTRIG_COUNT); break;
        case 2: m->actionId     = refCycle(REF_ACTION, m->actionId,   delta); break;
        case 3: m->enemyDefId   = refCycle(REF_ENEMY,  m->enemyDefId, delta); break;
        case 4: m->encounterType= (m->encounterType == 0xFF && delta > 0) ? 0 : (m->encounterType == 0 && delta < 0) ? 0xFF : (uint8_t)(m->encounterType + delta); break;
        case 5: m->modRequired  = (uint8_t)(m->modRequired + delta); break;
        case 6: { int v = (int)m->chance + delta; m->chance = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); break; }
        case 7: m->enemyCond    = (uint8_t)((m->enemyCond + ENEMYCOND_COUNT + delta) % ENEMYCOND_COUNT); break;
        case 8: m->enemyCondVal = (uint8_t)(m->enemyCondVal + delta); break;
        case 9: m->effectType   = (uint8_t)((m->effectType + LOGFX_COUNT + delta) % LOGFX_COUNT); break;
        case 10:m->effectTarget = (uint8_t)((m->effectTarget + LOGFXTGT_COUNT + delta) % LOGFXTGT_COUNT); break;
        case 11:m->effectValue  = (uint8_t)(m->effectValue + delta); break;
        default: break;
    }
}

static void drawList(void) {
    clear();
    int visible = LINES - 3;
    scroll_to(listSel, &listScroll, visible);
    mvprintw(0, 0, "LOG MESSAGE EDITOR  [%d/%d]%s",
        entryCount > 0 ? listSel + 1 : 0, entryCount, dirty ? " *" : "");
    mvprintw(1, 0, "Up/Down=select  Enter=edit  N=new  D=delete  S=save  Q=quit");
    mvhline(2, 0, '-', 78);
    for (int i = listScroll; i < entryCount && i < listScroll + visible; i++) {
        int row = (i - listScroll) + 3;
        if (i == listSel) attron(A_REVERSE);
        mvprintw(row, 0, "%3d  %-28s  %s", i,
                 entries[i].text,
                 entries[i].trigger < LOGTRIG_COUNT ? trigNames[entries[i].trigger] : "?");
        if (i == listSel) attroff(A_REVERSE);
    }
    refresh();
}

static void drawEdit(void) {
    if (listSel < 0 || listSel >= entryCount) return;
    LogMessage *m = &entries[listSel];
    clear();
    mvprintw(0, 0, "EDIT ENTRY %d  Up/Down=field  +/-=change  Enter=pick/edit  Q=back", listSel);
    mvhline(1, 0, '-', 78);
    char val[64];
    for (int f = 0; f <= EDIT_FIELDS; f++) {
        int row = 2 + f;
        if (f == editField) attron(A_REVERSE);
        fieldStr(f, m, val, sizeof(val));
        mvprintw(row, 0, "  %-22s : %s", fieldLabel(f), val);
        if (f == editField) attroff(A_REVERSE);
    }
    refresh();
}

int main(void) {
    load();
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    for (;;) {
        if (screen == SCR_LIST) {
            drawList();
            int ch = getch();
            if (ch == 'q' || ch == 'Q') {
                if (dirty) {
                    mvprintw(LINES - 1, 0, "Unsaved changes. Save? (y/n): ");
                    refresh();
                    int c2 = getch();
                    if (c2 == 'y' || c2 == 'Y') save();
                }
                break;
            }
            if (ch == KEY_UP   && listSel > 0)              listSel--;
            if (ch == KEY_DOWN && listSel < entryCount - 1) listSel++;
            if ((ch == '\n' || ch == KEY_ENTER) && entryCount > 0) {
                editField = 0;
                screen = SCR_EDIT;
            }
            if (ch == 'n' || ch == 'N') {
                if (entryCount < LOG_MSG_MAX) {
                    memset(&entries[entryCount], 0, sizeof(LogMessage));
                    entries[entryCount].actionId     = 0xFF;
                    entries[entryCount].enemyDefId   = 0xFF;
                    entries[entryCount].encounterType= 0xFF;
                    entries[entryCount].chance       = 100;
                    snprintf(entries[entryCount].text, 28, "New message %d", entryCount);
                    listSel = entryCount++;
                    editField = 0;
                    screen = SCR_EDIT;
                    dirty = 1;
                }
            }
            if ((ch == 'd' || ch == 'D') && entryCount > 0) {
                memmove(&entries[listSel], &entries[listSel + 1],
                        (size_t)(entryCount - listSel - 1) * sizeof(LogMessage));
                entryCount--;
                if (listSel >= entryCount && listSel > 0) listSel--;
                dirty = 1;
            }
            if (ch == 's' || ch == 'S') save();
        } else {
            drawEdit();
            int ch = getch();
            if (ch == 'q' || ch == 'Q' || ch == KEY_BACKSPACE || ch == 127) {
                screen = SCR_LIST;
            }
            if (ch == KEY_UP   && editField > 0)            editField--;
            if (ch == KEY_DOWN && editField < EDIT_FIELDS)  editField++;
            if ((ch == '+' || ch == '=') && editField > 0) { fieldCycle(editField, &entries[listSel],  1); dirty = 1; }
            if  (ch == '-'              && editField > 0)   { fieldCycle(editField, &entries[listSel], -1); dirty = 1; }
            if (ch == '\n' || ch == KEY_ENTER) {
                LogMessage *m = &entries[listSel];
                if      (editField == 2) { m->actionId   = refPick(REF_ACTION, m->actionId);   dirty = 1; }
                else if (editField == 3) { m->enemyDefId = refPick(REF_ENEMY,  m->enemyDefId); dirty = 1; }
                else if (editField == 0) {
                    drawEdit();
                    editStr(2, 26, m->text, 28);
                    dirty = 1;
                }
            }
            if ((ch == 'e' || ch == 'E') && editField == 0) {
                /* inline text edit */
                drawEdit();
                int row = 2 + 0;
                move(row, 26);
                editStr(row, 26, entries[listSel].text, 28);
                dirty = 1;
            }
            if (ch == 's' || ch == 'S') save();
        }
    }

    endwin();
    if (dirty) {
        printf("Unsaved changes discarded.\n");
    } else {
        printf("Saved to %s (%d entries)\n", outfile, entryCount);
    }
    return 0;
}
