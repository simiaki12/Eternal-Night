#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "encounter.h"
#include "actions.h"
#include "domains.h"
#include "log_messages.h"
#include "enc_graph.h"
#include "game.h"
#include "player.h"
#include "items.h"
#include "gfx.h"
#include "quests.h"
#include "loot.h"
#include "world_enemies.h"
#include "npcs.h"
#include "investigations.h"
#include "env_encounter.h"
#include "hunt_encounter.h"
#include "statuses.h"
#include "cases.h"

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
    tmp.intelligence = npc->patience;
    tmp.lootTableId  = 0xFF;
    tmp.imgName[0]   = npc->imgName[0];
    tmp.imgName[1]   = npc->imgName[1];

    /* Map the social profile onto the graph engine:
       personality tags carve states out of the mask, social power is the
       per-turn pushback on willingness, resistance scales progress. */
    tmp.stateMask = (1u << SSTATE_STRANGER)   | (1u << SSTATE_SUSPICIOUS)
                  | (1u << SSTATE_FEARFUL)    | (1u << SSTATE_TRUSTING)
                  | (1u << SSTATE_HOSTILE)    | (1u << SSTATE_GREEDY);
    if (npc->tags & NPC_TAG_FEARLESS) tmp.stateMask &= ~(1u << SSTATE_FEARFUL);
    if (npc->tags & NPC_TAG_NOBLE)    tmp.stateMask &= ~(1u << SSTATE_GREEDY);
    tmp.damage   = npc->social_power;
    tmp.tenacity = npc->resistance >= 90 ? 10 : (uint8_t)(100 - npc->resistance);

    encounterStart(ENCOUNTER_SOCIAL, &tmp, 0);
    encounter.socialNpcId      = (int)se->npc_id;
    encounter.enemies[0].hp    = (int)npc->base_standing; /* willingness */
    encounter.enemyState[0]    = (se->flags & SE_FLAG_DISP_OVERRIDE)
                               ? se->disp_start : SSTATE_STRANGER;
    encounter.socialTurns      = 0;
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
    if (ctx & ACT_CTX_ROUTED) {
        if (encounter.encounterType != ENCOUNTER_HUNT) return 0;
        uint8_t gs = encounter.enemyState[0];
        if (gs != HSTATE_TERRIFIED && gs != HSTATE_BROKEN) return 0;
    }
    if (ctx & ACT_CTX_EXECUTABLE) {
        /* finishers only open up on a staggered foe */
        if (encounter.encounterType != ENCOUNTER_COMBAT)                     return 0;
        if (!(tgt->flags & ENEMY_EXECUTABLE))                                return 0;
        if (encounter.enemyState[encounter.targetIndex] != CSTATE_STAGGERED) return 0;
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

    /* Domain affinity — scales with investment so a committed style feels cohesive */
    uint8_t primary = domainPrimary();
    if (def->domain == primary)
        w += (int)player.domains[primary].level * 3;

    /* Focused domain — explicit player choice always carries a base pull, plus scales */
    if (player.focusedDomain != DOMAIN_NONE && def->domain == player.focusedDomain)
        w += 20 + (int)player.domains[player.focusedDomain].level * 2;

    /* Status effects (Rage etc.) pull toward their domain */
    w += statusWeightBonus(def->domain);

    /* Player-marked preferences */
    if (isActionFavoured(def->id))   w += 60;
    if (isActionSuppressed(def->id)) w  = 1;

    return w < 1 ? 1 : w;
}


void generateActions(void) {
    encounter.actionCount = 0;

    /* Social encounters always have Demand as the first card */
    if (encounter.encounterType == ENCOUNTER_SOCIAL)
        encounter.actions[encounter.actionCount++] = (Action){ ACTION_DEMAND };

    uint8_t pool[ACTION_MAX];
    int poolSize = buildActionPool(pool, (uint8_t)encounter.encounterType);

    typedef struct { uint8_t id; int weight; } Candidate;
    Candidate candidates[ACTION_MAX];
    int candCount = 0;

    for (int i = 0; i < poolSize; i++) {
        if (pool[i] == (uint8_t)ACTION_DEMAND) continue; /* already injected */
        const ActionDef *def = getActionDef(pool[i]);
        if (!def) continue;
        if (!checkContext(def)) continue;
        candidates[candCount].id     = pool[i];
        candidates[candCount].weight = computeWeight(def);
        candCount++;
    }

    int slots = 4 - encounter.actionCount;
    int picks = candCount < slots ? candCount : slots;
    for (int pick = 0; pick < picks; pick++) {
        int total = 0;
        for (int i = 0; i < candCount; i++) total += candidates[i].weight;
        int r = rand() % total;
        int acc = 0;
        for (int i = 0; i < candCount; i++) {
            acc += candidates[i].weight;
            if (r < acc) {
                const ActionDef *chosen = getActionDef(candidates[i].id);
                encounter.actions[encounter.actionCount++] = (Action){ (ActionId)chosen->id };
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
    e->hp           = 0;
    e->alive        = 1;
    e->size         = def->size;
    e->speed        = def->speed;
    e->intelligence = def->intelligence;
    e->perception   = def->perception;
    e->flags        = def->flags;
    e->xpReward     = def->xpReward;
    e->goldDrop     = def->goldDrop;
    e->lootTableId  = def->lootTableId;
    e->stateMask    = def->stateMask;
    e->damage       = def->damage;
    e->tenacity     = def->tenacity;
    e->behaviors[0] = def->behaviors[0];
    e->behaviors[1] = def->behaviors[1];
    encounter.enemyState[slot] = (uint8_t)encGraphStart(ENC_IDX_COMBAT);
    encounter.potCount[slot]   = 0;
    int idx = (int)(def - enemyDefs);
    encounter.enemyDefIds[slot]    = (idx >= 0 && idx < enemyDefCount) ? (uint8_t)idx : 0xFF;
    encounter.fromWorldEnemy[slot] = 0;
    encounter.worldEnemyX[slot]    = 0;
    encounter.worldEnemyY[slot]    = 0;
    loadEnemyImg(slot, def->imgName);
}

/* Reset all combat slot/instance state for a fresh encounter on enemy `def`.
   Does NOT set encounterType, push an opener, fire triggers, or generate the
   action hand — the public entry points below own those so they run exactly
   once regardless of how the encounter was started. */
static void resetCombatState(const EnemyDef *def) {
    /* Free all enemy images and clear stale slot data */
    for (int i = 0; i < ENCOUNTER_MAX_ENEMIES; i++) {
        if (encounter.enemyImgs[i].data) { free(encounter.enemyImgs[i].data); encounter.enemyImgs[i].data = NULL; }
        encounter.enemies[i].hp           = 0;
        encounter.enemies[i].alive        = 0;
        encounter.fromWorldEnemy[i]       = 0;
        encounter.worldEnemyX[i]          = 0;
        encounter.worldEnemyY[i]          = 0;
    }

    fillEnemySlot(0, def);
    encounter.enemyCount      = 1;
    encounter.targetIndex     = 0;
    encounter.isFirstTurn     = 1;
    encounter.phase           = ENCOUNTER_PHASE_ACTIVE;
    encounter.gainedGold      = 0;
    encounter.droppedCount    = 0;
    encounter.modifiers       = 0;
    encounter.logCount        = 0;
    encounter.logScroll       = 0;
    encounter.socialNpcId          = -1;
    encounter.socialOutcome        = SOCIAL_OUTCOME_NONE;
    encounter.socialEndWillingness = 0;
    encounter.progressNextMult     = 10;
    encounter.pendingProgress      = 0;
    encounter.extraActionPending   = 0;
    memset(encounter.gainedDomainXp, 0, sizeof(encounter.gainedDomainXp));
}

void encounterStartCombat(const EnemyDef *def) {
    resetCombatState(def);
    encounter.encounterType = ENCOUNTER_COMBAT;
    {
        char opening[28];
        snprintf(opening, 28, "%.12s bars your path.", encounter.enemies[0].name);
        logPush(opening);
    }
    fireLogMessages(LOGTRIG_COMBAT_START, 0xFF, 0);
    generateActions();
    state = STATE_ENCOUNTER;
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
    resetCombatState(def);
    encounter.encounterType = type;
    encounter.modifiers     = mods;
    if (type == ENCOUNTER_COMBAT) {
        char opening[28];
        snprintf(opening, 28, "%.12s bars your path.", encounter.enemies[0].name);
        logPush(opening);
    } else {
        const char *opener;
        switch (type) {
            case ENCOUNTER_SOCIAL:        opener = "A tense exchange begins.";   break;
            case ENCOUNTER_INVESTIGATION: opener = "Something demands scrutiny."; break;
            case ENCOUNTER_HUNT:          opener = "You are on the hunt.";        break;
            case ENCOUNTER_ENVIRONMENTAL: opener = "The environment closes in.";  break;
            default:                      opener = NULL;                          break;
        }
        if (opener) logPush(opener);
    }
    fireLogMessages(LOGTRIG_COMBAT_START, 0xFF, 0);
    generateActions();
    state = STATE_ENCOUNTER;
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
        if (encounter.enemies[next].alive) { encounter.targetIndex = next; return; }
    }
}

/* -----------------------------------------------------------------------
 * State-graph combat
 * ----------------------------------------------------------------------- */

/* Bank `add` progress on edge from->to of enemy `slot`; returns the pot. */
static int potAdd(int slot, uint8_t from, uint8_t to, int add) {
    uint8_t key = (uint8_t)((from << 4) | to);
    for (int i = 0; i < encounter.potCount[slot]; i++) {
        if (encounter.potKey[slot][i] == key) {
            int v = encounter.potVal[slot][i] + add;
            if (v > 200) v = 200;
            encounter.potVal[slot][i] = (uint8_t)v;
            return v;
        }
    }
    if (encounter.potCount[slot] < COMBAT_POT_MAX) {
        int idx = encounter.potCount[slot]++;
        encounter.potKey[slot][idx] = key;
        encounter.potVal[slot][idx] = (uint8_t)(add > 200 ? 200 : add);
        return encounter.potVal[slot][idx];
    }
    return add; /* table full — roll on raw progress, don't bank */
}

/* Leaving a state wipes every pot banked from it (Simon's reset rule). */
static void potClearFrom(int slot, uint8_t from) {
    for (int i = 0; i < encounter.potCount[slot]; ) {
        if ((encounter.potKey[slot][i] >> 4) == from) {
            encounter.potCount[slot]--;
            encounter.potKey[slot][i] = encounter.potKey[slot][encounter.potCount[slot]];
            encounter.potVal[slot][i] = encounter.potVal[slot][encounter.potCount[slot]];
        } else {
            i++;
        }
    }
}

/* Highest pot banked from the enemy's current state — the UI progress hint. */
static int potMaxFrom(int slot, uint8_t from) {
    int best = 0;
    for (int i = 0; i < encounter.potCount[slot]; i++)
        if ((encounter.potKey[slot][i] >> 4) == from &&
            encounter.potVal[slot][i] > best)
            best = encounter.potVal[slot][i];
    return best > 100 ? 100 : best;
}

/* Shared graph resolution — combat, social and hunt all drive a target
   around their type's graph with identical rules: find the first live
   triplet (matches current state, allowed by stateMask), bank progress
   into that edge's pot, roll d100 under the pot. Whiffs are structural
   (wrong moment or blocked state) and spend the turn.
   `fx` applies effects in the caller's scope (meter/kill semantics). */
GraphResult encGraphResolve(int slot, int typeIdx, const ActionDef *adef,
                            uint8_t stateMask, uint8_t tenacity,
                            void (*fx)(const Effect *)) {
    char lm[28];
    const Transition *tr = NULL;
    int n = adef ? actionTransitionsFor(adef, typeIdx, &tr) : 0;
    uint8_t cur = encounter.enemyState[slot];

    const Transition *live = NULL;
    int base = 0;
    for (int i = 0; i < n; i++) {
        if (cur >= TRANSITION_SOURCES)          continue;
        int p = tr[i].progress[cur];
        if (p == 0)                             continue; /* no route from here */
        if (!(stateMask & (1u << tr[i].to)))    continue; /* target cannot go there */
        live = &tr[i];
        base = p;
        break;
    }

    if (!live) {
        if (n > 0) {
            snprintf(lm, 28, "%.14s: no opening.", adef->name);
            logPush(lm);
            if (fx) fx(&adef->fallback);
            return GRAPH_WHIFF;
        }
        if (adef) { snprintf(lm, 28, "%.26s.", adef->name); logPush(lm); }
        return GRAPH_NO_TRIPLETS;
    }

    int add = base + statusProgressBonus(adef->domain)
            + encounter.pendingProgress;
    encounter.pendingProgress = 0;
    add = add * encounter.progressNextMult / 10;
    encounter.progressNextMult = 10;
    add = add * (tenacity ? tenacity : 100) / 100;
    if (add < 1) add = 1;

    int pot = potAdd(slot, cur, live->to, add);
    if (rand() % 100 >= pot) {
        snprintf(lm, 28, "%.10s: builds %d%%", adef->name, pot > 100 ? 100 : pot);
        logPush(lm);
        return GRAPH_BUILD;
    }

    potClearFrom(slot, cur);
    encounter.enemyState[slot] = live->to;
    const StateDef *ns = encGraphState(typeIdx, live->to);
    if (typeIdx == ENC_IDX_COMBAT)
        snprintf(lm, 28, "%.12s: %.12s!", encounter.enemies[slot].name,
                 ns ? ns->name : "?");
    else
        snprintf(lm, 28, "Now: %.20s.", ns ? ns->name : "?");
    logPush(lm);
    if (ns && fx) fx(&ns->onEnter);
    return GRAPH_FIRED;
}

/* Effects whose meaning belongs to the encounter engine; everything else
   falls through to the generic executor in statuses.c. */
void encounterApplyEffect(const Effect *e) {
    if (!e || e->type == EFX_NONE || e->chance == 0) return;
    switch (e->type) {
        case EFX_PROGRESS:
            if (rand() % 100 < e->chance) encounter.pendingProgress += e->value;
            return;
        case EFX_PROGRESS_NEXT:
            if (rand() % 100 < e->chance)
                encounter.progressNextMult = e->value ? e->value : 10;
            return;
        case EFX_EXTRA_ACTION:
            if (rand() % 100 < e->chance) {
                encounter.extraActionPending = 1;
                encounterLog("You see another opening!");
            }
            return;
        default:
            applyEffect(e);
    }
}

static void applyCombatEffect(const Effect *e) { encounterApplyEffect(e); }

static void performCombatAction(void) {
    /* A log effect may have beaten the current target since last turn */
    if (!encounter.enemies[encounter.targetIndex].alive)
        advanceTarget();

    Action *a = &encounter.actions[encounter.selectedIndex];
    const ActionDef *adef = getActionDef((uint8_t)a->type);
    int    slot = encounter.targetIndex;
    int    phpBefore = (int)player.hp;
    char   lm[28];

    if (adef) applyCombatEffect(&adef->onPlay);

    /* Drive the target around the combat graph; a whiff spends the turn and
       the pressure phase below still lands. ALL_TARGETS actions sweep every
       living enemy, each with its own state and pots — the progress buff is
       restored per enemy so one swing lands equally on the whole line. */
    {
        int aoe   = adef && (adef->actionFlags & ACT_FLAG_ALL_TARGETS);
        int mult  = encounter.progressNextMult;
        int bonus = encounter.pendingProgress;
        for (int i = 0; i < encounter.enemyCount; i++) {
            if (aoe ? !encounter.enemies[i].alive : i != slot) continue;
            if (aoe) {
                encounter.progressNextMult = (uint8_t)mult;
                encounter.pendingProgress  = (uint8_t)bonus;
            }
            Enemy *t = &encounter.enemies[i];
            if (encGraphResolve(i, ENC_IDX_COMBAT, adef, t->stateMask,
                                t->tenacity, applyCombatEffect) != GRAPH_FIRED)
                continue;
            const StateDef *ns = encGraphState(ENC_IDX_COMBAT,
                                               encounter.enemyState[i]);
            if (ns && (ns->flags & GSTATE_TERMINAL)) {
                killEnemy(i);
                t->alive = 0;
            }
        }
    }

    fireLogMessages(LOGTRIG_ACTION, (uint8_t)a->type, slot);

    /* Domain XP for the action used */
    {
        uint8_t dom = actionGetDomain((uint8_t)a->type);
        if (dom != 0xFF) {
            domainAwardXp(dom, 1);
            if (encounter.gainedDomainXp[dom] < 255)
                encounter.gainedDomainXp[dom]++;
        }
    }

    /* Victory: every enemy driven to Broken */
    {
        int allDead = 1;
        for (int i = 0; i < encounter.enemyCount; i++)
            if (encounter.enemies[i].alive) { allDead = 0; break; }
        if (allDead) encounter.phase = ENCOUNTER_PHASE_VICTORY;
        else if (!encounter.enemies[encounter.targetIndex].alive)
            advanceTarget();
    }

    /* Pressure phase: every alive enemy's state flows against Azrael,
       plus its damage stat; state-keyed behaviors fire here too. */
    if (encounter.phase == ENCOUNTER_PHASE_ACTIVE) {
        if (encounter.extraActionPending) {
            encounter.extraActionPending = 0; /* free action — no pressure */
        } else {
            int threat = 0;
            for (int i = 0; i < encounter.enemyCount; i++) {
                Enemy *e = &encounter.enemies[i];
                if (!e->alive) continue;
                const StateDef *st = encGraphState(ENC_IDX_COMBAT,
                                                   encounter.enemyState[i]);
                threat += (st ? st->pressure : 0) + e->damage;
                for (int b = 0; b < 2; b++)
                    if (e->behaviors[b].fx.type != EFX_NONE &&
                        e->behaviors[b].stateId == encounter.enemyState[i])
                        applyCombatEffect(&e->behaviors[b].fx);
            }
            threat += statusThreatMod();
            if (threat > 0) {
                player.hp = (threat >= (int)player.hp)
                          ? 0 : (uint16_t)(player.hp - threat);
                snprintf(lm, 28, "Pressure: -%d HP.", threat);
                logPush(lm);
            }
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

    statusTickTurn();
    if (player.hp == 0) {
        logPush("You fall unconscious.");
        enterDeath();
        return;
    }

    encounter.isFirstTurn = 0;
    if (encounter.phase == ENCOUNTER_PHASE_ACTIVE)
        generateActions();
}

/* -----------------------------------------------------------------------
 * State-graph social — the NPC walks the disposition graph; willingness
 * (enemies[0].hp, 0-100) flows from the occupied state's pressure minus
 * the NPC's pushback. Only EFX_METER moves the bar directly.
 * ----------------------------------------------------------------------- */

static void socialEnd(SocialOutcome outcome, const char *msg) {
    encounter.phase                = ENCOUNTER_PHASE_VICTORY;
    encounter.socialOutcome        = outcome;
    encounter.socialEndWillingness = encounter.enemies[0].hp;
    if (msg) logPush(msg);
}

static void applySocialEffect(const Effect *e) {
    if (!e || e->type == EFX_NONE || e->chance == 0) return;
    if (e->type == EFX_METER) {
        if (rand() % 100 < e->chance) {
            int w = encounter.enemies[0].hp + (int8_t)e->value;
            encounter.enemies[0].hp = w < 0 ? 0 : w > 100 ? 100 : w;
        }
        return;
    }
    encounterApplyEffect(e);
}

static void performSocialAction(void) {
    Action *a = &encounter.actions[encounter.selectedIndex];
    const ActionDef *adef = getActionDef((uint8_t)a->type);
    Enemy *npc = &encounter.enemies[0];
    char   lm[28];

    /* Demand force-ends the exchange at the current willingness */
    if (a->type == ACTION_DEMAND) {
        socialEnd(SOCIAL_OUTCOME_DEMAND, "You force the issue.");
        return;
    }

    if (adef) applySocialEffect(&adef->onPlay);

    /* Steer the disposition graph — same resolution as combat */
    encGraphResolve(0, ENC_IDX_SOCIAL, adef, npc->stateMask,
                    npc->tenacity, applySocialEffect);

    fireLogMessages(LOGTRIG_ACTION, (uint8_t)a->type, 0);

    /* Domain XP */
    {
        uint8_t dom = actionGetDomain((uint8_t)a->type);
        if (dom != 0xFF) {
            domainAwardXp(dom, 1);
            if (encounter.gainedDomainXp[dom] < 255)
                encounter.gainedDomainXp[dom]++;
        }
    }

    /* Willingness flow: state pressure vs. NPC pushback */
    if (encounter.phase == ENCOUNTER_PHASE_ACTIVE) {
        if (encounter.extraActionPending) {
            encounter.extraActionPending = 0;
        } else {
            const StateDef *st = encGraphState(ENC_IDX_SOCIAL,
                                               encounter.enemyState[0]);
            int flow = (st ? st->pressure : 0) - npc->damage;
            if (flow != 0) {
                int w = npc->hp + flow;
                npc->hp = w < 0 ? 0 : w > 100 ? 100 : w;
                snprintf(lm, 28, flow > 0 ? "Warming (+%d)." : "Pushback (%d).", flow);
                logPush(lm);
            }
            encounter.socialTurns++;
        }
    }

    /* Resolution thresholds + patience */
    if (encounter.phase == ENCOUNTER_PHASE_ACTIVE) {
        if (npc->hp >= 100) {
            socialEnd(SOCIAL_OUTCOME_WIN, "They come around.");
        } else if (npc->hp <= 0) {
            socialEnd(SOCIAL_OUTCOME_LOSS, "They end the conversation.");
        } else if (npc->intelligence > 0 &&
                   encounter.socialTurns >= (int)npc->intelligence) {
            socialEnd(SOCIAL_OUTCOME_LOSS, "Their patience runs out.");
        }
    }

    if (encounter.phase == ENCOUNTER_PHASE_ACTIVE)
        fireLogMessages(LOGTRIG_TURN_END, 0xFF, -1);

    statusTickTurn();
    if (player.hp == 0) { logPush("You fall unconscious."); enterDeath(); return; }

    encounter.isFirstTurn = 0;
    if (encounter.phase == ENCOUNTER_PHASE_ACTIVE)
        generateActions();
}

/* TODO: combat.c — performCombatAction, killEnemy, advanceTarget are
 * self-contained combat math with no render calls; good candidate for
 * extraction once the social port grows and starts crowding this file. */
static void performPlayerAction(void) {
    /* Combat runs on the state-graph engine */
    if (encounter.encounterType == ENCOUNTER_COMBAT) {
        performCombatAction();
        return;
    }
    /* Investigation encounters are handled entirely in investigations.c */
    if (encounter.encounterType == ENCOUNTER_INVESTIGATION) {
        Action *ia = &encounter.actions[encounter.selectedIndex];
        const ActionDef *iadef = getActionDef(ia->type);
        domainAwardXp(iadef ? iadef->domain : 0, 1);
        if (iadef) applyEffect(&iadef->onPlay);
        investigationDoAction((uint8_t)ia->type);
        statusTickTurn();
        if (player.hp == 0) { logPush("You fall unconscious."); enterDeath(); return; }
        if (encounter.phase == ENCOUNTER_PHASE_ACTIVE)
            generateActions();
        return;
    }

    /* Environmental encounters are handled entirely in env_encounter.c */
    if (encounter.encounterType == ENCOUNTER_ENVIRONMENTAL) {
        Action *ea = &encounter.actions[encounter.selectedIndex];
        const ActionDef *eadef = getActionDef(ea->type);
        if (eadef) applyEffect(&eadef->onPlay);
        envEncounterDoAction((uint8_t)ea->type);
        statusTickTurn();
        if (player.hp == 0) { logPush("You fall unconscious."); enterDeath(); }
        return;
    }

    /* Hunt runs the shared graph on the group's morale (hunt_encounter.c),
       which owns its own onPlay/status handling. */
    if (encounter.encounterType == ENCOUNTER_HUNT) {
        huntEncounterDoAction((uint8_t)encounter.actions[encounter.selectedIndex].type);
        return;
    }

    /* Social runs on the state-graph engine */
    performSocialAction();
}

void handleEncounterInput(int key) {
    if (encounter.phase == ENCOUNTER_PHASE_VICTORY ||
        encounter.phase == ENCOUNTER_PHASE_TIMEOUT) {
        if (key == VK_RETURN || key == VK_ESCAPE) {
            for (int i = 0; i < encounter.enemyCount; i++)
                if (encounter.fromWorldEnemy[i])
                    worldEnemyRemoveAt(encounter.worldEnemyX[i], encounter.worldEnemyY[i]);
            statusEncounterEnd(); /* sweep DUR_TURNS buffs */
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
        /* ESC during active combat is handled in main.c (opens pause).
           On the victory/timeout screen above, ESC dismisses the result. */
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

static const char *willingnessTag(int w) {
    if (w >= 90) return "Fully committed";
    if (w >= 70) return "Willing";
    if (w >= 50) return "On the fence";
    if (w >= 25) return "Reluctant";
    return "Openly opposes";
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

    /* ── Victory / Timeout screen ──────────────────────────────── */
    if (encounter.phase == ENCOUNTER_PHASE_VICTORY ||
        encounter.phase == ENCOUNTER_PHASE_TIMEOUT) {

        if (encounter.encounterType == ENCOUNTER_INVESTIGATION) {
            const InvestigationDef *inv = invGetDef((uint8_t)encounter.invId);
            if (encounter.phase == ENCOUNTER_PHASE_VICTORY && encounter.invSuccess) {
                drawText(bx, y, "SCENE UNDERSTOOD", rgb(220, 190, 50), 2);  y += 28;
            } else if (encounter.phase == ENCOUNTER_PHASE_TIMEOUT) {
                drawText(bx, y, "TIME RAN OUT", rgb(160, 120, 50), 2);  y += 28;
            } else {
                drawText(bx, y, "Left without answers.", rgb(140, 120, 80), 1);  y += 20;
            }
            if (inv) {
                drawText(bx, y, inv->name, rgb(180, 160, 100), 1);  y += 14;
                fillRect(LP_X + 8, y, LP_W - 16, 1, bdPanel);  y += 8;
                for (int i = 0; i < inv->clueCount; i++) {
                    const ClueDef *c = clueGetDef(inv->clueIds[i]);
                    if (!c) continue;
                    int found = (encounter.invFoundMask >> i) & 1;
                    uint32_t fc = found ? rgb(160, 200, 160) : rgb(60, 60, 60);
                    char cbuf[36];
                    cbuf[0] = found ? '+' : '-'; cbuf[1] = ' ';
                    int cl = 0; while (c->text[cl] && cl < 30) cl++;
                    for (int k = 0; k < cl; k++) cbuf[2 + k] = c->text[k];
                    cbuf[2 + cl] = '\0';
                    drawText(bx, y, cbuf, fc, 1);  y += 12;
                }
            }
            drawText(bx, LP_Y + LP_H - 20, "Enter to continue", rgb(100, 90, 60), 1);
            return;
        }

        if (encounter.encounterType == ENCOUNTER_ENVIRONMENTAL) {
            const EnvEncounterDef *edef = envEncGetDef((uint8_t)encounter.envEncId);
            if (encounter.phase == ENCOUNTER_PHASE_VICTORY) {
                drawText(bx, y, "RESOLVED",  rgb(80, 220, 80),   2);  y += 28;
            } else {
                drawText(bx, y, "FAILED",    rgb(200, 70, 70),   2);  y += 28;
            }
            if (edef) {
                drawText(bx, y, edef->name, rgb(120, 180, 120), 1);  y += 14;
                fillRect(LP_X + 8, y, LP_W - 16, 1, bdPanel);  y += 8;
                const EnvStateDef *st = &edef->states[encounter.envStateIdx];
                drawText(bx, y, st->description, rgb(90, 140, 90), 1);  y += 14;
                snprintf(buf, sizeof(buf), "Progress: %d / %d",
                    encounter.envProgress, edef->progressGoal);
                drawText(bx, y, buf, rgb(80, 130, 80), 1);
            }
            drawText(bx, LP_Y + LP_H - 20, "Enter to continue", rgb(70, 110, 70), 1);
            return;
        }

        if (encounter.encounterType == ENCOUNTER_HUNT) {
            const HuntEncounterDef *hdef = huntEncGetDef((uint8_t)encounter.huntEncId);
            if (encounter.phase == ENCOUNTER_PHASE_VICTORY) {
                drawText(bx, y, "HUNT COMPLETE", rgb(220, 180, 50), 2);  y += 28;
            } else {
                drawText(bx, y, "HUNT FAILED",   rgb(200, 70,  70), 2);  y += 28;
            }
            if (hdef) {
                drawText(bx, y, hdef->name, rgb(180, 150, 60), 1);  y += 14;
                fillRect(LP_X + 8, y, LP_W - 16, 1, bdPanel);  y += 8;
                int killed = encounter.huntEnemiesTotal - encounter.huntEnemiesLeft;
                snprintf(buf, sizeof(buf), "Eliminated: %d / %d",
                    killed, encounter.huntEnemiesTotal);
                drawText(bx, y, buf, rgb(160, 130, 50), 1);  y += 14;
                const StateDef *hst = encGraphState(ENC_IDX_HUNT, encounter.enemyState[0]);
                if (hst) { drawText(bx, y, hst->name, rgb(140, 115, 45), 1); y += 14; }
            }
            for (int i = 0; i < 4; i++) {
                if (encounter.gainedDomainXp[i] == 0) continue;
                snprintf(buf, sizeof(buf), "+%d %s xp", encounter.gainedDomainXp[i], domainName(i));
                drawText(bx, y, buf, rgb(140, 200, 255), 1);  y += 14;
            }
            drawText(bx, LP_Y + LP_H - 20, "Enter to continue", rgb(100, 90, 60), 1);
            return;
        }

        if (encounter.encounterType == ENCOUNTER_SOCIAL) {
            /* Social outcome header */
            static const char    *labels[] = { "Exchange over.",  "Agreement reached.", "Demand made.",         "They walked away." };
            static const uint32_t cols[]   = { 0xFFFFFFFF,        0xFF50DC50,           0xFFDCB432,             0xFFB43232           };
            int oi = (encounter.socialOutcome >= 1 && encounter.socialOutcome <= 3) ? encounter.socialOutcome : 0;
            drawText(bx, y, labels[oi], cols[oi], 2);  y += 28;

            /* Final willingness */
            int w = encounter.socialEndWillingness;
            const char *wtag = willingnessTag(w);
            uint32_t wcol = w >= 70 ? rgb(50, 190, 80) : w >= 50 ? rgb(190, 170, 40) : rgb(190, 55, 55);
            drawText(bx, y, wtag, wcol, 1);  y += 18;

            /* Domain XP */
            fillRect(LP_X + 8, y + 2, LP_W - 16, 1, rgb(30, 35, 80));  y += 10;
            for (int i = 0; i < 4; i++) {
                if (encounter.gainedDomainXp[i] == 0) continue;
                snprintf(buf, sizeof(buf), "+%d %s xp", encounter.gainedDomainXp[i], domainName(i));
                drawText(bx, y, buf, rgb(140, 200, 255), 1);  y += 14;
            }

        } else {
            /* Combat / other victory */
            drawText(bx, y, "VICTORY!", rgb(255, 220, 50), 2);  y += 28;

            for (int i = 0; i < 4; i++) {
                if (encounter.gainedDomainXp[i] == 0) continue;
                snprintf(buf, sizeof(buf), "+%d %s xp", encounter.gainedDomainXp[i], domainName(i));
                drawText(bx, y, buf, rgb(140, 200, 255), 2);  y += 20;
            }
            if (encounter.gainedGold > 0) {
                snprintf(buf, sizeof(buf), "+%d Solmark%s",
                         encounter.gainedGold, encounter.gainedGold == 1 ? "" : "s");
                drawText(bx, y, buf, rgb(255, 215, 0), 2);  y += 20;
            }
            for (int i = 0; i < encounter.droppedCount; i++) {
                snprintf(buf, sizeof(buf), "Found: %s", itemName(encounter.droppedItems[i]));
                drawText(bx, y, buf, rgb(140, 255, 200), 1);  y += 14;
            }
        }

        drawText(bx, LP_Y + LP_H - 20, "Enter to continue", rgb(100, 90, 80), 1);
        return;
    }

    /* ── Investigation active section ───────────────────────────── */
    if (encounter.encounterType == ENCOUNTER_INVESTIGATION) {
        const InvestigationDef *inv = invGetDef((uint8_t)encounter.invId);
        if (inv) {
            drawText(bx, y, inv->name, rgb(200, 180, 80), 2);  y += 22;

            /* The case arc this scene feeds — persists between visits */
            if (encounter.invCaseId != 0xFF) {
                const CaseDef  *cd = caseGetDef(encounter.invCaseId);
                const StateDef *ast = encGraphState(ENC_IDX_INVESTIGATION,
                                                    encounter.enemyState[0]);
                if (cd) { drawText(bx, y, cd->name, rgb(190, 170, 90), 1); y += 12; }
                if (ast) {
                    drawText(bx, y, ast->name, rgb(230, 205, 110), 1);  y += 11;
                    int fill = potMaxFrom(0, encounter.enemyState[0]) * barW / 100;
                    fillRect(bx, y, barW, 4, rgb(40, 34, 12));
                    if (fill > 0) fillRect(bx, y, fill, 4, rgb(215, 190, 80));
                    y += 8;
                }
            }
            snprintf(buf, sizeof(buf), "Turns: %d", encounter.invTurns);
            uint32_t tc = encounter.invTurns > 3 ? rgb(180, 160, 80)
                        : encounter.invTurns > 1 ? rgb(210, 130, 40)
                        :                          rgb(210, 60,  40);
            drawText(bx, y, buf, tc, 1);  y += 14;
            fillRect(LP_X + 8, y, LP_W - 16, 1, bdPanel);  y += 8;
            drawText(bx, y, inv->pressureText, rgb(120, 105, 55), 1);  y += 14;
            fillRect(LP_X + 8, y, LP_W - 16, 1, bdPanel);  y += 8;
            int found = 0, total = inv->clueCount;
            for (int i = 0; i < total; i++)
                if ((encounter.invFoundMask >> i) & 1) found++;
            snprintf(buf, sizeof(buf), "%d / %d clues", found, total);
            drawText(bx, y, buf, rgb(140, 130, 70), 1);
        }
        goto render_log;
    }

    /* ── Hunt encounter active section ─────────────────────────── */
    if (encounter.encounterType == ENCOUNTER_HUNT) {
        const HuntEncounterDef *hdef = huntEncGetDef((uint8_t)encounter.huntEncId);
        if (hdef) {
            drawText(bx, y, hdef->name, rgb(220, 170, 40), 2);  y += 22;

            /* Group morale — the hunt graph state */
            const StateDef *st = encGraphState(ENC_IDX_HUNT, encounter.enemyState[0]);
            if (st) {
                uint32_t sc = st->pressure >= 8 ? rgb(235, 80,  50)
                            : st->pressure >= 3 ? rgb(215, 150, 50)
                            :                     rgb(150, 170, 120);
                drawText(bx, y, st->name, sc, 1);  y += 12;

                /* Banked progress out of the current posture */
                int pfill = potMaxFrom(0, encounter.enemyState[0]) * barW / 100;
                fillRect(bx, y, barW, 4, rgb(35, 25, 15));
                if (pfill > 0) fillRect(bx, y, pfill, 4, rgb(200, 170, 70));
                y += 8;
            }
            fillRect(LP_X + 8, y, LP_W - 16, 1, bdPanel);  y += 8;

            /* Enemy count bar */
            int left  = encounter.huntEnemiesLeft;
            int total = encounter.huntEnemiesTotal > 0 ? encounter.huntEnemiesTotal : 1;
            int fill  = (total - left) * barW / total;
            if (fill > barW) fill = barW;
            fillRect(bx, y, barW, 8, rgb(35, 20, 5));
            if (fill > 0) fillRect(bx, y, fill, 8, rgb(210, 140, 30));
            y += 10;
            snprintf(buf, sizeof(buf), "Enemies: %d remaining", left);
            drawText(bx, y, buf, rgb(180, 120, 40), 1);  y += 14;

            /* Noise raised so far — they attack when it fills */
            if (hdef->alertLimit > 0) {
                int room = hdef->alertLimit - encounter.huntAlert;
                if (room < 0) room = 0;
                uint32_t ac = room > 2 ? rgb(150, 140, 90)
                            : room > 0 ? rgb(215, 140, 40)
                            :            rgb(220, 60, 40);
                snprintf(buf, sizeof(buf), "Alert: %d / %d",
                         encounter.huntAlert, hdef->alertLimit);
                drawText(bx, y, buf, ac, 1);
            }
        }
        goto render_log;
    }

    /* ── Environmental encounter active section ─────────────────── */
    if (encounter.encounterType == ENCOUNTER_ENVIRONMENTAL) {
        const EnvEncounterDef *edef = envEncGetDef((uint8_t)encounter.envEncId);
        if (edef) {
            drawText(bx, y, edef->name, rgb(80, 200, 80), 2);  y += 22;
            const EnvStateDef *st = &edef->states[encounter.envStateIdx];
            drawText(bx, y, st->description, rgb(100, 160, 100), 1);  y += 14;
            fillRect(LP_X + 8, y, LP_W - 16, 1, bdPanel);  y += 8;

            /* Progress bar */
            int goal  = edef->progressGoal > 0 ? edef->progressGoal : 1;
            int fill  = encounter.envProgress * barW / goal;
            if (fill > barW) fill = barW;
            fillRect(bx, y, barW, 8, rgb(12, 35, 12));
            if (fill > 0) fillRect(bx, y, fill, 8, rgb(50, 200, 80));
            y += 10;
            snprintf(buf, sizeof(buf), "Progress: %d / %d", encounter.envProgress, goal);
            drawText(bx, y, buf, rgb(80, 150, 80), 1);  y += 14;

            /* State turn budget */
            if (st->turnBudget > 0) {
                int left = st->turnBudget - encounter.envTurnInState;
                uint32_t tc = left > 2 ? rgb(140, 200, 140)
                            : left > 0 ? rgb(210, 150, 40)
                            :            rgb(210, 60,  40);
                snprintf(buf, sizeof(buf), "Turns in state: %d / %d", encounter.envTurnInState, st->turnBudget);
                drawText(bx, y, buf, tc, 1);
            }
        }
        goto render_log;
    }

    /* ── Environmental outcome screen ───────────────────────────── */

    /* ── Enemy / Social section ────────────────────────────────── */
    if (encounter.encounterType == ENCOUNTER_SOCIAL) {
        const Enemy *e = &encounter.enemies[0];
        drawText(bx, y, e->name, rgb(140, 165, 230), 1);  y += 12;

        /* Disposition = social graph state; colored by willingness flow */
        const StateDef *st = encGraphState(ENC_IDX_SOCIAL, encounter.enemyState[0]);
        if (st) {
            int flow = st->pressure - e->damage;
            uint32_t dcol = flow > 0 ? rgb(90, 190, 110)
                          : flow < 0 ? rgb(210, 100, 80)
                          :            rgb(100, 120, 200);
            drawText(bx, y, st->name, dcol, 1);  y += 11;
        }

        /* Willingness bar (0-100) */
        int w = e->hp;
        const char *wtag = willingnessTag(w);
        uint32_t wcol = w >= 70 ? rgb(50, 190, 80) : w >= 50 ? rgb(190, 170, 40) : rgb(190, 55, 55);
        fillRect(bx, y, barW, 6, rgb(12, 15, 40));
        int wfill = w * barW / 100;
        if (wfill > 0) fillRect(bx, y, wfill, 6, wcol);
        y += 9;
        drawText(bx, y, wtag, wcol, 1);  y += 12;

        /* Banked progress out of the current disposition */
        {
            int fill = potMaxFrom(0, encounter.enemyState[0]) * barW / 100;
            fillRect(bx, y, barW, 4, rgb(20, 22, 45));
            if (fill > 0) fillRect(bx, y, fill, 4, rgb(150, 160, 220));
            y += 7;
        }

        /* Patience clock */
        if (e->intelligence > 0) {
            int left = (int)e->intelligence - encounter.socialTurns;
            if (left < 0) left = 0;
            uint32_t tc = left > 3 ? rgb(110, 125, 190)
                        : left > 1 ? rgb(200, 150, 60)
                        :            rgb(210, 70, 50);
            snprintf(buf, sizeof(buf), "Patience: %d", left);
            drawText(bx, y, buf, tc, 1);  y += 12;
        }

    } else {
        drawText(bx, y, "ENEMIES", rgb(100, 50, 50), 1);  y += 12;
        for (int ei = 0; ei < encounter.enemyCount; ei++) {
            const Enemy *e  = &encounter.enemies[ei];
            int          sel  = (ei == encounter.targetIndex);
            int          dead = !e->alive;
            uint32_t     nameCol = dead ? rgb(60, 50, 50)
                                 : sel  ? rgb(255, 210, 80)
                                 :        rgb(200, 90, 90);
            char nbuf[20];
            nbuf[0] = sel ? '>' : ' '; nbuf[1] = ' ';
            int nl = 0; while (e->name[nl] && nl < 16) nl++;
            for (int c = 0; c < nl && c < 17; c++) nbuf[2 + c] = e->name[c];
            nbuf[2 + (nl < 17 ? nl : 17)] = '\0';
            drawText(bx, y, nbuf, nameCol, 1);  y += 11;

            /* Current graph state + banked progress out of it */
            const StateDef *st = dead ? NULL
                : encGraphState(ENC_IDX_COMBAT, encounter.enemyState[ei]);
            if (dead) {
                drawText(bx + 8, y, "Broken", rgb(70, 60, 60), 1);  y += 11;
                fillRect(bx, y, barW, 4, rgb(28, 18, 18));
            } else if (st) {
                int t = st->pressure + e->damage;
                uint32_t stCol = t >= 10 ? rgb(230, 90, 60)
                               : t >= 5  ? rgb(210, 150, 60)
                               : t >  0  ? rgb(190, 170, 90)
                               :           rgb(140, 150, 170);
                drawText(bx + 8, y, st->name, stCol, 1);  y += 11;
                int fill = potMaxFrom(ei, encounter.enemyState[ei]) * barW / 100;
                fillRect(bx, y, barW, 4, rgb(35, 25, 15));
                if (fill > 0)
                    fillRect(bx, y, fill, 4, sel ? rgb(220, 190, 60) : rgb(150, 120, 50));
            }
            y += 8;
        }
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

render_log:
    /* ── Active statuses ────────────────────────────────────────── */
    if (renderStatusStrip(bx, y, 0) > 0)
        y += 12;

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

        /* Favour / suppress badge */
        if (isActionFavoured(aid))
            drawText(cx + 4, CARD_Y + 4, "F", rgb(80, 220, 80), 1);
        else if (isActionSuppressed(aid))
            drawText(cx + 4, CARD_Y + 4, "S", rgb(220, 70, 70), 1);
    }

    if (encounter.enemyCount > 1) {
        drawText(CARD_X0, CARD_Y + CARD_H + 4,
                 "TAB: switch target", rgb(55, 55, 80), 1);
    }
    drawText(CARD_X0, CARD_Y + CARD_H + 16, "F: favour  S: suppress", rgb(55, 55, 80), 1);
}
