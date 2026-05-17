#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "encounter.h"
#include "actions.h"
#include "domains.h"
#include "log_messages.h"
#include "game.h"
#include "player.h"
#include "items.h"
#include "gfx.h"
#include "quests.h"
#include "loot.h"
#include "world_enemies.h"
#include "npcs.h"

EncounterState encounter;

/* -----------------------------------------------------------------------
 * Social encounter implementation
 * ----------------------------------------------------------------------- */

SocialEncounterDef seDefs[SE_DEF_MAX];
int                seDefCount = 0;

int loadSocialEncounters(PakData data) {
    if (!data.data || data.size < 1) return 0;
    const uint8_t *d = (const uint8_t *)data.data;
    uint8_t count = d[0];
    if (count > SE_DEF_MAX) count = SE_DEF_MAX;
    uint32_t expected = 1 + (uint32_t)count * sizeof(SocialEncounterDef);
    if (data.size < expected) return 0;
    memcpy(seDefs, d + 1, (size_t)count * sizeof(SocialEncounterDef));
    seDefCount = count;
    return seDefCount;
}

void socialNewGame(void) {
    for (int i = 0; i < NPC_DEF_MAX; i++)
        player.npcStates[i].standing = (i < npcDefCount) ? npcDefs[i].base_standing : 50;
    for (int i = 0; i < SE_DEF_MAX; i++)
        player.seStates[i].state = SE_STATE_NEVER_MET;
}

int socialFindActive(uint8_t npc_id) {
    for (int i = 0; i < seDefCount; i++) {
        if (seDefs[i].npc_id != npc_id)                 continue;
        if (seDefs[i].flags & SE_FLAG_HIDDEN)            continue;
        if (player.seStates[i].state == SE_STATE_LOCKED) continue;
        return i;
    }
    return -1;
}

void socialEncounterStart(int se_idx) {
    if (se_idx < 0 || se_idx >= seDefCount) return;
    const SocialEncounterDef *se = &seDefs[se_idx];
    if (se->npc_id >= (uint8_t)npcDefCount) return;
    const NpcDef *npc = &npcDefs[se->npc_id];

    EnemyDef tmp;
    memset(&tmp, 0, sizeof(tmp));
    strncpy(tmp.name, npc->name, sizeof(tmp.name) - 1);
    tmp.hp           = npc->base_standing;
    tmp.defense      = npc->resistance;
    tmp.attack       = npc->social_power;
    tmp.intelligence = npc->patience;
    tmp.lootTableId  = 0xFF;
    tmp.imgName[0]   = npc->imgName[0];
    tmp.imgName[1]   = npc->imgName[1];

    encounterStart(ENCOUNTER_SOCIAL, &tmp, 0);
    player.seStates[se_idx].state = SE_STATE_PARTIAL;
}


/* Lazily-loaded sprites for action cards — indexed by action id, loaded on first render */
static PakData actionImgs[ACTION_MAX];

static void logPush(const char *msg) {
    if (encounter.logCount < 128) {
        strncpy(encounter.log[encounter.logCount], msg, sizeof(encounter.log[0]) - 1);
        encounter.log[encounter.logCount][sizeof(encounter.log[0]) - 1] = '\0';
        encounter.logCount++;
    }
    encounter.logScroll = 0;
}

void encounterLog(const char *msg) { logPush(msg); }

static int checkContext(const ActionDef *def) {
    const Enemy *tgt = &encounter.enemies[encounter.targetIndex];
    uint8_t ctx = def->contextFlags;
    if ((ctx & ACT_CTX_FIRST_TURN)   && !encounter.isFirstTurn)               return 0;
    if ((ctx & ACT_CTX_ENEMY_WEAPON) && !(tgt->flags & ENEMY_HAS_WEAPON))  return 0;
    if ((ctx & ACT_CTX_CAN_STUN)     && !(tgt->flags & ENEMY_STUNNABLE))   return 0;
    if ((ctx & ACT_CTX_PLAYER_HURT)  && (int)player.hp >= getMaxHp() / 2)  return 0;
    if ((ctx & ACT_CTX_REQUIRES_DARK) && !(encounter.modifiers & ENCOUNTER_MOD_DARK))       return 0;
    if ((ctx & ACT_CTX_BLOCKED_HOLY)  && (encounter.modifiers & ENCOUNTER_MOD_HOLY_GROUND)) return 0;
    if (ctx & ACT_CTX_EXECUTABLE) {
        if (!(tgt->flags & ENEMY_EXECUTABLE))  return 0;
        if (tgt->hp > tgt->maxHp / 3)          return 0;
    }
    return 1;
}

static int computeWeight(const ActionDef *def) {
    const Enemy *tgt = &encounter.enemies[encounter.targetIndex];
    int w = def->baseWeight;
    switch (def->id) {
        case ACTION_STRONG:
            w += tgt->size  * 5;
            w -= tgt->speed * 3;
            break;
        case ACTION_DISARM:
            w += tgt->speed * 3;
            break;
        case ACTION_BACKSTAB:
        case ACTION_HIDE:
            w -= tgt->perception * 4;
            break;
        case ACTION_CALM:
            w += tgt->intelligence * 5;
            break;
        default:
            break;
    }

    /* Encounter modifier adjustments */
    if (encounter.modifiers & ENCOUNTER_MOD_DARK) {
        if (def->id == ACTION_BACKSTAB) w += 15;
        if (def->id == ACTION_HIDE)     w += 10;
    }
    if (encounter.modifiers & ENCOUNTER_MOD_HOLY_GROUND) {
        if (def->id == ACTION_HEAL)     w -= 20;
    }
    if (encounter.modifiers & ENCOUNTER_MOD_CROWDED) {
        if (def->id == ACTION_CALM)     w += 15;
        if (def->id == ACTION_HIDE)     w += 10;
    }
    if (encounter.modifiers & ENCOUNTER_MOD_BURNING) {
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
    int poolSize = buildActionPool(pool, (uint8_t)encounter.encounterType);

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

    encounter.actionCount = 0;
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
                encounter.actions[encounter.actionCount++] = (Action){ (ActionId)chosen->id, chosen->power };
                candidates[i] = candidates[--candCount];
                break;
            }
        }
    }
    encounter.selectedIndex = 0;
}

static void loadEnemyImg(int slot, const char *imgName) {
    if (encounter.enemyImgs[slot].data) {
        free(encounter.enemyImgs[slot].data);
        encounter.enemyImgs[slot].data = NULL;
    }
    if (imgName[0]) {
        char path[32];
        snprintf(path, sizeof(path), "assets/sprites/%s.bin", imgName);
        encounter.enemyImgs[slot] = pakRead(path);
    }
}

static void fillEnemySlot(int slot, const EnemyDef *def) {
    Enemy *e = &encounter.enemies[slot];
    memcpy(e->name, def->name, 16);
    e->hp           = def->hp;
    e->maxHp        = def->hp;
    e->attack       = def->attack;
    e->defense      = def->defense;
    e->size         = def->size;
    e->speed        = def->speed;
    e->intelligence = def->intelligence;
    e->perception   = def->perception;
    e->flags        = def->flags;
    e->xpReward     = def->xpReward;
    e->goldDrop     = def->goldDrop;
    e->lootTableId  = def->lootTableId;
    int idx = (int)(def - enemyDefs);
    encounter.enemyDefIds[slot]    = (idx >= 0 && idx < enemyDefCount) ? (uint8_t)idx : 0xFF;
    encounter.fromWorldEnemy[slot] = 0;
    encounter.worldEnemyX[slot]    = 0;
    encounter.worldEnemyY[slot]    = 0;
    loadEnemyImg(slot, def->imgName);
}

void encounterStartCombat(const EnemyDef *def) {
    /* Free all enemy images and clear stale slot data */
    for (int i = 0; i < ENCOUNTER_MAX_ENEMIES; i++) {
        if (encounter.enemyImgs[i].data) { free(encounter.enemyImgs[i].data); encounter.enemyImgs[i].data = NULL; }
        encounter.enemies[i].hp           = 0;
        encounter.enemies[i].maxHp        = 0;
        encounter.fromWorldEnemy[i]       = 0;
        encounter.worldEnemyX[i]          = 0;
        encounter.worldEnemyY[i]          = 0;
    }

    fillEnemySlot(0, def);
    encounter.enemyCount      = 1;
    encounter.targetIndex     = 0;
    encounter.isFirstTurn     = 1;
    encounter.skipEnemyAttack = 0;
    encounter.phase           = ENCOUNTER_PHASE_ACTIVE;
    encounter.gainedGold      = 0;
    encounter.droppedCount    = 0;
    encounter.encounterType   = ENCOUNTER_COMBAT;
    encounter.modifiers       = 0;
    encounter.logCount        = 0;
    encounter.logScroll       = 0;
    memset(encounter.gainedDomainXp, 0, sizeof(encounter.gainedDomainXp));
    {
        char opening[28];
        snprintf(opening, 28, "%.12s bars your path.", encounter.enemies[0].name);
        logPush(opening);
    }
    fireLogMessages(LOGTRIG_COMBAT_START, 0xFF, 0);
    generateActions();
    state = STATE_COMBAT;
}

void encounterAddEnemy(const EnemyDef *def, uint8_t wx, uint8_t wy) {
    if (encounter.enemyCount >= ENCOUNTER_MAX_ENEMIES) return;
    int slot = encounter.enemyCount++;
    fillEnemySlot(slot, def);
    encounter.fromWorldEnemy[slot] = 1;
    encounter.worldEnemyX[slot]    = wx;
    encounter.worldEnemyY[slot]    = wy;
    char msg[28];
    snprintf(msg, 28, "%.12s joins the fight!", def->name);
    logPush(msg);
}

void encounterStart(EncounterType type, const EnemyDef *def, uint32_t mods) {
    encounterStartCombat(def);
    encounter.encounterType = type;
    encounter.modifiers     = mods;
    encounter.logCount      = 0;
    const char *opener;
    switch (type) {
        case ENCOUNTER_SOCIAL:        opener = "A tense exchange begins.";  break;
        case ENCOUNTER_INVESTIGATION: opener = "Something demands scrutiny."; break;
        case ENCOUNTER_HUNT:          opener = "You are on the hunt.";      break;
        case ENCOUNTER_ENVIRONMENTAL: opener = "The environment closes in."; break;
        default:                      opener = NULL;                         break;
    }
    if (opener) logPush(opener);
    fireLogMessages(LOGTRIG_COMBAT_START, 0xFF, 0);
    generateActions();
}

/* Kill one enemy: roll loot/gold, fire quest hook, log the fall. */
static void killEnemy(int slot) {
    Enemy *e = &encounter.enemies[slot];
    if (e->goldDrop > 0) {
        int g = rand() % e->goldDrop + 1;
        player.gold       += (uint16_t)g;
        encounter.gainedGold += g;
    }
    rollLoot(e->lootTableId, encounter.droppedItems, &encounter.droppedCount);
    questOnEnemyKilled(encounter.enemyDefIds[slot]);
    char vm[28]; snprintf(vm, 28, "%.20s falls.", e->name); logPush(vm);
    fireLogMessages(LOGTRIG_ENEMY_KILLED, 0xFF, slot);
}

/* Advance targetIndex to the next alive enemy (wrapping). */
static void advanceTarget(void) {
    for (int i = 1; i <= encounter.enemyCount; i++) {
        int next = (encounter.targetIndex + i) % encounter.enemyCount;
        if (encounter.enemies[next].hp > 0) { encounter.targetIndex = next; return; }
    }
}

static void performPlayerAction(void) {
    /* A log effect from the previous turn may have killed the current target */
    if (encounter.enemies[encounter.targetIndex].maxHp == 0)
        advanceTarget();

    Action *a   = &encounter.actions[encounter.selectedIndex];
    Enemy  *tgt = &encounter.enemies[encounter.targetIndex];
    encounter.skipEnemyAttack = 0;
    int ehpBefore = tgt->hp;
    int phpBefore = (int)player.hp;

    switch (a->type) {
        case ACTION_ATTACK:
            tgt->hp -= getAttack();
            break;

        case ACTION_STRONG:
            tgt->hp -= getAttack() + a->power;
            break;

        case ACTION_HEAL: {
            int newHp = (int)player.hp + a->power;
            int cap   = getMaxHp();
            player.hp = (uint16_t)(newHp > cap ? cap : newHp);
            break;
        }

        case ACTION_DEFEND:
            break;

        case ACTION_DISARM:
            tgt->flags  &= ~ENEMY_HAS_WEAPON;
            tgt->attack  = tgt->attack > 1 ? tgt->attack / 2 : 1;
            break;

        case ACTION_BACKSTAB:
            tgt->hp -= getAttack() + a->power;
            encounter.skipEnemyAttack = 1;
            break;

        case ACTION_STUN:
            encounter.skipEnemyAttack = 1;
            tgt->flags &= ~ENEMY_STUNNABLE;
            break;

        case ACTION_CALM:
            tgt->attack       = tgt->attack > 1 ? tgt->attack / 2 : 1;
            tgt->intelligence = 0;
            break;

        case ACTION_HIDE:
            encounter.skipEnemyAttack = (player.agility > tgt->perception);
            break;

        case ACTION_EXECUTE:
            tgt->hp -= getAttack() * 2 + a->power;
            break;

        /* ── Domain: Combat ─────────────────────────────────────── */
        case ACTION_GUARDED_STANCE:
            /* Small preemptive hit; incoming damage is halved (see enemy attack phase) */
            tgt->hp -= a->power;
            break;

        case ACTION_SHIELD_BREAK:
            /* Crush defenses open, deal bonus damage */
            tgt->defense = 0;
            tgt->hp -= a->power;
            break;

        case ACTION_RIPOSTE:
            /* Swift counter-strike; skip enemy retaliation */
            tgt->hp -= getAttack() + a->power;
            encounter.skipEnemyAttack = 1;
            break;

        /* ── Domain: Trickery ───────────────────────────────────── */
        case ACTION_POCKET_SAND:
            /* Blind: strip perception, skip counter */
            tgt->perception = 0;
            encounter.skipEnemyAttack = 1;
            break;

        case ACTION_FAKE_WEAKNESS:
            /* Bait the enemy into overextending, then punish */
            tgt->hp -= getAttack() + a->power;
            encounter.skipEnemyAttack = 1;
            break;

        /* ── Domain: Blood ──────────────────────────────────────── */
        case ACTION_CRIMSON_RUSH:
            /* Wounded fury: strike twice (simulated as double hit) */
            tgt->hp -= getAttack() + a->power;
            encounter.skipEnemyAttack = 1;
            break;

        case ACTION_BLOOD_MIST: {
            /* Dissolve into fog: skip all counters, blind every enemy */
            encounter.skipEnemyAttack = 1;
            for (int i = 0; i < encounter.enemyCount; i++)
                if (encounter.enemies[i].perception > 1) encounter.enemies[i].perception--;
            break;
        }

        case ACTION_HEMORRHAGE:
            /* Exploit fresh wounds on a weakened target */
            tgt->hp -= getAttack() + a->power;
            break;

        case ACTION_FEAST_FEAR: {
            /* Drain a terrified (stunnable) enemy; restore HP */
            int dmg = getAttack() + a->power;
            tgt->hp -= dmg;
            int newHp = (int)player.hp + a->power;
            int cap   = getMaxHp();
            player.hp = (uint16_t)(newHp > cap ? cap : newHp);
            tgt->flags &= ~ENEMY_STUNNABLE;
            encounter.skipEnemyAttack = 1;
            break;
        }

        case ACTION_BAT_SWARM: {
            /* AoE darkness: chip all enemies, disrupt counters */
            int totalDealt = 0;
            for (int i = 0; i < encounter.enemyCount; i++) {
                if (encounter.enemies[i].hp > 0 && encounter.enemies[i].maxHp > 0) {
                    int d = a->power < encounter.enemies[i].hp ? a->power : encounter.enemies[i].hp;
                    encounter.enemies[i].hp -= a->power;
                    totalDealt += d;
                }
            }
            char aoeMsg[28];
            snprintf(aoeMsg, sizeof(aoeMsg), "Bat Swarm: %d dmg.", totalDealt);
            logPush(aoeMsg);
            ehpBefore = tgt->hp;
            encounter.skipEnemyAttack = 1;
            break;
        }

        /* ── Domain: Charm ──────────────────────────────────────── */
        case ACTION_NOBLE_PRESENCE:
            /* First-turn aura: halve every enemy's attack, skip counters */
            encounter.skipEnemyAttack = 1;
            for (int i = 0; i < encounter.enemyCount; i++)
                encounter.enemies[i].attack = encounter.enemies[i].attack > 1
                    ? encounter.enemies[i].attack / 2 : 1;
            break;

        case ACTION_PROVE_SKILL:
            /* Humiliate an armed foe: deal power damage, halve their attack */
            tgt->hp -= a->power;
            tgt->attack = tgt->attack > 1 ? tgt->attack / 2 : 1;
            encounter.skipEnemyAttack = 1;
            break;

        case ACTION_DUELIST_MARK:
            /* Challenge: open with a strong blow and strip all defense */
            tgt->hp -= getAttack() + a->power;
            tgt->defense = 0;
            break;

        default:
            break;
    }

    /* Log action result (use target HP delta for single-target; for AoE log separately) */
    {
        int dealt  = ehpBefore - tgt->hp;
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

    fireLogMessages(LOGTRIG_ACTION, (uint8_t)a->type, encounter.targetIndex);

    /* Domain XP for the action used */
    {
        uint8_t dom = actionGetDomain((uint8_t)a->type);
        if (dom != 0xFF) {
            domainAwardXp(dom, 1);
            if (encounter.gainedDomainXp[dom] < 255)
                encounter.gainedDomainXp[dom]++;
        }
    }

    /* Non-encounter encounters: any action resolves the scene */
    if (encounter.encounterType != ENCOUNTER_COMBAT && encounter.phase == ENCOUNTER_PHASE_ACTIVE) {
        encounter.phase = ENCOUNTER_PHASE_VICTORY;
        encounter.skipEnemyAttack = 1;
    }

    /* Kill any enemies that reached 0 HP; check for full victory */
    if (encounter.phase == ENCOUNTER_PHASE_ACTIVE) {
        int allDead = 1;
        for (int i = 0; i < encounter.enemyCount; i++) {
            if (encounter.enemies[i].hp <= 0 && encounter.enemies[i].maxHp > 0) {
                encounter.enemies[i].hp = 0;
                killEnemy(i);
                encounter.enemies[i].maxHp = 0; /* marks slot as processed */
            }
            if (encounter.enemies[i].maxHp > 0) allDead = 0;
        }
        if (allDead) {
            encounter.phase = ENCOUNTER_PHASE_VICTORY;
            encounter.skipEnemyAttack = 1;
        } else if (encounter.enemies[encounter.targetIndex].maxHp == 0) {
            advanceTarget();
        }
    }

    if (player.hp == 0) {
        logPush("You fall unconscious.");
        enterDeath();
        return;
    }

    /* Each surviving enemy counter-attacks */
    if (!encounter.skipEnemyAttack && encounter.phase == ENCOUNTER_PHASE_ACTIVE) {
        for (int i = 0; i < encounter.enemyCount; i++) {
            if (encounter.enemies[i].maxHp == 0) continue; /* dead */
            int dmg = encounter.enemies[i].attack;
            if (a->type == ACTION_DEFEND || a->type == ACTION_GUARDED_STANCE) dmg = dmg / 2 + 1;
            player.hp = (dmg >= (int)player.hp) ? 0 : (uint16_t)(player.hp - dmg);
            char cm[28];
            snprintf(cm, 28, "%.10s: %d dmg.", encounter.enemies[i].name, dmg);
            logPush(cm);
            if (player.hp == 0) break;
        }
    }

    if (player.hp == 0) {
        logPush("You fall unconscious.");
        enterDeath();
        return;
    }

    if (encounter.phase == ENCOUNTER_PHASE_ACTIVE) {
        int phpAfter = (int)player.hp;
        if (phpAfter < phpBefore)
            fireLogMessages(LOGTRIG_PLAYER_HURT, 0xFF, -1);
        if (phpAfter < getMaxHp() / 3)
            fireLogMessages(LOGTRIG_PLAYER_LOW_HP, 0xFF, -1);
        fireLogMessages(LOGTRIG_TURN_END, 0xFF, -1);
    }

    encounter.isFirstTurn = 0;
    generateActions();
}

void handleEncounterInput(int key) {
    if (encounter.phase == ENCOUNTER_PHASE_VICTORY) {
        if (key == VK_RETURN || key == VK_ESCAPE) {
            for (int i = 0; i < encounter.enemyCount; i++)
                if (encounter.fromWorldEnemy[i])
                    worldEnemyRemoveAt(encounter.worldEnemyX[i], encounter.worldEnemyY[i]);
            state = STATE_WORLD;
        }
        return;
    }
    switch (key) {
        case VK_LEFT:
            if (encounter.actionCount > 0)
                encounter.selectedIndex = (encounter.selectedIndex + encounter.actionCount - 1) % encounter.actionCount;
            break;
        case VK_RIGHT:
            if (encounter.actionCount > 0)
                encounter.selectedIndex = (encounter.selectedIndex + 1) % encounter.actionCount;
            break;
        case VK_UP: {
            int maxScroll = encounter.logCount > 8 ? encounter.logCount - 8 : 0;
            if (encounter.logScroll < maxScroll) encounter.logScroll++;
            break;
        }
        case VK_DOWN:
            if (encounter.logScroll > 0) encounter.logScroll--;
            break;
        case VK_TAB:
            advanceTarget();
            generateActions();
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

void renderEncounter(void) {
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

    /* ── Encounter-type colours ────────────────────────────────────── */
    uint32_t bgMain, bgPanel, bdPanel;
    switch (encounter.encounterType) {
        case ENCOUNTER_SOCIAL:
            bgMain = rgb(3,  4,  14); bgPanel = rgb(6,  8,  22); bdPanel = rgb(35, 45, 120); break;
        case ENCOUNTER_INVESTIGATION:
            bgMain = rgb(10, 9,  2);  bgPanel = rgb(16, 14, 3);  bdPanel = rgb(100,88, 18);  break;
        case ENCOUNTER_HUNT:
            bgMain = rgb(3,  3,  3);  bgPanel = rgb(7,  7,  7);  bdPanel = rgb(38, 38, 38);  break;
        case ENCOUNTER_ENVIRONMENTAL:
            bgMain = rgb(3,  10, 3);  bgPanel = rgb(5,  15, 5);  bdPanel = rgb(28, 90, 28);  break;
        default: /* ENCOUNTER_COMBAT */
            bgMain = rgb(10, 4,  4);  bgPanel = rgb(14, 8,  8);  bdPanel = rgb(90, 40, 40);  break;
    }

    /* ── Background ─────────────────────────────────────────────── */
    fillRect(0, 0, gfxWidth, gfxHeight, bgMain);

    /* ── Left panel ─────────────────────────────────────────────── */
    fillRect(LP_X, LP_Y, LP_W, LP_H, bgPanel);
    fillRect(LP_X,              LP_Y,              LP_W, 1,      bdPanel);
    fillRect(LP_X,              LP_Y + LP_H - 1,   LP_W, 1,      bdPanel);
    fillRect(LP_X,              LP_Y,              1,    LP_H,   bdPanel);
    fillRect(LP_X + LP_W - 1,  LP_Y,              1,    LP_H,   bdPanel);

    /* ── Monster image (targeted enemy) ────────────────────────── */
    {
        const PakData *img = &encounter.enemyImgs[encounter.targetIndex];
        if (img->data) {
            int iw    = img->data[0];
            int ih    = img->data[1];
            int scale = IMG_SZ / (iw > ih ? iw : ih);
            if (scale < 1) scale = 1;
            int dx    = IMG_X + (IMG_SZ - iw * scale) / 2;
            int dy    = IMG_Y + (IMG_SZ - ih * scale) / 2;
            drawBin(dx, dy, img->data, scale, 0, 255);
        }
    }

    char buf[48];
    const int bx   = LP_X + 10;
    const int barW = LP_W - 20;
    int y = LP_Y + 12;

    /* ── Victory screen ─────────────────────────────────────────── */
    if (encounter.phase == ENCOUNTER_PHASE_VICTORY) {
        drawText(bx, y, "VICTORY!", rgb(255, 220, 50), 2);  y += 28;

        for (int i = 0; i < 4; i++) {
            if (encounter.gainedDomainXp[i] == 0) continue;
            snprintf(buf, sizeof(buf), "+%d %s xp",
                     encounter.gainedDomainXp[i], domainName(i));
            drawText(bx, y, buf, rgb(140, 200, 255), 2);    y += 20;
        }

        if (encounter.gainedGold > 0) {
            snprintf(buf, sizeof(buf), "+%d Solmark%s",
                     encounter.gainedGold, encounter.gainedGold == 1 ? "" : "s");
            drawText(bx, y, buf, rgb(255, 215, 0), 2);      y += 20;
        }
        for (int i = 0; i < encounter.droppedCount; i++) {
            snprintf(buf, sizeof(buf), "Found: %s", itemName(encounter.droppedItems[i]));
            drawText(bx, y, buf, rgb(140, 255, 200), 1);    y += 14;
        }
        drawText(bx, LP_Y + LP_H - 20, "Enter to continue", rgb(100, 90, 80), 1);
        return;
    }

    /* ── Enemy section (all enemies) ───────────────────────────── */
    drawText(bx, y, "ENEMIES", rgb(100, 50, 50), 1);  y += 12;
    for (int ei = 0; ei < encounter.enemyCount; ei++) {
        const Enemy *e  = &encounter.enemies[ei];
        int          sel = (ei == encounter.targetIndex);
        int          dead = (e->maxHp == 0);
        uint32_t     nameCol = dead   ? rgb(60, 50, 50)
                             : sel    ? rgb(255, 210, 80)
                             :          rgb(200, 90, 90);
        /* target indicator + name */
        char nbuf[20];
        nbuf[0] = sel ? '>' : ' '; nbuf[1] = ' ';
        int nl = 0; while (e->name[nl] && nl < 16) nl++;
        for (int c = 0; c < nl && c < 17; c++) nbuf[2 + c] = e->name[c];
        nbuf[2 + (nl < 17 ? nl : 17)] = '\0';
        drawText(bx, y, nbuf, nameCol, 1);  y += 11;
        /* HP bar */
        if (!dead && e->maxHp > 0) {
            int fill = e->hp * barW / e->maxHp;
            if (fill < 0) fill = 0;
            fillRect(bx, y, barW, 6, rgb(40, 12, 12));
            if (fill > 0) fillRect(bx, y, fill, 6, sel ? rgb(220, 160, 40) : rgb(180, 50, 50));
        } else {
            fillRect(bx, y, barW, 6, rgb(28, 18, 18));
        }
        y += 10;
    }

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
    if (encounter.logCount > 0) {
        fillRect(LP_X + 8, y + 4, LP_W - 16, 1, rgb(45, 30, 30));  y += 12;

        int end   = encounter.logCount - encounter.logScroll;
        if (end < 0) end = 0;
        int start = end - 8;
        if (start < 0) start = 0;

        if (start > 0) {
            char more[20];
            snprintf(more, sizeof(more), "^ %d more", start);
            drawText(bx, y, more, rgb(45, 42, 60), 1);  y += 12;
        }
        for (int i = start; i < end; i++) {
            int age = encounter.logCount - 1 - i;
            uint32_t lc = age == 0 ? rgb(180, 170, 210)
                        : age == 1 ? rgb(130, 120, 155)
                        : age <= 3 ? rgb(85,  78,  108)
                        :            rgb(52,  48,  70);
            drawText(bx, y, encounter.log[i], lc, 1);  y += 12;
        }
    }

    /* ── Action cards ───────────────────────────────────────────── */
    for (int i = 0; i < encounter.actionCount; i++) {
        int cx  = CARD_X0 + i * (CARD_W + CARD_GAP);
        int sel = (i == encounter.selectedIndex);

        uint32_t bgCol = sel ? rgb(30, 24, 54)  : rgb(14, 12, 28);
        uint32_t bdCol = sel ? rgb(220, 200, 50) : rgb(55, 50, 85);

        drawCard(cx, CARD_Y, CARD_W, CARD_H, bgCol, bdCol);

        uint8_t          aid  = (uint8_t)encounter.actions[i].type;
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

    if (encounter.enemyCount > 1) {
        drawText(CARD_X0, CARD_Y + CARD_H + 4,
                 "TAB: switch target", rgb(55, 55, 80), 1);
    }
}
