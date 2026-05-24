/*
 * shop_editor.c — ncurses editor for assets/data/shops.dat
 *
 * SCR_LIST  Up/Down=select  N=new  D=delete  Enter=edit  S=save  Q=quit
 * SCR_EDIT  Up/Down=field   Enter=edit name/pick item  D=remove item  Backspace=back
 */

#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- mirror of ShopDef (keep in sync with shop.h) ---- */
#define SHOP_DEF_MAX   32
#define SHOP_STOCK_MAX 16

typedef struct {
    char    name[24];
    uint8_t stock[SHOP_STOCK_MAX];
    uint8_t count;
    uint8_t _pad[7];
} ShopDef; /* 48 bytes */

typedef char check_shop[(sizeof(ShopDef) == 48) ? 1 : -1];

/* ---- mirror of ItemDef (keep in sync with items.h) ---- */
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
    uint8_t  actions[4];
    uint8_t  _pad[10];
} ItemDef; /* 64 bytes */

typedef char check_item[(sizeof(ItemDef) == 64) ? 1 : -1];
/* ------------------------------------------------------- */

#define SCROLL_MARGIN 3

static void scroll_to(int sel, int *scroll, int visible) {
    if (sel - *scroll < SCROLL_MARGIN)               *scroll = sel - SCROLL_MARGIN;
    if (sel - *scroll > visible - SCROLL_MARGIN - 1) *scroll = sel - visible + SCROLL_MARGIN + 1;
    if (*scroll < 0) *scroll = 0;
}

static ShopDef shops[SHOP_DEF_MAX];
static int     shopCount = 0;
static int     dirty     = 0;
static const char *shopfile = "assets/data/shops.dat";

static ItemDef items[64];
static int     itemCount = 0;
static const char *itemfile = "assets/data/items.dat";

/* --- file I/O --- */

static void loadShops(void) {
    FILE *f = fopen(shopfile, "rb");
    if (!f) { shopCount = 0; return; }
    uint8_t n;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > SHOP_DEF_MAX) n = SHOP_DEF_MAX;
    shopCount = (int)fread(shops, sizeof(ShopDef), n, f);
    fclose(f);
}

static void saveShops(void) {
    FILE *f = fopen(shopfile, "wb");
    if (!f) return;
    fwrite(&(uint8_t){(uint8_t)shopCount}, 1, 1, f);
    fwrite(shops, sizeof(ShopDef), shopCount, f);
    fclose(f);
    dirty = 0;
}

static void loadItems(void) {
    FILE *f = fopen(itemfile, "rb");
    if (!f) { itemCount = 0; return; }
    uint8_t n;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > 64) n = 64;
    itemCount = (int)fread(items, sizeof(ItemDef), n, f);
    fclose(f);
}

/* --- text editing --- */

static int editString(int row, int col, char *buf, int maxlen) {
    char tmp[128];
    strncpy(tmp, buf, maxlen); tmp[maxlen - 1] = '\0';
    int len = (int)strlen(tmp);
    curs_set(1);
    echo();
    for (;;) {
        mvprintw(row, col, "%-*s", maxlen - 1, tmp);
        move(row, col + len);
        int ch = getch();
        if (ch == '\n' || ch == KEY_ENTER) break;
        if (ch == 27) { curs_set(0); noecho(); return 0; }
        if ((ch == KEY_BACKSPACE || ch == 127) && len > 0) { tmp[--len] = '\0'; }
        else if (ch >= 32 && ch < 127 && len < maxlen - 1) { tmp[len++] = (char)ch; tmp[len] = '\0'; }
    }
    curs_set(0); noecho();
    strncpy(buf, tmp, maxlen);
    return 1;
}

/* --- item picker --- */

static int pickItem(void) {
    if (itemCount == 0) {
        clear();
        mvprintw(0, 0, "No items found in %s -- run make seed_items first.", itemfile);
        mvprintw(1, 0, "Press any key.");
        refresh();
        getch();
        return -1;
    }
    int sel = 0, scroll = 0;
    for (;;) {
        int visible = LINES - 4;
        scroll_to(sel, &scroll, visible);
        clear();
        mvprintw(0, 0, "PICK ITEM  Up/Down=select  Enter=confirm  ESC=cancel");
        for (int i = scroll; i < itemCount && i < scroll + visible; i++) {
            if (i == sel) attron(A_REVERSE);
            mvprintw((i - scroll) + 2, 2, "%2d  %-16s  %dg",
                i,
                items[i].name[0] ? items[i].name : "(unnamed)",
                items[i].price);
            if (i == sel) attroff(A_REVERSE);
        }
        refresh();
        int ch = getch();
        if (ch == KEY_UP   && sel > 0)          sel--;
        if (ch == KEY_DOWN && sel < itemCount-1) sel++;
        if (ch == '\n' || ch == KEY_ENTER)       return sel;
        if (ch == 27)                            return -1;
    }
}

/* --- edit screen --- */

/* Fields: 0 = name, 1..count = stock items, count+1 = (add slot) */

static void renderEdit(int shopIdx, int sel, int scroll, const char *status) {
    ShopDef *s = &shops[shopIdx];
    int totalFields = (int)s->count + 2;
    int visible = LINES - 5;
    clear();
    mvprintw(0, 0, "EDIT SHOP %d  [%s]", shopIdx, dirty ? "unsaved" : "saved");
    mvprintw(1, 0, "Up/Down=field  Enter=edit/pick  D=remove  Backspace=back  S=save");
    if (status) mvprintw(2, 0, "%s", status);

    for (int f = scroll; f < totalFields && f < scroll + visible; f++) {
        int row = (f - scroll) + 4;
        int hi  = (f == sel);
        if (hi) attron(A_REVERSE);
        if (f == 0) {
            mvprintw(row, 2, "Name:  %-23s", s->name[0] ? s->name : "(unnamed)");
        } else if (f <= (int)s->count) {
            int slot = f - 1;
            uint8_t id = s->stock[slot];
            const char *iname = id < (uint8_t)itemCount && items[id].name[0]
                                ? items[id].name : "???";
            mvprintw(row, 2, "Stock[%2d]: %-16s  (id %d)", slot, iname, id);
        } else {
            mvprintw(row, 2, "(add item...)");
        }
        if (hi) attroff(A_REVERSE);
    }
    refresh();
}

static void screenEdit(int shopIdx) {
    int sel = 0, scroll = 0;
    const char *status = NULL;
    for (;;) {
        ShopDef *s = &shops[shopIdx];
        int totalFields = (int)s->count + 2;
        int visible = LINES - 5;
        scroll_to(sel, &scroll, visible);
        renderEdit(shopIdx, sel, scroll, status);
        status = NULL;
        int ch = getch();
        switch (ch) {
            case KEY_UP:
                if (sel > 0) sel--;
                break;
            case KEY_DOWN:
                if (sel < totalFields - 1) sel++;
                break;
            case 's': case 'S':
                saveShops();
                status = "Saved.";
                break;
            case KEY_BACKSPACE: case 127: case 27:
                return;
            case 'd': case 'D':
                if (sel >= 1 && sel <= (int)s->count) {
                    int slot = sel - 1;
                    for (int i = slot; i < (int)s->count - 1; i++)
                        s->stock[i] = s->stock[i + 1];
                    s->count--;
                    if (sel > (int)s->count) sel = (int)s->count;
                    dirty = 1;
                } else {
                    status = "Nothing to remove here.";
                }
                break;
            case '\n': case KEY_ENTER:
                if (sel == 0) {
                    if (editString(4, 9, s->name, 24)) dirty = 1;
                } else if (sel <= (int)s->count) {
                    int picked = pickItem();
                    if (picked >= 0) { s->stock[sel - 1] = (uint8_t)picked; dirty = 1; }
                } else {
                    if (s->count >= SHOP_STOCK_MAX) {
                        status = "Stock full (16 items max).";
                    } else {
                        int picked = pickItem();
                        if (picked >= 0) {
                            s->stock[s->count++] = (uint8_t)picked;
                            sel = (int)s->count;
                            dirty = 1;
                        }
                    }
                }
                break;
        }
    }
}

/* --- list screen --- */

static void renderList(int sel, int scroll, const char *status) {
    int visible = LINES - 4;
    clear();
    mvprintw(0, 0, "SHOP LIST  [%s]  [%d/%d]",
        dirty ? "unsaved" : "saved",
        shopCount > 0 ? sel + 1 : 0, shopCount);
    mvprintw(1, 0, "Up/Down=select  Enter=edit  N=new  D=delete  S=save  Q=quit");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = scroll; i < shopCount && i < scroll + visible; i++) {
        if (i == sel) attron(A_REVERSE);
        mvprintw((i - scroll) + 4, 2, "%2d  %-23s  %d item(s)",
            i,
            shops[i].name[0] ? shops[i].name : "(unnamed)",
            shops[i].count);
        if (i == sel) attroff(A_REVERSE);
    }
    if (shopCount == 0)
        mvprintw(4, 2, "(no shops -- press N to add one)");
    refresh();
}

/* --- main --- */

int main(void) {
    loadShops();
    loadItems();

    initscr();
    keypad(stdscr, TRUE);
    noecho();
    cbreak();
    curs_set(0);

    int sel = 0, scroll = 0;
    const char *status = NULL;
    for (;;) {
        scroll_to(sel, &scroll, LINES - 4);
        renderList(sel, scroll, status);
        status = NULL;
        int ch = getch();
        switch (ch) {
            case KEY_UP:
                if (sel > 0) sel--;
                break;
            case KEY_DOWN:
                if (sel < shopCount - 1) sel++;
                break;
            case 'n': case 'N':
                if (shopCount < SHOP_DEF_MAX) {
                    memset(&shops[shopCount], 0, sizeof(ShopDef));
                    snprintf(shops[shopCount].name, 24, "Shop %d", shopCount);
                    sel = shopCount++;
                    dirty = 1;
                    screenEdit(sel);
                } else {
                    status = "Max shops reached (32).";
                }
                break;
            case 'd': case 'D':
                if (shopCount > 0) {
                    for (int i = sel; i < shopCount - 1; i++)
                        shops[i] = shops[i + 1];
                    shopCount--;
                    if (sel >= shopCount && sel > 0) sel--;
                    dirty = 1;
                    status = "Deleted.";
                }
                break;
            case '\n': case KEY_ENTER:
                if (shopCount > 0) screenEdit(sel);
                break;
            case 's': case 'S':
                saveShops();
                status = "Saved.";
                break;
            case 'q': case 'Q': {
                if (!dirty) { endwin(); return 0; }
                mvprintw(LINES - 1, 0, "Unsaved changes. Q again to quit, any other key to cancel.");
                refresh();
                int c2 = getch();
                if (c2 == 'q' || c2 == 'Q') { endwin(); return 0; }
                break;
            }
        }
    }
}
