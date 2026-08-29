/*
 * enemy_editor.c — ncurses editor for assets/enemies.dat
 *
 * Navigation:
 *   SCR_LIST  Up/Down=select  N=new  D=delete  Enter=edit  S=save  Q=quit
 *   SCR_EDIT  Up/Down=field   +/-=change numeric  Enter=edit text  Bksp=back
 */

#include <ncurses.h>
#include "refs.h"
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

typedef struct { uint8_t type, value, chance; } Effect;
typedef struct { uint8_t stateId; Effect fx; } EnemyBehavior;

typedef struct {
    char    name[16];
    uint8_t hp, attack, defense;   /* hp/attack/defense LEGACY */
    uint8_t size, speed, intelligence, perception;
    uint8_t flags;
    uint8_t xpReward;
    uint8_t goldDrop;
    uint8_t lootTableId;
    char    imgName[16];
    uint8_t stateMask;   /* combat-graph states (bit per state S0-S4) */
    uint8_t damage;      /* pressure added per turn */
    uint8_t tenacity;    /* % progress scaling; 0 = 100 */
    EnemyBehavior behaviors[2];
    uint8_t _pad[2];
} EnemyDef;  /* 56 bytes */

typedef struct {
    uint8_t enemyIds[ENEMY_POOL_SIZE];
    uint8_t count;
    uint8_t _pad[3];
    char    tileName[16];
} EnemyPool;  /* 24 bytes */

typedef char check_size[(sizeof(EnemyDef) == 56) ? 1 : -1];

#define EFX_COUNT 9
static const char *efxNames[EFX_COUNT] = {
    "none", "progress", "prog_next", "heal_hp", "dmg_hp",
    "extra_act", "status+", "status-", "meter"
};
static const char *cstateNames[5] = {
    "SquaringUp", "TradingBlows", "Staggered", "Frenzied", "Broken"
};
/* ------------------------------------------------ */

#define SCROLL_MARGIN 3

static void scroll_to(int sel, int *scroll, int visible) {
    if (sel - *scroll < SCROLL_MARGIN)               *scroll = sel - SCROLL_MARGIN;
    if (sel - *scroll > visible - SCROLL_MARGIN - 1)  *scroll = sel - visible + SCROLL_MARGIN + 1;
    if (*scroll < 0) *scroll = 0;
}

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
    F_ST_S0, F_ST_S1, F_ST_S2, F_ST_S3, F_ST_S4,
    F_DAMAGE, F_TENACITY,
    F_B1_STATE, F_B1_FX, F_B1_VAL, F_B1_CH,
    F_B2_STATE, F_B2_FX, F_B2_VAL, F_B2_CH,
    F_COUNT
} Field;

static const char *fieldNames[] = {
    "Name", "Image",
    "HP (legacy)", "ATK (legacy)", "DEF (legacy)",
    "Size (1-5)", "Speed", "Intelligence", "Perception",
    "XP Reward", "Gold Drop", "Loot Table ID",
    "Flag: Has Weapon", "Flag: Executable", "Flag: Blockable", "Flag: Stunnable",
    "State: SquaringUp", "State: TradingBlows", "State: Staggered",
    "State: Frenzied", "State: Broken",
    "Damage (pressure/turn)", "Tenacity (% progress)",
    "Behavior 1: state", "Behavior 1: effect", "Behavior 1: value", "Behavior 1: chance",
    "Behavior 2: state", "Behavior 2: effect", "Behavior 2: value", "Behavior 2: chance",
};

/* +/- editing for one behavior field; part: 0=state 1=fx 2=val 3=chance */
static void adjustBehavior(EnemyBehavior *b, int part, int dir) {
    switch (part) {
        case 0: b->stateId = refCycle(REF_STATE_COMBAT, b->stateId, dir); break;
        case 1: b->fx.type = (uint8_t)((b->fx.type + (dir > 0 ? 1 : EFX_COUNT - 1)) % EFX_COUNT); break;
        case 2: b->fx.value  = (uint8_t)(b->fx.value + dir);                  break;
        case 3: {
            int nv = (int)b->fx.chance + dir * 5;
            b->fx.chance = (uint8_t)(nv < 0 ? 0 : nv > 100 ? 100 : nv);
            break;
        }
    }
}

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
                mvprintw(row, 2, "%-18s  %s", fieldNames[i],
                         refLabel(REF_LOOT_TABLE, e->lootTableId));
                break;
            case F_FLAG_WEAPON: mvprintw(row, 2, "%-18s  %s", fieldNames[i], (e->flags & EDEF_HAS_WEAPON) ? "[X]" : "[ ]"); break;
            case F_FLAG_EXEC:   mvprintw(row, 2, "%-18s  %s", fieldNames[i], (e->flags & EDEF_EXECUTABLE) ? "[X]" : "[ ]"); break;
            case F_FLAG_BLOCK:  mvprintw(row, 2, "%-18s  %s", fieldNames[i], (e->flags & EDEF_BLOCKABLE)  ? "[X]" : "[ ]"); break;
            case F_FLAG_STUN:   mvprintw(row, 2, "%-18s  %s", fieldNames[i], (e->flags & EDEF_STUNNABLE)  ? "[X]" : "[ ]"); break;
            case F_ST_S0: case F_ST_S1: case F_ST_S2: case F_ST_S3: case F_ST_S4:
                mvprintw(row, 2, "%-22s  %s", fieldNames[i],
                         (e->stateMask & (1u << (i - F_ST_S0))) ? "[X]" : "[ ]");
                break;
            case F_DAMAGE:   mvprintw(row, 2, "%-22s  %d", fieldNames[i], e->damage);   break;
            case F_TENACITY: mvprintw(row, 2, "%-22s  %d", fieldNames[i],
                                      e->tenacity ? e->tenacity : 100);                  break;
            case F_B1_STATE: case F_B2_STATE: {
                const EnemyBehavior *b = (i == F_B1_STATE) ? &e->behaviors[0] : &e->behaviors[1];
                mvprintw(row, 2, "%-22s  %s", fieldNames[i], refLabel(REF_STATE_COMBAT, b->stateId));
                break;
            }
            case F_B1_FX: case F_B2_FX: {
                const EnemyBehavior *b = (i == F_B1_FX) ? &e->behaviors[0] : &e->behaviors[1];
                mvprintw(row, 2, "%-22s  %s", fieldNames[i],
                         b->fx.type < EFX_COUNT ? efxNames[b->fx.type] : "?");
                break;
            }
            case F_B1_VAL: case F_B2_VAL: {
                const EnemyBehavior *b = (i == F_B1_VAL) ? &e->behaviors[0] : &e->behaviors[1];
                mvprintw(row, 2, "%-22s  %d", fieldNames[i], b->fx.value);
                break;
            }
            case F_B1_CH: case F_B2_CH: {
                const EnemyBehavior *b = (i == F_B1_CH) ? &e->behaviors[0] : &e->behaviors[1];
                mvprintw(row, 2, "%-22s  %d", fieldNames[i], b->fx.chance);
                break;
            }
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
                } else if (sel == F_LOOT) {
                    e->lootTableId = refPick(REF_LOOT_TABLE, e->lootTableId); dirty = 1;
                } else if (sel == F_B1_STATE || sel == F_B2_STATE) {
                    EnemyBehavior *b = &e->behaviors[sel == F_B1_STATE ? 0 : 1];
                    b->stateId = refPick(REF_STATE_COMBAT, b->stateId); dirty = 1;
                }
                break;

            /* Left/Right step through referenced records */
            case KEY_RIGHT: case KEY_LEFT: {
                int dir = (ch == KEY_RIGHT) ? 1 : -1;
                if (sel == F_LOOT) {
                    e->lootTableId = refCycle(REF_LOOT_TABLE, e->lootTableId, dir); dirty = 1;
                } else if (sel == F_B1_STATE || sel == F_B2_STATE) {
                    EnemyBehavior *b = &e->behaviors[sel == F_B1_STATE ? 0 : 1];
                    b->stateId = refCycle(REF_STATE_COMBAT, b->stateId, dir); dirty = 1;
                }
                break;
            }

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
                        e->lootTableId = refCycle(REF_LOOT_TABLE, e->lootTableId, 1); break;
                    case F_FLAG_WEAPON: e->flags ^= EDEF_HAS_WEAPON; break;
                    case F_FLAG_EXEC:   e->flags ^= EDEF_EXECUTABLE; break;
                    case F_FLAG_BLOCK:  e->flags ^= EDEF_BLOCKABLE;  break;
                    case F_FLAG_STUN:   e->flags ^= EDEF_STUNNABLE;  break;
                    case F_ST_S0: case F_ST_S1: case F_ST_S2: case F_ST_S3: case F_ST_S4:
                        e->stateMask ^= (uint8_t)(1u << (sel - F_ST_S0)); break;
                    case F_DAMAGE:   if (e->damage   < 255) e->damage++;   break;
                    case F_TENACITY: if (e->tenacity < 200) e->tenacity++; break;
                    case F_B1_STATE: case F_B1_FX: case F_B1_VAL: case F_B1_CH:
                        adjustBehavior(&e->behaviors[0], sel - F_B1_STATE,  1); break;
                    case F_B2_STATE: case F_B2_FX: case F_B2_VAL: case F_B2_CH:
                        adjustBehavior(&e->behaviors[1], sel - F_B2_STATE,  1); break;
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
                        e->lootTableId = refCycle(REF_LOOT_TABLE, e->lootTableId, -1); break;
                    case F_FLAG_WEAPON: e->flags ^= EDEF_HAS_WEAPON; break;
                    case F_FLAG_EXEC:   e->flags ^= EDEF_EXECUTABLE; break;
                    case F_FLAG_BLOCK:  e->flags ^= EDEF_BLOCKABLE;  break;
                    case F_FLAG_STUN:   e->flags ^= EDEF_STUNNABLE;  break;
                    case F_ST_S0: case F_ST_S1: case F_ST_S2: case F_ST_S3: case F_ST_S4:
                        e->stateMask ^= (uint8_t)(1u << (sel - F_ST_S0)); break;
                    case F_DAMAGE:   if (e->damage   > 0) e->damage--;   break;
                    case F_TENACITY: if (e->tenacity > 0) e->tenacity--; break;
                    case F_B1_STATE: case F_B1_FX: case F_B1_VAL: case F_B1_CH:
                        adjustBehavior(&e->behaviors[0], sel - F_B1_STATE, -1); break;
                    case F_B2_STATE: case F_B2_FX: case F_B2_VAL: case F_B2_CH:
                        adjustBehavior(&e->behaviors[1], sel - F_B2_STATE, -1); break;
                    default: dirty = 0; break;
                }
                break;
        }
    }
}

/* ---- enemy picker (used by pool edit to choose slot contents) ---- */

/* Returns chosen enemy ID, or 0xFF to clear the slot.
   Returns current unchanged if the user cancels with Esc/Backspace. */
static uint8_t pickEnemy(uint8_t current) {
    if (enemyCount == 0) return current;

    int total  = enemyCount + 1; /* enemies 0..N-1, then "(clear)" */
    int sel    = (current == 0xFF || current >= (uint8_t)enemyCount)
                     ? enemyCount : (int)current;
    int scroll = 0;

    while (1) {
        int visible = LINES - 3;
        scroll_to(sel, &scroll, visible);
        clear();
        mvprintw(0, 0, "PICK ENEMY  [%d/%d]", sel < enemyCount ? sel + 1 : 0, enemyCount);
        mvprintw(1, 0, "Up/Down=select  Enter=confirm  Esc/Bksp=cancel");

        for (int i = scroll; i < total && i < scroll + visible; i++) {
            int row = (i - scroll) + 3;
            if (i == sel) attron(A_REVERSE);
            if (i < enemyCount) {
                char flags[5] = "----";
                if (enemies[i].flags & EDEF_HAS_WEAPON) flags[0] = 'W';
                if (enemies[i].flags & EDEF_EXECUTABLE) flags[1] = 'E';
                if (enemies[i].flags & EDEF_BLOCKABLE)  flags[2] = 'B';
                if (enemies[i].flags & EDEF_STUNNABLE)  flags[3] = 'S';
                mvprintw(row, 2, "%2d  %-15s  hp:%-3d atk:%-3d def:%-3d  [%s]",
                    i, enemies[i].name[0] ? enemies[i].name : "(unnamed)",
                    enemies[i].hp, enemies[i].attack, enemies[i].defense, flags);
            } else {
                mvprintw(row, 2, " -  (clear slot)");
            }
            if (i == sel) attroff(A_REVERSE);
        }
        refresh();

        int ch = getch();
        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--; break;
            case KEY_DOWN: if (sel < total - 1) sel++; break;
            case '\n': case KEY_ENTER:
                return (sel < enemyCount) ? (uint8_t)sel : 0xFF;
            case 27:
            case KEY_BACKSPACE: case 127:
                return current;
        }
    }
}

/* ---- pool edit screen ---- */

typedef enum {
    PF_TILE = 0,
    PF_SLOT0, PF_SLOT1, PF_SLOT2, PF_SLOT3,
    PF_COUNT,
    PF_NFIELDS
} PoolField;

static const char *pFieldNames[] = {
    "Tile Name",
    "Slot 0", "Slot 1", "Slot 2", "Slot 3",
    "Count (active)"
};

static void renderPoolEdit(EnemyPool *p, int poolIdx, int sel, const char *status) {
    clear();
    mvprintw(0, 0, "POOL EDITOR — Pool %d (loc tile 0x%02X)", poolIdx + 1, poolIdx + 1);
    mvprintw(1, 0, "Up/Down=field  +/-=change  Enter=pick/edit  Bksp=back  S=save");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = 0; i < PF_NFIELDS; i++) {
        if (i == sel) attron(A_REVERSE);
        int row = i + 4;
        if (i == PF_TILE) {
            mvprintw(row, 2, "%-16s  %s", pFieldNames[i], p->tileName[0] ? p->tileName : "(none)");
        } else if (i >= PF_SLOT0 && i <= PF_SLOT3) {
            int slot = i - PF_SLOT0;
            uint8_t id = p->enemyIds[slot];
            if (id == 0xFF)
                mvprintw(row, 2, "%-16s  --  (empty)", pFieldNames[i]);
            else if (id < (uint8_t)enemyCount)
                mvprintw(row, 2, "%-16s  %2d  (%s)", pFieldNames[i], id, enemies[id].name);
            else
                mvprintw(row, 2, "%-16s  %2d  (?)", pFieldNames[i], id);
        } else {
            mvprintw(row, 2, "%-16s  %d", pFieldNames[i], p->count);
        }
        if (i == sel) attroff(A_REVERSE);
    }
    refresh();
}

static void screenPoolEdit(int idx) {
    EnemyPool  *p      = &pools[idx];
    int         sel    = 0;
    const char *status = NULL;

    while (1) {
        renderPoolEdit(p, idx, sel, status);
        status = NULL;
        int ch = getch();

        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--; break;
            case KEY_DOWN: if (sel < PF_NFIELDS - 1) sel++; break;

            case KEY_BACKSPACE: case 127: return;

            case 's': case 'S': save(); status = "Saved."; break;

            case '\n': case KEY_ENTER:
                if (sel == PF_TILE) {
                    if (editString(sel + 4, 20, p->tileName, 16)) dirty = 1;
                } else if (sel >= PF_SLOT0 && sel <= PF_SLOT3) {
                    int slot = sel - PF_SLOT0;
                    uint8_t picked = pickEnemy(p->enemyIds[slot]);
                    if (picked != p->enemyIds[slot]) { p->enemyIds[slot] = picked; dirty = 1; }
                }
                break;

            case '+': case '=':
                dirty = 1;
                if (sel >= PF_SLOT0 && sel <= PF_SLOT3) {
                    int slot = sel - PF_SLOT0;
                    uint8_t id = p->enemyIds[slot];
                    if (id == 0xFF)
                        p->enemyIds[slot] = 0;
                    else if (enemyCount > 0 && id < (uint8_t)(enemyCount - 1))
                        p->enemyIds[slot]++;
                    else
                        dirty = 0;
                } else if (sel == PF_COUNT) {
                    if (p->count < ENEMY_POOL_SIZE) p->count++;
                    else dirty = 0;
                } else {
                    dirty = 0;
                }
                break;

            case '-':
                dirty = 1;
                if (sel >= PF_SLOT0 && sel <= PF_SLOT3) {
                    int slot = sel - PF_SLOT0;
                    uint8_t id = p->enemyIds[slot];
                    if (id == 0)
                        p->enemyIds[slot] = 0xFF;
                    else if (id == 0xFF)
                        p->enemyIds[slot] = enemyCount > 0 ? (uint8_t)(enemyCount - 1) : 0xFF;
                    else
                        p->enemyIds[slot]--;
                } else if (sel == PF_COUNT) {
                    if (p->count > 0) p->count--;
                    else dirty = 0;
                } else {
                    dirty = 0;
                }
                break;
        }
    }
}

/* ---- pool list screen ---- */

static void renderPools(int sel, int scroll, const char *status) {
    clear();
    int visible = LINES - 4;
    mvprintw(0, 0, "POOL LIST  [%s]  (%d/%d)",
             dirty ? "unsaved" : "saved", poolCount > 0 ? sel + 1 : 0, poolCount);
    mvprintw(1, 0, "Up/Down=select  Enter=edit  N=new  D=delete  Bksp=back  S=save");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = scroll; i < poolCount && i < scroll + visible; i++) {
        int row = (i - scroll) + 4;
        if (i == sel) attron(A_REVERSE);
        char members[64] = "";
        int n = pools[i].count < ENEMY_POOL_SIZE ? pools[i].count : ENEMY_POOL_SIZE;
        for (int s = 0; s < n; s++) {
            uint8_t id = pools[i].enemyIds[s];
            if (s > 0) strncat(members, " ", sizeof(members) - strlen(members) - 1);
            if (id < (uint8_t)enemyCount)
                strncat(members, enemies[id].name, sizeof(members) - strlen(members) - 1);
            else
                strncat(members, "?", sizeof(members) - strlen(members) - 1);
        }
        mvprintw(row, 2, "Pool %2d (0x%02X)  tile:%-16s  [%s]",
            i + 1, i + 1,
            pools[i].tileName[0] ? pools[i].tileName : "(none)",
            members[0] ? members : "empty");
        if (i == sel) attroff(A_REVERSE);
    }

    if (poolCount == 0)
        mvprintw(4, 2, "(no pools -- press N to add one)");

    refresh();
}

static void screenPools(void) {
    int  sel    = 0;
    int  scroll = 0;
    const char *status = NULL;

    while (1) {
        if (sel >= poolCount && poolCount > 0) sel = poolCount - 1;
        if (sel < 0) sel = 0;
        scroll_to(sel, &scroll, LINES - 4);

        renderPools(sel, scroll, status);
        status = NULL;
        int ch = getch();

        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--; break;
            case KEY_DOWN: if (sel < poolCount - 1) sel++; break;

            case '\n': case KEY_ENTER:
                if (poolCount > 0) screenPoolEdit(sel);
                break;

            case 'n': case 'N':
                if (poolCount < ENEMY_POOL_MAX) {
                    memset(&pools[poolCount], 0, sizeof(EnemyPool));
                    memset(pools[poolCount].enemyIds, 0xFF, ENEMY_POOL_SIZE);
                    pools[poolCount].count = 0;
                    sel = poolCount++;
                    dirty = 1;
                    screenPoolEdit(sel);
                } else {
                    status = "Max pools reached (15).";
                }
                break;

            case 'd': case 'D':
                if (poolCount > 0) {
                    for (int i = sel; i < poolCount - 1; i++)
                        pools[i] = pools[i + 1];
                    poolCount--;
                    dirty = 1;
                    status = "Pool deleted.";
                }
                break;

            case 's': case 'S':
                save();
                status = "Saved.";
                break;

            case KEY_BACKSPACE: case 127:
                return;
        }
    }
}

/* ---- list screen ---- */

static void renderList(int sel, int scroll, const char *status) {
    clear();
    int visible = LINES - 4;
    mvprintw(0, 0, "ENEMY LIST  [%s]  (%d/%d)",
             dirty ? "unsaved" : "saved", enemyCount > 0 ? sel + 1 : 0, enemyCount);
    mvprintw(1, 0, "Up/Down=select  Enter=edit  N=new  D=delete  P=pools  S=save  Q=quit");
    if (status) mvprintw(2, 0, "%s", status);

    for (int i = scroll; i < enemyCount && i < scroll + visible; i++) {
        int row = (i - scroll) + 4;
        if (i == sel) attron(A_REVERSE);
        char flags[5] = "----";
        if (enemies[i].flags & EDEF_HAS_WEAPON) flags[0] = 'W';
        if (enemies[i].flags & EDEF_EXECUTABLE) flags[1] = 'E';
        if (enemies[i].flags & EDEF_BLOCKABLE)  flags[2] = 'B';
        if (enemies[i].flags & EDEF_STUNNABLE)  flags[3] = 'S';
        mvprintw(row, 2, "%2d  %-15s  hp:%-3d atk:%-3d def:%-3d  [%s]  img:%s",
            i,
            enemies[i].name[0] ? enemies[i].name : "(unnamed)",
            enemies[i].hp, enemies[i].attack, enemies[i].defense,
            flags,
            enemies[i].imgName[0] ? enemies[i].imgName : "--");
        if (i == sel) attroff(A_REVERSE);
    }

    if (enemyCount == 0)
        mvprintw(4, 2, "(no enemies -- press N to add one)");

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
        if (sel >= enemyCount && enemyCount > 0) sel = enemyCount - 1;
        if (sel < 0) sel = 0;
        scroll_to(sel, &scroll, LINES - 4);

        renderList(sel, scroll, status);
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

            case 'p': case 'P':
                screenPools();
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
