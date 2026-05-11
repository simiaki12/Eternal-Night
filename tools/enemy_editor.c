/*
 * enemy_editor.c — ncurses editor for assets/enemies.dat
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

/* ---- mirror of src/enemies.h (keep in sync) ---- */
#define EDEF_HAS_WEAPON  (1<<0)
#define EDEF_EXECUTABLE  (1<<1)
#define EDEF_BLOCKABLE   (1<<2)
#define EDEF_STUNNABLE   (1<<3)

#define ENEMY_DEF_MAX   32
#define ENEMY_POOL_MAX  15
#define ENEMY_POOL_SIZE  4

typedef struct {
    char    name[16];
    uint8_t hp, attack, defense;
    uint8_t size, speed, intelligence, perception;
    uint8_t flags;
    uint8_t xpReward;
    uint8_t goldDrop;
    uint8_t lootTableId;
    char    imgName[16];
    uint8_t _pad[5];
} EnemyDef;  /* 48 bytes */

typedef struct {
    uint8_t enemyIds[ENEMY_POOL_SIZE];
    uint8_t count;
    uint8_t _pad[3];
    char    tileName[16];
} EnemyPool;  /* 24 bytes */

typedef char check_size[(sizeof(EnemyDef) == 48) ? 1 : -1];
/* ------------------------------------------------ */

#define MAX_ENEMIES 32
static EnemyDef  enemies[MAX_ENEMIES];
static int       enemyCount = 0;
static EnemyPool pools[ENEMY_POOL_MAX];
static int       poolCount  = 0;
static int       dirty      = 0;
static const char *outfile  = "assets/data/enemies.dat";

/* ---- file I/O ---- */

static void load(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) { enemyCount = 0; poolCount = 0; return; }
    uint8_t n;
    if (fread(&n, 1, 1, f) != 1) { fclose(f); return; }
    if (n > MAX_ENEMIES) n = MAX_ENEMIES;
    enemyCount = (int)fread(enemies, sizeof(EnemyDef), n, f);
    uint8_t p;
    if (fread(&p, 1, 1, f) == 1) {
        if (p > ENEMY_POOL_MAX) p = ENEMY_POOL_MAX;
        poolCount = (int)fread(pools, sizeof(EnemyPool), p, f);
    }
    fclose(f);
}

static void save(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;
    uint8_t n = (uint8_t)enemyCount;
    fwrite(&n, 1, 1, f);
    fwrite(enemies, sizeof(EnemyDef), enemyCount, f);
    uint8_t p = (uint8_t)poolCount;
    fwrite(&p, 1, 1, f);
    fwrite(pools, sizeof(EnemyPool), poolCount, f);
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
    F_NAME = 0,
    F_IMG,
    F_HP, F_ATK, F_DEF,
    F_SIZE, F_SPEED, F_INT, F_PER,
    F_XP, F_GOLD, F_LOOT,
    F_FLAG_WEAPON, F_FLAG_EXEC, F_FLAG_BLOCK, F_FLAG_STUN,
    F_COUNT
} Field;

static const char *fieldNames[] = {
    "Name", "Image",
    "HP", "ATK", "DEF",
    "Size (1-5)", "Speed", "Intelligence", "Perception",
    "XP Reward", "Gold Drop", "Loot Table ID",
    "Flag: Has Weapon", "Flag: Executable", "Flag: Blockable", "Flag: Stunnable"
};

static void renderEdit(EnemyDef *e, int sel, const char *status) {
    clear();
    mvprintw(0, 0, "ENEMY EDITOR — %s", e->name[0] ? e->name : "(unnamed)");
    mvprintw(1, 0, "Up/Down=field  +/-=change  Enter=edit text  Bksp=back  S=save");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = 0; i < F_COUNT; i++) {
        if (i == sel) attron(A_REVERSE);
        int row = i + 4;
        switch (i) {
            case F_NAME:  mvprintw(row, 2, "%-18s  %s",  fieldNames[i], e->name);    break;
            case F_IMG:   mvprintw(row, 2, "%-18s  %s",  fieldNames[i], e->imgName); break;
            case F_HP:    mvprintw(row, 2, "%-18s  %d",  fieldNames[i], e->hp);       break;
            case F_ATK:   mvprintw(row, 2, "%-18s  %d",  fieldNames[i], e->attack);   break;
            case F_DEF:   mvprintw(row, 2, "%-18s  %d",  fieldNames[i], e->defense);  break;
            case F_SIZE:  mvprintw(row, 2, "%-18s  %d",  fieldNames[i], e->size);     break;
            case F_SPEED: mvprintw(row, 2, "%-18s  %d",  fieldNames[i], e->speed);    break;
            case F_INT:   mvprintw(row, 2, "%-18s  %d",  fieldNames[i], e->intelligence); break;
            case F_PER:   mvprintw(row, 2, "%-18s  %d",  fieldNames[i], e->perception);   break;
            case F_XP:    mvprintw(row, 2, "%-18s  %d",  fieldNames[i], e->xpReward); break;
            case F_GOLD:  mvprintw(row, 2, "%-18s  %d",  fieldNames[i], e->goldDrop); break;
            case F_LOOT:
                if (e->lootTableId == 0xFF)
                    mvprintw(row, 2, "%-18s  none", fieldNames[i]);
                else
                    mvprintw(row, 2, "%-18s  %d",  fieldNames[i], e->lootTableId);
                break;
            case F_FLAG_WEAPON: mvprintw(row, 2, "%-18s  %s", fieldNames[i], (e->flags & EDEF_HAS_WEAPON) ? "[X]" : "[ ]"); break;
            case F_FLAG_EXEC:   mvprintw(row, 2, "%-18s  %s", fieldNames[i], (e->flags & EDEF_EXECUTABLE) ? "[X]" : "[ ]"); break;
            case F_FLAG_BLOCK:  mvprintw(row, 2, "%-18s  %s", fieldNames[i], (e->flags & EDEF_BLOCKABLE)  ? "[X]" : "[ ]"); break;
            case F_FLAG_STUN:   mvprintw(row, 2, "%-18s  %s", fieldNames[i], (e->flags & EDEF_STUNNABLE)  ? "[X]" : "[ ]"); break;
        }
        if (i == sel) attroff(A_REVERSE);
    }
    refresh();
}

static void screenEdit(int idx) {
    EnemyDef   *e   = &enemies[idx];
    int         sel = 0;
    const char *status = NULL;

    while (1) {
        renderEdit(e, sel, status);
        status = NULL;
        int ch = getch();

        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--; break;
            case KEY_DOWN: if (sel < F_COUNT - 1) sel++; break;

            case KEY_BACKSPACE: case 127: return;

            case 's': case 'S': save(); status = "Saved."; break;

            case '\n': case KEY_ENTER:
                if (sel == F_NAME) {
                    if (editString(sel + 4, 22, e->name, 16)) dirty = 1;
                } else if (sel == F_IMG) {
                    if (editString(sel + 4, 22, e->imgName, 16)) dirty = 1;
                }
                break;

            case '+': case '=':
                dirty = 1;
                switch (sel) {
                    case F_HP:    if (e->hp         < 255) e->hp++;          break;
                    case F_ATK:   if (e->attack     < 255) e->attack++;      break;
                    case F_DEF:   if (e->defense    < 255) e->defense++;     break;
                    case F_SIZE:  if (e->size       <   5) e->size++;        break;
                    case F_SPEED: if (e->speed      < 255) e->speed++;       break;
                    case F_INT:   if (e->intelligence < 255) e->intelligence++; break;
                    case F_PER:   if (e->perception < 255) e->perception++;  break;
                    case F_XP:    if (e->xpReward   < 255) e->xpReward++;    break;
                    case F_GOLD:  if (e->goldDrop   < 255) e->goldDrop++;    break;
                    case F_LOOT:
                        e->lootTableId = (e->lootTableId == 0xFF) ? 0
                                       : (e->lootTableId < 254)   ? e->lootTableId + 1
                                       : 0xFF;
                        break;
                    case F_FLAG_WEAPON: e->flags ^= EDEF_HAS_WEAPON; break;
                    case F_FLAG_EXEC:   e->flags ^= EDEF_EXECUTABLE; break;
                    case F_FLAG_BLOCK:  e->flags ^= EDEF_BLOCKABLE;  break;
                    case F_FLAG_STUN:   e->flags ^= EDEF_STUNNABLE;  break;
                    default: dirty = 0; break;
                }
                break;

            case '-':
                dirty = 1;
                switch (sel) {
                    case F_HP:    if (e->hp          > 1) e->hp--;          break;
                    case F_ATK:   if (e->attack      > 0) e->attack--;      break;
                    case F_DEF:   if (e->defense     > 0) e->defense--;     break;
                    case F_SIZE:  if (e->size        > 1) e->size--;        break;
                    case F_SPEED: if (e->speed       > 0) e->speed--;       break;
                    case F_INT:   if (e->intelligence > 0) e->intelligence--; break;
                    case F_PER:   if (e->perception  > 0) e->perception--;  break;
                    case F_XP:    if (e->xpReward    > 0) e->xpReward--;    break;
                    case F_GOLD:  if (e->goldDrop    > 0) e->goldDrop--;    break;
                    case F_LOOT:
                        e->lootTableId = (e->lootTableId == 0)    ? 0xFF
                                       : (e->lootTableId == 0xFF) ? 254
                                       : e->lootTableId - 1;
                        break;
                    case F_FLAG_WEAPON: e->flags ^= EDEF_HAS_WEAPON; break;
                    case F_FLAG_EXEC:   e->flags ^= EDEF_EXECUTABLE; break;
                    case F_FLAG_BLOCK:  e->flags ^= EDEF_BLOCKABLE;  break;
                    case F_FLAG_STUN:   e->flags ^= EDEF_STUNNABLE;  break;
                    default: dirty = 0; break;
                }
                break;
        }
    }
}

/* ---- list screen ---- */

static void renderList(int sel, const char *status) {
    clear();
    mvprintw(0, 0, "ENEMY LIST  [%s]  (%d enemies)", dirty ? "unsaved" : "saved", enemyCount);
    mvprintw(1, 0, "Up/Down=select  Enter=edit  N=new  D=delete  S=save  Q=quit");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = 0; i < enemyCount; i++) {
        if (i == sel) attron(A_REVERSE);
        char flags[5] = "----";
        if (enemies[i].flags & EDEF_HAS_WEAPON) flags[0] = 'W';
        if (enemies[i].flags & EDEF_EXECUTABLE) flags[1] = 'E';
        if (enemies[i].flags & EDEF_BLOCKABLE)  flags[2] = 'B';
        if (enemies[i].flags & EDEF_STUNNABLE)  flags[3] = 'S';
        mvprintw(i + 4, 2, "%2d  %-15s  hp:%-3d atk:%-3d def:%-3d  [%s]  img:%s",
            i,
            enemies[i].name[0] ? enemies[i].name : "(unnamed)",
            enemies[i].hp, enemies[i].attack, enemies[i].defense,
            flags,
            enemies[i].imgName[0] ? enemies[i].imgName : "--");
        if (i == sel) attroff(A_REVERSE);
    }

    if (enemyCount == 0)
        mvprintw(4, 2, "(no enemies — press N to add one)");

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
        if (sel >= enemyCount && enemyCount > 0) sel = enemyCount - 1;
        if (sel < 0) sel = 0;

        renderList(sel, status);
        status = NULL;
        int ch = getch();

        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--; break;
            case KEY_DOWN: if (sel < enemyCount - 1) sel++; break;

            case '\n': case KEY_ENTER:
                if (enemyCount > 0) screenEdit(sel);
                break;

            case 'n': case 'N':
                if (enemyCount < MAX_ENEMIES) {
                    memset(&enemies[enemyCount], 0, sizeof(EnemyDef));
                    enemies[enemyCount].lootTableId = 0xFF;
                    enemies[enemyCount].size        = 1;
                    sel = enemyCount++;
                    dirty = 1;
                    screenEdit(sel);
                } else {
                    status = "Max enemies reached.";
                }
                break;

            case 'd': case 'D':
                if (enemyCount > 0) {
                    for (int i = sel; i < enemyCount - 1; i++)
                        enemies[i] = enemies[i + 1];
                    enemyCount--;
                    dirty = 1;
                    status = "Enemy deleted.";
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
