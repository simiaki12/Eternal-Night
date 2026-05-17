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

#define ACT_FLAG_STARTER (1<<0)

#define ACTION_MAX 64

typedef struct {
    uint8_t  id;
    uint8_t  contextFlags;
    uint8_t  baseWeight;
    uint8_t  power;
    char     name[16];
    char     imgName[8];
    char     desc[32];
    uint8_t  domain;
    uint8_t  encounterCat;
    uint8_t  actionFlags;
    uint8_t  _pad;
} ActionDef;

typedef char _check_size[(sizeof(ActionDef) == 64) ? 1 : -1];
/* ----------------------------------------------------------------------- */

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
    F_POWER,
    F_DOMAIN,
    F_ENCOUNTER_CAT,
    F_STARTER,
    F_CTX_FIRST_TURN,
    F_CTX_ENEMY_WEAPON,
    F_CTX_EXECUTABLE,
    F_CTX_CAN_STUN,
    F_CTX_PLAYER_HURT,
    F_CTX_REQUIRES_DARK,
    F_CTX_BLOCKED_HOLY,
    F_COUNT
} Field;

static const char *fieldNames[] = {
    "ID",
    "Name",
    "Description",
    "Image (sprite base)",
    "Base weight",
    "Power",
    "Domain (0=Combat 1=Trickery 2=Blood 3=Charm FF=none)",
    "Encounter cat (bit: 1=combat 2=social 4=invest 8=hunt 10=env)",
    "Starter (available without any domain unlock)",
    "Ctx: first turn only",
    "Ctx: enemy has weapon",
    "Ctx: enemy executable",
    "Ctx: enemy stunnable",
    "Ctx: player hurt (<50%)",
    "Ctx: requires darkness",
    "Ctx: blocked on holy ground",
};

static void renderEdit(ActionDef *a, int sel, const char *status) {
    clear();
    mvprintw(0, 0, "ACTION EDITOR — %s (id %d)", a->name[0] ? a->name : "(unnamed)", a->id);
    mvprintw(1, 0, "Up/Down=field  +/-=change  Enter=edit text  Bksp=back  S=save");
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
            case F_POWER:  mvprintw(row, 2, "%-42s  %d",  fieldNames[i], a->power);      break;
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
        }
        if (i == sel) attroff(A_REVERSE);
    }
    refresh();
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
                    case F_POWER:           if (a->power        < 255) a->power++;        break;
                    case F_DOMAIN:          if (a->domain       < 254) a->domain++;
                                            else a->domain = DOMAIN_NONE;                 break;
                    case F_ENCOUNTER_CAT:   if (a->encounterCat < 255) a->encounterCat++; break;
                    case F_STARTER:           a->actionFlags  ^= ACT_FLAG_STARTER;         break;
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
                    case F_POWER:           if (a->power      > 0) a->power--;        break;
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
                    default: dirty = 0; break;
                }
                break;
        }
    }
}

/* ---- list screen ---- */

static void renderList(int sel, const char *status) {
    clear();
    mvprintw(0, 0, "ACTION LIST  [%s]  (%d actions)", dirty ? "unsaved" : "saved", actionCount);
    mvprintw(1, 0, "Up/Down=select  Enter=edit  N=new  D=delete  S=save  Q=quit");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = 0; i < actionCount; i++) {
        if (i == sel) attron(A_REVERSE);
        char ctx[6] = "-----";
        if (actions[i].contextFlags & ACT_CTX_FIRST_TURN)   ctx[0] = 'F';
        if (actions[i].contextFlags & ACT_CTX_ENEMY_WEAPON) ctx[1] = 'W';
        if (actions[i].contextFlags & ACT_CTX_EXECUTABLE)   ctx[2] = 'X';
        if (actions[i].contextFlags & ACT_CTX_CAN_STUN)     ctx[3] = 'S';
        if (actions[i].contextFlags & ACT_CTX_PLAYER_HURT)  ctx[4] = 'H';
        mvprintw(i + 4, 2, "%2d  id:%-3d  %-16s  wt:%-3d  pow:%-3d  ctx:[%s]  img:%s",
            i,
            actions[i].id,
            actions[i].name[0] ? actions[i].name : "(unnamed)",
            actions[i].baseWeight,
            actions[i].power,
            ctx,
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
    int  running = 1;
    const char *status = NULL;

    while (running) {
        if (sel >= actionCount && actionCount > 0) sel = actionCount - 1;
        if (sel < 0) sel = 0;

        renderList(sel, status);
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
