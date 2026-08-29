/*
 * item_editor.c — ncurses editor for assets/items.dat
 *
 * Navigation:
 *   SCR_LIST  Up/Down=select  N=new  D=delete  Enter=edit  S=save  Q=quit
 *   SCR_EDIT  Up/Down=field   +/-=change numeric  Enter=edit text  Backspace=back
 */

#include <ncurses.h>
#include "refs.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- mirror of src/items.h (keep in sync) ---- */
#define ITEM_WEAPON       0
#define ITEM_OUTFIT       1
#define ITEM_CONSUMABLE   2
#define ITEM_UNDERGARMENT 3
#define ITEM_ACCESSORY    4
#define ITEM_RELIC        5

#define ITEM_FLAG_HEAL        0x01
#define ITEM_FLAG_BUFF_ATTACK 0x02

typedef struct {
    char     name[16];
    uint8_t  type;
    int8_t   attackBonus;
    int8_t   defenseBonus;
    int8_t   intelligenceBonus;
    int8_t   perceptionBonus;
    int8_t   staminaBonus;
    int8_t   hpBonus;
    uint8_t  flags;
    uint16_t price;
    char     description[24];
    uint8_t  actions[4];  /* action IDs granted; 0xFF = none */
    uint8_t  _pad[10];
} ItemDef;

typedef char check_size[(sizeof(ItemDef) == 64) ? 1 : -1];
/* ------------------------------------------------ */

#define SCROLL_MARGIN 3

static void scroll_to(int sel, int *scroll, int visible) {
    if (sel - *scroll < SCROLL_MARGIN)               *scroll = sel - SCROLL_MARGIN;
    if (sel - *scroll > visible - SCROLL_MARGIN - 1)  *scroll = sel - visible + SCROLL_MARGIN + 1;
    if (*scroll < 0) *scroll = 0;
}

#define MAX_ITEMS 64
static ItemDef items[MAX_ITEMS];
static int     itemCount = 0;
static int     dirty     = 0;
static const char *outfile = "assets/data/items.dat";

static const char *typeNames[] = { "Weapon", "Outfit", "Consumable", "Undergarment", "Accessory", "Relic" };
#define TYPE_COUNT 6

/* ---- file I/O ---- */

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) { itemCount = 0; return; }
    uint8_t n;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > MAX_ITEMS) n = MAX_ITEMS;
    itemCount = (int)fread(items, sizeof(ItemDef), n, f);
    fclose(f);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)itemCount;
    fwrite(&n, 1, 1, f);
    fwrite(items, sizeof(ItemDef), itemCount, f);
    fclose(f);
    dirty = 0;
}

/* ---- inline text input — returns 1 on confirm, 0 on cancel ---- */

static int editString(int row, int col, char *buf, int maxLen) {
    int len = 0; while (len < maxLen && buf[len]) len++;
    echo();
    curs_set(1);
    while (1) {
        /* render */
        mvhline(row, col, ' ', maxLen + 1);
        mvprintw(row, col, "%.*s", len, buf);
        move(row, col + len);
        refresh();

        int ch = getch();
        if (ch == '\n' || ch == KEY_ENTER) break;
        if (ch == 27)  { noecho(); curs_set(0); return 0; }
        if ((ch == KEY_BACKSPACE || ch == 127) && len > 0) { buf[--len] = '\0'; continue; }
        if (ch >= 32 && ch < 127 && len < maxLen - 1) { buf[len++] = (char)ch; buf[len] = '\0'; }
    }
    noecho();
    curs_set(0);
    return 1;
}

/* ---- edit screen ---- */

typedef enum {
    F_NAME = 0, F_DESC, F_TYPE,
    F_ATK, F_DEF, F_MHP, F_INT, F_PER, F_STA,
    F_FLAG_HEAL, F_FLAG_BUFF,
    F_PRICE,
    F_ACT0, F_ACT1, F_ACT2, F_ACT3,
    F_COUNT
} Field;

static const char *fieldNames[] = {
    "Name", "Description", "Type",
    "ATK bonus", "DEF bonus", "HP bonus", "INT bonus", "PER bonus", "STA bonus",
    "Flag: Heal", "Flag: Buff ATK",
    "Price",
    "Action slot 0", "Action slot 1", "Action slot 2", "Action slot 3",
};

static void renderEdit(ItemDef *it, int sel, const char *status) {
    clear();
    mvprintw(0, 0, "ITEM EDITOR — %s", it->name[0] ? it->name : "(unnamed)");
    mvprintw(1, 0, "Up/Down=field  +/-=change  Enter=edit text  Bksp=back  S=save");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = 0; i < F_COUNT; i++) {
        if (i == sel) attron(A_REVERSE);
        switch (i) {
            case F_NAME:
                mvprintw(i + 4, 2, "%-16s  %s", fieldNames[i], it->name);
                break;
            case F_DESC:
                mvprintw(i + 4, 2, "%-16s  %s", fieldNames[i], it->description);
                break;
            case F_TYPE:
                mvprintw(i + 4, 2, "%-16s  %s", fieldNames[i], typeNames[it->type]);
                break;
            case F_ATK:  mvprintw(i+4,2,"%-16s  %+d", fieldNames[i], it->attackBonus);       break;
            case F_DEF:  mvprintw(i+4,2,"%-16s  %+d", fieldNames[i], it->defenseBonus);      break;
            case F_MHP:  mvprintw(i+4,2,"%-16s  %+d", fieldNames[i], it->hpBonus);           break;
            case F_INT:  mvprintw(i+4,2,"%-16s  %+d", fieldNames[i], it->intelligenceBonus); break;
            case F_PER:  mvprintw(i+4,2,"%-16s  %+d", fieldNames[i], it->perceptionBonus);   break;
            case F_STA:  mvprintw(i+4,2,"%-16s  %+d", fieldNames[i], it->staminaBonus);      break;
            case F_FLAG_HEAL:
                mvprintw(i+4,2,"%-16s  %s", fieldNames[i],
                    (it->flags & ITEM_FLAG_HEAL) ? "[X]" : "[ ]");
                break;
            case F_FLAG_BUFF:
                mvprintw(i+4,2,"%-16s  %s", fieldNames[i],
                    (it->flags & ITEM_FLAG_BUFF_ATTACK) ? "[X]" : "[ ]");
                break;
            case F_PRICE:
                mvprintw(i+4,2,"%-16s  %d", fieldNames[i], it->price);
                break;
            case F_ACT0: case F_ACT1: case F_ACT2: case F_ACT3: {
                int j = i - F_ACT0;
                if (it->actions[j] == 0xFF)
                    mvprintw(i+4,2,"%-16s  none", fieldNames[i]);
                else
                    mvprintw(i+4,2,"%-16s  %s", fieldNames[i], refLabel(REF_ACTION, it->actions[j]));
                break;
            }
        }
        if (i == sel) attroff(A_REVERSE);
    }
    refresh();
}

static void screenEdit(int idx) {
    ItemDef *it  = &items[idx];
    int      sel = 0;
    const char *status = NULL;

    while (1) {
        renderEdit(it, sel, status);
        status = NULL;
        int ch = getch();

        switch (ch) {
            case KEY_UP:    if (sel > 0) sel--; break;
            case KEY_DOWN:  if (sel < F_COUNT - 1) sel++; break;

            case KEY_BACKSPACE: case 127: return;

            case 's': case 'S': save(); status = "Saved."; break;

            case '\n': case KEY_ENTER:
                if (sel == F_NAME) {
                    if (editString(sel + 4, 20, it->name, 16)) dirty = 1;
                } else if (sel == F_DESC) {
                    if (editString(sel + 4, 20, it->description, 24)) dirty = 1;
                } else if (sel >= F_ACT0 && sel <= F_ACT3) {
                    int j = sel - F_ACT0;
                    it->actions[j] = refPick(REF_ACTION, it->actions[j]);
                    dirty = 1;
                }
                break;

            case '+': case '=':
                dirty = 1;
                switch (sel) {
                    case F_TYPE:  it->type = (it->type + 1) % TYPE_COUNT; break;
                    case F_ATK:   if (it->attackBonus       < 127) it->attackBonus++;       break;
                    case F_DEF:   if (it->defenseBonus      < 127) it->defenseBonus++;      break;
                    case F_MHP:   if (it->hpBonus           < 127) it->hpBonus++;           break;
                    case F_INT:   if (it->intelligenceBonus < 127) it->intelligenceBonus++; break;
                    case F_PER:   if (it->perceptionBonus   < 127) it->perceptionBonus++;   break;
                    case F_STA:   if (it->staminaBonus      < 127) it->staminaBonus++;      break;
                    case F_FLAG_HEAL:  it->flags ^= ITEM_FLAG_HEAL;        break;
                    case F_FLAG_BUFF:  it->flags ^= ITEM_FLAG_BUFF_ATTACK; break;
                    case F_PRICE: if (it->price < 65535) it->price++;      break;
                    case F_ACT0: case F_ACT1: case F_ACT2: case F_ACT3: {
                        int j = sel - F_ACT0;
                        it->actions[j] = refCycle(REF_ACTION, it->actions[j], 1);
                        break;
                    }
                    default: dirty = 0; break;
                }
                break;

            case '-':
                dirty = 1;
                switch (sel) {
                    case F_TYPE:  it->type = (it->type + TYPE_COUNT - 1) % TYPE_COUNT; break;
                    case F_ATK:   if (it->attackBonus       > -128) it->attackBonus--;       break;
                    case F_DEF:   if (it->defenseBonus      > -128) it->defenseBonus--;      break;
                    case F_MHP:   if (it->hpBonus           > -128) it->hpBonus--;           break;
                    case F_INT:   if (it->intelligenceBonus > -128) it->intelligenceBonus--; break;
                    case F_PER:   if (it->perceptionBonus   > -128) it->perceptionBonus--;   break;
                    case F_STA:   if (it->staminaBonus      > -128) it->staminaBonus--;      break;
                    case F_FLAG_HEAL:  it->flags ^= ITEM_FLAG_HEAL;        break;
                    case F_FLAG_BUFF:  it->flags ^= ITEM_FLAG_BUFF_ATTACK; break;
                    case F_PRICE: if (it->price > 0) it->price--;          break;
                    case F_ACT0: case F_ACT1: case F_ACT2: case F_ACT3: {
                        int j = sel - F_ACT0;
                        it->actions[j] = refCycle(REF_ACTION, it->actions[j], -1);
                        break;
                    }
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
    mvprintw(0, 0, "ITEM LIST  [%s]  [%d/%d]", dirty ? "unsaved" : "saved",
        itemCount > 0 ? sel + 1 : 0, itemCount);
    mvprintw(1, 0, "Up/Down=select  Enter=edit  N=new  D=delete  S=save  Q=quit");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = scroll; i < itemCount && i < scroll + visible; i++) {
        if (i == sel) attron(A_REVERSE);
        mvprintw((i - scroll) + 4, 2, "%2d  %-16s  %-10s  %+dATK %+dDEF  %dg",
            i,
            items[i].name[0] ? items[i].name : "(unnamed)",
            typeNames[items[i].type % TYPE_COUNT],
            items[i].attackBonus,
            items[i].defenseBonus,
            items[i].price);
        if (i == sel) attroff(A_REVERSE);
    }

    if (itemCount == 0)
        mvprintw(4, 2, "(no items — press N to add one)");

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
        if (sel >= itemCount && itemCount > 0) sel = itemCount - 1;
        if (sel < 0) sel = 0;
        scroll_to(sel, &scroll, LINES - 4);

        renderList(sel, scroll, status);
        status = NULL;
        int ch = getch();

        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--; break;
            case KEY_DOWN: if (sel < itemCount - 1) sel++; break;

            case '\n': case KEY_ENTER:
                if (itemCount > 0) screenEdit(sel);
                break;

            case 'n': case 'N':
                if (itemCount < MAX_ITEMS) {
                    memset(&items[itemCount], 0, sizeof(ItemDef));
                    memset(items[itemCount].actions, 0xFF, 4);
                    sel = itemCount++;
                    dirty = 1;
                    screenEdit(sel);
                } else {
                    status = "Max items reached.";
                }
                break;

            case 'd': case 'D':
                if (itemCount > 0) {
                    for (int i = sel; i < itemCount - 1; i++)
                        items[i] = items[i + 1];
                    itemCount--;
                    dirty = 1;
                    status = "Item deleted.";
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
