#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "combat.h"
#include "actions.h"
#include "domains.h"
#include "game.h"
#include "player.h"
#include "items.h"
#include "gfx.h"
#include "quests.h"
#include "loot.h"
#include "world_enemies.h"

CombatState combat;


/* Lazily-loaded sprites for action cards — indexed by action id, loaded on first render */
static PakData actionImgs[ACTION_MAX];

static void logPush(const char *msg) {
    if (combat.logCount == 8) {
        for (int i = 0; i < 7; i++)
            memcpy(combat.log[i], combat.log[i + 1], sizeof(combat.log[0]));
        combat.logCount = 7;
    }
    strncpy(combat.log[combat.logCount], msg, sizeof(combat.log[0]) - 1);
    combat.log[combat.logCount][sizeof(combat.log[0]) - 1] = '\0';
    combat.logCount++;
}

static int checkContext(const ActionDef *def) {
    uint8_t ctx = def->contextFlags;
    if ((ctx & ACT_CTX_FIRST_TURN)   && !combat.isFirstTurn)                      return 0;
    if ((ctx & ACT_CTX_ENEMY_WEAPON) && !(combat.enemy.flags & ENEMY_HAS_WEAPON)) return 0;
    if ((ctx & ACT_CTX_CAN_STUN)     && !(combat.enemy.flags & ENEMY_STUNNABLE))  return 0;
    if ((ctx & ACT_CTX_PLAYER_HURT)  && (int)player.hp >= getMaxHp() / 2)          return 0;
    if ((ctx & ACT_CTX_REQUIRES_DARK) && !(combat.modifiers & ENCOUNTER_MOD_DARK))      return 0;
    if ((ctx & ACT_CTX_BLOCKED_HOLY)  && (combat.modifiers & ENCOUNTER_MOD_HOLY_GROUND)) return 0;
    if (ctx & ACT_CTX_EXECUTABLE) {
        if (!(combat.enemy.flags & ENEMY_EXECUTABLE))  return 0;
        if (combat.enemy.hp > combat.enemy.maxHp / 3)  return 0;
    }
    return 1;
}

static int computeWeight(const ActionDef *def) {
    int w = def->baseWeight;
    switch (def->id) {
        case ACTION_STRONG:
            w += combat.enemy.size  * 5;
            w -= combat.enemy.speed * 3;
            break;
        case ACTION_DISARM:
            w += combat.enemy.speed * 3;
            break;
        case ACTION_BACKSTAB:
        case ACTION_HIDE:
            w -= combat.enemy.perception * 4;
            break;
        case ACTION_CALM:
            w += combat.enemy.intelligence * 5;
            break;
        default:
            break;
    }

    /* Encounter modifier adjustments */
    if (combat.modifiers & ENCOUNTER_MOD_DARK) {
        if (def->id == ACTION_BACKSTAB) w += 15;
        if (def->id == ACTION_HIDE)     w += 10;
    }
    if (combat.modifiers & ENCOUNTER_MOD_HOLY_GROUND) {
        if (def->id == ACTION_HEAL)     w -= 20;
    }
    if (combat.modifiers & ENCOUNTER_MOD_CROWDED) {
        if (def->id == ACTION_CALM)     w += 15;
        if (def->id == ACTION_HIDE)     w += 10;
    }
    if (combat.modifiers & ENCOUNTER_MOD_BURNING) {
        if (def->id == ACTION_STRONG)   w += 10;
        if (def->id == ACTION_DEFEND)   w -= 10;
    }

    /* Domain affinity — reward what the player has invested in */
    uint8_t primary = domainPrimary();
    if (player.domains[primary].level > 0 && def->domain == primary)
        w += 20;

    /* Focused domain — stronger pull toward the player's chosen style */
    if (player.focusedDomain != DOMAIN_NONE && def->domain == player.focusedDomain)
        w += 35;

    return w < 1 ? 1 : w;
}


static void generateActions(void) {
    uint8_t pool[ACTION_MAX];
    int poolSize = buildActionPool(pool, (uint8_t)combat.encounterType);

    typedef struct { uint8_t id; int weight; } Candidate;
    Candidate candidates[ACTION_MAX];
    int candCount = 0;

    for (int i = 0; i < poolSize; i++) {
        const ActionDef *def = getActionDef(pool[i]);
        if (!def) continue;
        if (!checkContext(def)) continue;
        candidates[candCount].id     = pool[i];
        candidates[candCount].weight = computeWeight(def);
        candCount++;
    }

    combat.actionCount = 0;
    int picks = candCount < 4 ? candCount : 4;
    for (int pick = 0; pick < picks; pick++) {
        int total = 0;
        for (int i = 0; i < candCount; i++) total += candidates[i].weight;
        int r = rand() % total;
        int acc = 0;
        for (int i = 0; i < candCount; i++) {
            acc += candidates[i].weight;
            if (r < acc) {
                const ActionDef *chosen = getActionDef(candidates[i].id);
                combat.actions[combat.actionCount++] = (Action){ (ActionId)chosen->id, chosen->power };
                candidates[i] = candidates[--candCount];
                break;
            }
        }
    }
    combat.selectedIndex = 0;
}

void startCombat(const EnemyDef *def) {
    /* Free previous enemy image if any */
    if (combat.enemyImg.data) { free(combat.enemyImg.data); combat.enemyImg.data = NULL; }

    /* Load sprite */
    if (def->imgName[0]) {
        char path[32];
        snprintf(path, sizeof(path), "assets/sprites/%s.bin", def->imgName);
        combat.enemyImg = pakRead(path);
    }

    int idx = (int)(def - enemyDefs);
    combat.enemyDefId = (idx >= 0 && idx < enemyDefCount) ? (uint8_t)idx : 0xFF;
    memcpy(combat.enemy.name, def->name, 16);
    combat.enemy.hp           = def->hp;
    combat.enemy.maxHp        = def->hp;
    combat.enemy.attack       = def->attack;
    combat.enemy.defense      = def->defense;
    combat.enemy.size         = def->size;
    combat.enemy.speed        = def->speed;
    combat.enemy.intelligence = def->intelligence;
    combat.enemy.perception   = def->perception;
    combat.enemy.flags        = def->flags;
    combat.enemy.xpReward     = def->xpReward;
    combat.enemy.goldDrop     = def->goldDrop;
    combat.enemy.lootTableId  = def->lootTableId;
    combat.isFirstTurn        = 1;
    combat.skipEnemyAttack    = 0;
    combat.phase              = COMBAT_PHASE_ACTIVE;
    combat.gainedGold         = 0;
    combat.droppedCount       = 0;
    combat.fromWorldEnemy     = 0;
    combat.encounterType      = ENCOUNTER_COMBAT;
    combat.modifiers          = 0;
    combat.logCount           = 0;
    memset(combat.gainedDomainXp, 0, sizeof(combat.gainedDomainXp));
    /* Atmospheric opening line */
    {
        char opening[28];
        if      (combat.modifiers & ENCOUNTER_MOD_DARK)        snprintf(opening, 28, "Darkness surrounds you.");
        else if (combat.modifiers & ENCOUNTER_MOD_HOLY_GROUND) snprintf(opening, 28, "Sacred ground burns you.");
        else if (combat.modifiers & ENCOUNTER_MOD_RAINING)     snprintf(opening, 28, "Rain hammers the ground.");
        else if (combat.modifiers & ENCOUNTER_MOD_CROWDED)     snprintf(opening, 28, "Voices echo around you.");
        else if (combat.modifiers & ENCOUNTER_MOD_BURNING)     snprintf(opening, 28, "Flames crackle nearby.");
        else                                                    snprintf(opening, 28, "%.12s bars your path.", combat.enemy.name);
        logPush(opening);
    }
    generateActions();
    state = STATE_COMBAT;
}

static void performPlayerAction(void) {
    Action *a = &combat.actions[combat.selectedIndex];
    combat.skipEnemyAttack = 0;
    int ehpBefore = combat.enemy.hp;
    int phpBefore = (int)player.hp;

    switch (a->type) {
        case ACTION_ATTACK:
            combat.enemy.hp -= getAttack();
            break;

        case ACTION_STRONG:
            combat.enemy.hp -= getAttack() + a->power;
            break;

        case ACTION_HEAL: {
            int newHp = (int)player.hp + a->power;
            int cap   = getMaxHp();
            player.hp = (uint16_t)(newHp > cap ? cap : newHp);
            break;
        }

        case ACTION_DEFEND:
            /* Damage reduction applied below in enemy counter-attack */
            break;

        case ACTION_DISARM:
            combat.enemy.flags  &= ~ENEMY_HAS_WEAPON;
            combat.enemy.attack  = combat.enemy.attack > 1 ? combat.enemy.attack / 2 : 1;
            break;

        case ACTION_BACKSTAB:
            combat.enemy.hp    -= getAttack() + a->power;
            combat.skipEnemyAttack = 1;
            break;

        case ACTION_STUN:
            combat.skipEnemyAttack = 1;
            /* Remove stunnable flag so Stun won't appear again */
            combat.enemy.flags &= ~ENEMY_STUNNABLE;
            break;

        case ACTION_CALM:
            combat.enemy.attack  = combat.enemy.attack > 1 ? combat.enemy.attack / 2 : 1;
            /* Remove calm option once used */
            combat.enemy.intelligence = 0;
            break;

        case ACTION_HIDE:
            combat.skipEnemyAttack = (player.agility > combat.enemy.perception);
            break;

        case ACTION_EXECUTE:
            combat.enemy.hp -= getAttack() * 2 + a->power;
            break;

        /* ── Domain: Combat ─────────────────────────────────────── */
        case ACTION_INTIMIDATE:
            combat.enemy.attack = combat.enemy.attack > 1 ? combat.enemy.attack / 2 : 1;
            combat.skipEnemyAttack = 1;
            break;

        case ACTION_COUNTER:
            combat.enemy.hp -= getAttack() * 2;
            combat.skipEnemyAttack = 1;
            break;

        case ACTION_WAR_CRY:
            combat.skipEnemyAttack = 1;
            break;

        case ACTION_THREATEN:
            if (combat.encounterType == ENCOUNTER_SOCIAL) {
                combat.phase = COMBAT_PHASE_VICTORY;
                combat.skipEnemyAttack = 1;
            } else {
                combat.enemy.attack = combat.enemy.attack > 1 ? combat.enemy.attack / 2 : 1;
            }
            break;

        case ACTION_AMBUSH:
            combat.enemy.hp -= getAttack() + a->power;
            combat.skipEnemyAttack = 1;
            break;

        /* ── Domain: Trickery ───────────────────────────────────── */
        case ACTION_VANISH:
            combat.skipEnemyAttack = 1;
            combat.enemy.perception = combat.enemy.perception > 1 ? combat.enemy.perception - 1 : 0;
            break;

        case ACTION_POISON:
            combat.enemy.hp -= getAttack();
            combat.enemy.attack = combat.enemy.attack > 1 ? combat.enemy.attack - 1 : 1;
            break;

        case ACTION_SET_TRAP:
            combat.enemy.hp -= a->power;
            combat.skipEnemyAttack = 1;
            break;

        case ACTION_DECEIVE:
            combat.phase = COMBAT_PHASE_VICTORY;
            combat.skipEnemyAttack = 1;
            break;

        case ACTION_PICKPOCKET: {
            int stolen = combat.enemy.goldDrop / 2 + 1;
            player.gold += (uint16_t)stolen;
            combat.gainedGold += stolen;
            combat.skipEnemyAttack = 1;
            break;
        }

        case ACTION_INSPECT:
            combat.enemy.defense = combat.enemy.defense > 1 ? combat.enemy.defense / 2 : 0;
            combat.skipEnemyAttack = 1;
            break;

        case ACTION_TRACK:
            combat.enemy.hp -= getAttack() + a->power;
            combat.skipEnemyAttack = 1;
            break;

        /* ── Domain: Blood ──────────────────────────────────────── */
        case ACTION_BLOOD_DRAIN: {
            int dmg = getAttack() + a->power;
            combat.enemy.hp -= dmg;
            int newHp = (int)player.hp + dmg / 2;
            int cap   = getMaxHp();
            player.hp = (uint16_t)(newHp > cap ? cap : newHp);
            break;
        }

        case ACTION_BITE:
            combat.enemy.hp -= getAttack() * 2 + a->power;
            break;

        case ACTION_BLOOD_HOWL:
            /* damages all enemies — until multi-enemy, hits the single target */
            combat.enemy.hp -= a->power;
            break;

        case ACTION_BLOOD_SURGE: {
            int cost = a->power / 2;
            player.hp = ((int)player.hp > cost) ? (uint16_t)(player.hp - cost) : 1;
            combat.enemy.hp -= getAttack() * 2 + a->power;
            combat.skipEnemyAttack = 1;
            break;
        }

        case ACTION_BLOOD_SCENT: {
            int bonus = (combat.enemy.hp < combat.enemy.maxHp / 2) ? a->power * 2 : a->power;
            combat.enemy.hp -= getAttack() + bonus;
            break;
        }

        /* ── Domain: Charm ──────────────────────────────────────── */
        case ACTION_DOMINATE:
            combat.phase = COMBAT_PHASE_VICTORY;
            combat.skipEnemyAttack = 1;
            break;

        case ACTION_MESMERIZE:
            combat.skipEnemyAttack = 1;
            combat.enemy.flags &= ~ENEMY_STUNNABLE;
            break;

        case ACTION_BRIBE: {
            const int BRIBE_COST = 15;
            if (player.gold >= BRIBE_COST) {
                player.gold -= BRIBE_COST;
                combat.phase = COMBAT_PHASE_VICTORY;
            } else {
                combat.skipEnemyAttack = 1; /* not enough gold — enemy hesitates */
            }
            break;
        }

        case ACTION_SILVER_TONGUE:
            combat.phase = COMBAT_PHASE_VICTORY;
            combat.skipEnemyAttack = 1;
            break;

        case ACTION_INTERROGATE:
            combat.enemy.defense = combat.enemy.defense > 1 ? combat.enemy.defense / 2 : 0;
            combat.skipEnemyAttack = 1;
            break;

        /* ── Environmental ──────────────────────────────────────── */
        case ACTION_FEED:
        case ACTION_RALLY: {
            int newHp = (int)player.hp + a->power;
            int cap   = getMaxHp();
            player.hp = (uint16_t)(newHp > cap ? cap : newHp);
            break;
        }

        case ACTION_BLEND_IN:
            combat.skipEnemyAttack = 1;
            combat.enemy.perception = combat.enemy.perception > 2 ? combat.enemy.perception - 2 : 0;
            break;

        default:
            break;
    }

    /* Log action result */
    {
        int dealt  = ehpBefore - combat.enemy.hp;
        int healed = (int)player.hp - phpBefore;
        const ActionDef *adef = getActionDef((uint8_t)a->type);
        const char *nm = adef ? adef->name : "?";
        char lm[28];
        if      (dealt > 0 && healed > 0) snprintf(lm, 28, "%.10s: %d dmg+%d HP.", nm, dealt, healed);
        else if (dealt > 0)               snprintf(lm, 28, "%.12s: %d dmg.", nm, dealt);
        else if (healed > 0)              snprintf(lm, 28, "%.14s: +%d HP.", nm, healed);
        else if (healed < 0)              snprintf(lm, 28, "%.8s: -%d HP.", nm, -healed);
        else                              snprintf(lm, 28, "%.26s.", nm);
        logPush(lm);
    }

    /* Domain XP for the action used */
    {
        uint8_t dom = actionGetDomain((uint8_t)a->type);
        if (dom != 0xFF) {
            domainAwardXp(dom, 1);
            if (combat.gainedDomainXp[dom] < 255)
                combat.gainedDomainXp[dom]++;
        }
    }

    /* Enemy counter-attack */
    if (combat.enemy.hp > 0 && !combat.skipEnemyAttack && combat.phase == COMBAT_PHASE_ACTIVE) {
        int dmg = combat.enemy.attack;
        if (a->type == ACTION_DEFEND)
            dmg = dmg / 2 + 1;
        player.hp = (dmg >= (int)player.hp) ? 0 : (uint16_t)(player.hp - dmg);
        char cm[28];
        snprintf(cm, 28, "%.10s: %d dmg.", combat.enemy.name, dmg);
        logPush(cm);
    }

    if (combat.enemy.hp <= 0) {
        combat.gainedGold = 0;
        if (combat.enemy.goldDrop > 0) {
            combat.gainedGold = rand() % combat.enemy.goldDrop + 1;
            player.gold += (uint16_t)combat.gainedGold;
        }
        rollLoot(combat.enemy.lootTableId, combat.droppedItems, &combat.droppedCount);
        combat.phase = COMBAT_PHASE_VICTORY;
        questOnEnemyKilled(combat.enemyDefId);
        { char vm[28]; snprintf(vm, 28, "%.20s falls.", combat.enemy.name); logPush(vm); }
        return;
    }
    if (player.hp == 0) {
        logPush("You fall unconscious.");
        enterDeath();
        return;
    }

    combat.isFirstTurn = 0;
    generateActions();
}

void handleCombatInput(int key) {
    if (combat.phase == COMBAT_PHASE_VICTORY) {
        if (key == VK_RETURN || key == VK_ESCAPE) {
            if (combat.fromWorldEnemy)
                worldEnemyRemoveAt(combat.worldEnemyX, combat.worldEnemyY);
            state = STATE_WORLD;
        }
        return;
    }
    switch (key) {
        case VK_LEFT:
        case VK_UP:
            combat.selectedIndex = (combat.selectedIndex + combat.actionCount - 1) % combat.actionCount;
            break;
        case VK_RIGHT:
        case VK_DOWN:
            combat.selectedIndex = (combat.selectedIndex + 1) % combat.actionCount;
            break;
        case VK_RETURN: performPlayerAction(); break;
        case VK_ESCAPE: state = STATE_WORLD;  break;
    }
}

/* 4px border, rounded corners via 2×2 big-pixel connector blocks.
   Background is drawn as a cross-shape to leave the 4×4 corner areas empty.
   Each corner: 2×2 transparent tip, two 2×2 border connectors on each arm,
   and a 2×2 bg fill in the inner position to complete the curve. */
static void drawCard(int cx, int cy, int cw, int ch,
                     uint32_t bgCol, uint32_t bdCol) {
    const int R  = 4; /* corner cutout size */
    const int BT = 4; /* border thickness   */

    /* Background — three rects skipping the R×R corner areas */
    fillRect(cx + R,      cy,      cw - R*2, ch,      bgCol);
    fillRect(cx,          cy + R,  R,        ch - R*2, bgCol);
    fillRect(cx + cw - R, cy + R,  R,        ch - R*2, bgCol);

    /* Border — BT-thick on all four sides */
    fillRect(cx + R, cy,           cw - R*2, BT, bdCol); /* top    */
    fillRect(cx + R, cy + ch - BT, cw - R*2, BT, bdCol); /* bottom */
    fillRect(cx,           cy + R, BT, ch - R*2, bdCol); /* left   */
    fillRect(cx + cw - BT, cy + R, BT, ch - R*2, bdCol); /* right  */

    /* Corner connectors: two 2×2 border blocks per arm + inner bg fill */
    /* Top-Left */
    fillRect(cx + 2, cy,     2, 2, bdCol);
    fillRect(cx,     cy + 2, 2, 2, bdCol);
    fillRect(cx + 2, cy + 2, 2, 2, bgCol);
    /* Top-Right */
    fillRect(cx + cw - 4, cy,     2, 2, bdCol);
    fillRect(cx + cw - 2, cy + 2, 2, 2, bdCol);
    fillRect(cx + cw - 4, cy + 2, 2, 2, bgCol);
    /* Bottom-Left */
    fillRect(cx + 2, cy + ch - 2, 2, 2, bdCol);
    fillRect(cx,     cy + ch - 4, 2, 2, bdCol);
    fillRect(cx + 2, cy + ch - 4, 2, 2, bgCol);
    /* Bottom-Right */
    fillRect(cx + cw - 4, cy + ch - 2, 2, 2, bdCol);
    fillRect(cx + cw - 2, cy + ch - 4, 2, 2, bdCol);
    fillRect(cx + cw - 4, cy + ch - 4, 2, 2, bgCol);
}

void renderCombat(void) {
    /* ── Layout ─────────────────────────────────────────────────── */
    const int CARD_W   = 140;
    const int CARD_H   = 108;
    const int CARD_GAP = 8;
    const int CARD_Y   = gfxHeight - CARD_H - 16;
    const int CARD_X0  = (gfxWidth - (4 * CARD_W + 3 * CARD_GAP)) / 2;

    const int LP_X = 16,  LP_Y = 16;
    const int LP_W = 220, LP_H = CARD_Y - LP_Y - 8;

    const int IMG_SZ = 256;
    const int MR_X   = LP_X + LP_W + 16;
    const int MR_W   = gfxWidth - MR_X - 16;
    const int IMG_X  = MR_X + (MR_W - IMG_SZ) / 2;
    const int IMG_Y  = LP_Y  + (LP_H - IMG_SZ) / 2;

    /* ── Background ─────────────────────────────────────────────── */
    fillRect(0, 0, gfxWidth, gfxHeight, rgb(10, 8, 20));

    /* ── Left panel ─────────────────────────────────────────────── */
    fillRect(LP_X, LP_Y, LP_W, LP_H, rgb(14, 8, 8));
    fillRect(LP_X,              LP_Y,              LP_W, 1,      rgb(90, 40, 40));
    fillRect(LP_X,              LP_Y + LP_H - 1,   LP_W, 1,      rgb(90, 40, 40));
    fillRect(LP_X,              LP_Y,              1,    LP_H,   rgb(90, 40, 40));
    fillRect(LP_X + LP_W - 1,  LP_Y,              1,    LP_H,   rgb(90, 40, 40));

    /* ── Monster image ──────────────────────────────────────────── */
    if (combat.enemyImg.data) {
        int iw    = combat.enemyImg.data[0];
        int ih    = combat.enemyImg.data[1];
        int scale = IMG_SZ / (iw > ih ? iw : ih);
        if (scale < 1) scale = 1;
        int dx    = IMG_X + (IMG_SZ - iw * scale) / 2;
        int dy    = IMG_Y + (IMG_SZ - ih * scale) / 2;
        drawBin(dx, dy, combat.enemyImg.data, scale, 0, 255);
    }

    char buf[48];
    const int bx   = LP_X + 10;
    const int barW = LP_W - 20;
    int y = LP_Y + 12;

    /* ── Victory screen ─────────────────────────────────────────── */
    if (combat.phase == COMBAT_PHASE_VICTORY) {
        drawText(bx, y, "VICTORY!", rgb(255, 220, 50), 2);  y += 28;

        for (int i = 0; i < 4; i++) {
            if (combat.gainedDomainXp[i] == 0) continue;
            snprintf(buf, sizeof(buf), "+%d %s xp",
                     combat.gainedDomainXp[i], domainName(i));
            drawText(bx, y, buf, rgb(140, 200, 255), 2);    y += 20;
        }

        if (combat.gainedGold > 0) {
            snprintf(buf, sizeof(buf), "+%d Solmark%s",
                     combat.gainedGold, combat.gainedGold == 1 ? "" : "s");
            drawText(bx, y, buf, rgb(255, 215, 0), 2);      y += 20;
        }
        for (int i = 0; i < combat.droppedCount; i++) {
            snprintf(buf, sizeof(buf), "Found: %s", itemName(combat.droppedItems[i]));
            drawText(bx, y, buf, rgb(140, 255, 200), 1);    y += 14;
        }
        drawText(bx, LP_Y + LP_H - 20, "Enter to continue", rgb(100, 90, 80), 1);
        return;
    }

    /* ── Enemy section ──────────────────────────────────────────── */
    drawText(bx, y, "ENEMY", rgb(100, 50, 50), 1);  y += 12;
    drawText(bx, y, combat.enemy.name, rgb(220, 90, 90), 2);  y += 22;

    int eHpFill = (combat.enemy.maxHp > 0)
                ? combat.enemy.hp * barW / combat.enemy.maxHp : 0;
    if (eHpFill < 0) eHpFill = 0;
    fillRect(bx, y, barW, 10, rgb(40, 12, 12));
    if (eHpFill > 0) fillRect(bx, y, eHpFill, 10, rgb(200, 50, 50));
    y += 12;
    snprintf(buf, sizeof(buf), "HP  %d / %d", combat.enemy.hp, combat.enemy.maxHp);
    drawText(bx, y, buf, rgb(150, 70, 70), 1);  y += 18;

    /* ── Divider ────────────────────────────────────────────────── */
    fillRect(LP_X + 8, y + 4, LP_W - 16, 1, rgb(55, 35, 35));  y += 14;

    /* ── Player section ─────────────────────────────────────────── */
    drawText(bx, y, "PLAYER", rgb(50, 110, 50), 1);  y += 12;

    int pMax    = getMaxHp() > 0 ? getMaxHp() : 1;
    int pFill   = player.hp * barW / pMax;
    int hpPct   = player.hp * 100 / pMax;
    uint32_t hpCol = hpPct > 50 ? rgb(50, 200, 50)
                   : hpPct > 25 ? rgb(200, 200, 50)
                   :              rgb(200, 50, 50);
    fillRect(bx, y, barW, 10, rgb(12, 30, 12));
    if (pFill > 0) fillRect(bx, y, pFill, 10, hpCol);
    y += 12;
    snprintf(buf, sizeof(buf), "HP  %d / %d", player.hp, getMaxHp());
    drawText(bx, y, buf, rgb(70, 150, 70), 1);  y += 18;

    snprintf(buf, sizeof(buf), "ATK  %d", getAttack());
    drawText(bx, y, buf, rgb(210, 90, 90), 1);
    snprintf(buf, sizeof(buf), "DEF  %d", getDefense());
    drawText(bx + 90, y, buf, rgb(90, 140, 210), 1);  y += 16;

    /* ── Encounter log ──────────────────────────────────────────── */
    if (combat.logCount > 0) {
        fillRect(LP_X + 8, y + 4, LP_W - 16, 1, rgb(45, 30, 30));  y += 12;
        for (int i = 0; i < combat.logCount; i++) {
            int age = combat.logCount - 1 - i;  /* 0 = newest */
            uint32_t lc = age == 0 ? rgb(180, 170, 210)
                        : age == 1 ? rgb(130, 120, 155)
                        : age <= 3 ? rgb(85,  78,  108)
                        :            rgb(52,  48,  70);
            drawText(bx, y, combat.log[i], lc, 1);
            y += 12;
        }
    }

    /* ── Action cards ───────────────────────────────────────────── */
    for (int i = 0; i < combat.actionCount; i++) {
        int cx  = CARD_X0 + i * (CARD_W + CARD_GAP);
        int sel = (i == combat.selectedIndex);

        uint32_t bgCol = sel ? rgb(30, 24, 54)  : rgb(14, 12, 28);
        uint32_t bdCol = sel ? rgb(220, 200, 50) : rgb(55, 50, 85);

        drawCard(cx, CARD_Y, CARD_W, CARD_H, bgCol, bdCol);

        uint8_t          aid  = (uint8_t)combat.actions[i].type;
        const ActionDef *adef = getActionDef(aid);
        if (!adef) continue;

        /* Lazy-load action sprite */
        if (!actionImgs[aid].data && adef->imgName[0]) {
            char path[32];
            snprintf(path, sizeof(path), "assets/sprites/%s.bin", adef->imgName);
            actionImgs[aid] = pakRead(path);
        }

        /* Reserve top portion of card for image, bottom for text */
        const int IMG_AREA = 68;
        const int TXT_AREA = CARD_H - IMG_AREA;

        if (actionImgs[aid].data) {
            int iw    = actionImgs[aid].data[0];
            int ih    = actionImgs[aid].data[1];
            int iscale = IMG_AREA / (iw > ih ? iw : ih);
            if (iscale < 1) iscale = 1;
            int ix = cx + (CARD_W - iw * iscale) / 2;
            int iy = CARD_Y + (IMG_AREA - ih * iscale) / 2;
            drawBin(ix, iy, actionImgs[aid].data, iscale, 0, 255);
        }

        const char *name  = adef->name;
        int         nlen  = (int)strlen(name);
        int         scale = (nlen * 16 <= CARD_W - 16) ? 2 : 1;
        int         textW = nlen * 8 * scale;
        int         tx    = cx + (CARD_W - textW) / 2;
        int         ty    = CARD_Y + IMG_AREA + (TXT_AREA - 8 * scale) / 2;
        uint32_t    tCol  = sel ? rgb(255, 240, 80) : rgb(150, 145, 190);
        drawText(tx, ty, name, tCol, scale);

        if (sel)
            fillRect(cx + CARD_W/2 - 2, CARD_Y + CARD_H - 8, 4, 4, rgb(220, 200, 50));
    }
}
