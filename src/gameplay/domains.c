#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "domains.h"
#include "player.h"
#include "actions.h"
#include "game.h"
#include "gfx.h"

/* ── Node data ──────────────────────────────────────────────────────────────
   N(id, domain, p0, p1, cost, tree_row, tree_col, type, reward_value, name)  */
#define N(i,d,p0,p1,c,tr,tc,t,v,nm) \
    { (i),(d),{(p0),(p1)},(c),(tr),(tc),(t),(v),(nm) }

Domain domains[DOMAIN_COUNT] = {

    /* ── DOMAIN_COMBAT ──────────────────────────────────────────────────────
       col:      0                 1               2
       row 0:              [Strong Attack]
       row 1: [Grd.Stance] [Parry]       [Shield Break]
       row 2: [+1 Atk]     [Stun]        [Disarm]
       row 3:              [Riposte]                     ← prereq Stun+Disarm
       row 4: [+1 Atk]     [Execute]     [+5 MaxHP]     ← prereq +1Atk+Riposte
       row 5: [+1 Atk]                   [+1 Def]       ← from Execute         */
    { DOMAIN_COMBAT, DOMAIN_TYPE_BASE, 0xFF, 0xFF, {
        N( 0, DOMAIN_COMBAT, 0xFF,0xFF, 0, 0,1, NODE_REWARD_ACTION, ACTION_STRONG,         "Strong Attack"),
        N( 1, DOMAIN_COMBAT, 0,   0xFF, 1, 1,0, NODE_REWARD_ACTION, ACTION_GUARDED_STANCE, "Guarded Stance"),
        N( 2, DOMAIN_COMBAT, 0,   0xFF, 1, 1,1, NODE_REWARD_ACTION, ACTION_DEFEND,         "Parry"),
        N( 3, DOMAIN_COMBAT, 0,   0xFF, 1, 1,2, NODE_REWARD_ACTION, ACTION_SHIELD_BREAK,   "Shield Break"),
        N( 4, DOMAIN_COMBAT, 1,   0xFF, 2, 2,0, NODE_REWARD_STAT,   STAT_NODE(DSTAT_ATTACK,1),  "+1 Attack"),
        N( 5, DOMAIN_COMBAT, 2,   0xFF, 2, 2,1, NODE_REWARD_ACTION, ACTION_STUN,           "Stun"),
        N( 6, DOMAIN_COMBAT, 3,   0xFF, 2, 2,2, NODE_REWARD_ACTION, ACTION_DISARM,         "Disarm"),
        N( 7, DOMAIN_COMBAT, 5,   6,    3, 3,1, NODE_REWARD_ACTION, ACTION_RIPOSTE,        "Riposte"),
        N( 8, DOMAIN_COMBAT, 4,   7,    5, 4,0, NODE_REWARD_STAT,   STAT_NODE(DSTAT_ATTACK,1),  "+1 Attack"),
        N( 9, DOMAIN_COMBAT, 4,   7,    5, 4,1, NODE_REWARD_ACTION, ACTION_EXECUTE,        "Execute"),
        N(10, DOMAIN_COMBAT, 4,   7,    5, 4,2, NODE_REWARD_STAT,   STAT_NODE(DSTAT_MAXHP,5),   "+5 Max HP"),
        N(11, DOMAIN_COMBAT, 9,   0xFF, 6, 5,0, NODE_REWARD_STAT,   STAT_NODE(DSTAT_ATTACK,1),  "+1 Attack"),
        N(12, DOMAIN_COMBAT, 9,   0xFF, 6, 5,2, NODE_REWARD_STAT,   STAT_NODE(DSTAT_DEFENSE,1), "+1 Defense"),
    }},

    /* ── DOMAIN_TRICKERY ────────────────────────────────────────────────────
       col:      0               1               2
       row 0:               [Moonstep]
       row 1: [Pocket Sand]              [Fake Weakness]
       row 2: [+1 Agility]  [Blindspot]  [+1 Agility]
       row 3: [+1 Percep]   [Disarm]     [+1 Percep]  ← prereq from row 2
       row 4:               [+1 Percep]               ← capstone              */
    { DOMAIN_TRICKERY, DOMAIN_TYPE_BASE, 0xFF, 0xFF, {
        N( 0, DOMAIN_TRICKERY, 0xFF,0xFF, 0, 0,1, NODE_REWARD_ACTION, ACTION_BACKSTAB,      "Moonstep"),
        N( 1, DOMAIN_TRICKERY, 0,   0xFF, 1, 1,0, NODE_REWARD_ACTION, ACTION_POCKET_SAND,   "Pocket Sand"),
        N( 2, DOMAIN_TRICKERY, 0,   0xFF, 1, 1,2, NODE_REWARD_ACTION, ACTION_FAKE_WEAKNESS, "Fake Weakness"),
        N( 3, DOMAIN_TRICKERY, 1,   0xFF, 2, 2,0, NODE_REWARD_STAT,   STAT_NODE(DSTAT_AGILITY,1),    "+1 Agility"),
        N( 4, DOMAIN_TRICKERY, 0,   0xFF, 2, 2,1, NODE_REWARD_ACTION, ACTION_HIDE,          "Blindspot"),
        N( 5, DOMAIN_TRICKERY, 2,   0xFF, 2, 2,2, NODE_REWARD_STAT,   STAT_NODE(DSTAT_AGILITY,1),    "+1 Agility"),
        N( 6, DOMAIN_TRICKERY, 3,   0xFF, 3, 3,0, NODE_REWARD_STAT,   STAT_NODE(DSTAT_PERCEPTION,1), "+1 Perception"),
        N( 7, DOMAIN_TRICKERY, 3,   5,    3, 3,1, NODE_REWARD_ACTION, ACTION_DISARM,        "Disarm"),
        N( 8, DOMAIN_TRICKERY, 5,   0xFF, 3, 3,2, NODE_REWARD_STAT,   STAT_NODE(DSTAT_PERCEPTION,1), "+1 Perception"),
        N( 9, DOMAIN_TRICKERY, 6,   8,    4, 4,1, NODE_REWARD_STAT,   STAT_NODE(DSTAT_PERCEPTION,1), "+1 Perception"),
    }},

    /* ── DOMAIN_BLOOD ───────────────────────────────────────────────────────
       col:      0                1              2
       row 0:               [+5 Max HP]
       row 1: [Regenerate]  [Blood Mist]  [+1 Int.]
       row 2: [Crimson Rush] [+5 MaxHP]   [+1 Stamina]
       row 3: [Hemorrhage]               [Feast Fear]  ← from Crimson Rush
       row 4:               [Bat Swarm]                ← prereq 6+7, capstone  */
    { DOMAIN_BLOOD, DOMAIN_TYPE_BASE, 0xFF, 0xFF, {
        N( 0, DOMAIN_BLOOD, 0xFF,0xFF, 0, 0,1, NODE_REWARD_STAT,   STAT_NODE(DSTAT_MAXHP,5),        "+5 Max HP"),
        N( 1, DOMAIN_BLOOD, 0,   0xFF, 1, 1,0, NODE_REWARD_ACTION, ACTION_HEAL,          "Regenerate"),
        N( 2, DOMAIN_BLOOD, 0,   0xFF, 1, 1,1, NODE_REWARD_ACTION, ACTION_BLOOD_MIST,    "Blood Mist"),
        N( 3, DOMAIN_BLOOD, 0,   0xFF, 1, 1,2, NODE_REWARD_STAT,   STAT_NODE(DSTAT_INTELLIGENCE,1), "+1 Int."),
        N( 4, DOMAIN_BLOOD, 1,   0xFF, 2, 2,0, NODE_REWARD_ACTION, ACTION_CRIMSON_RUSH,  "Crimson Rush"),
        N( 5, DOMAIN_BLOOD, 2,   0xFF, 2, 2,1, NODE_REWARD_STAT,   STAT_NODE(DSTAT_MAXHP,5),        "+5 Max HP"),
        N( 6, DOMAIN_BLOOD, 3,   0xFF, 2, 2,2, NODE_REWARD_STAT,   STAT_NODE(DSTAT_STAMINA,1),      "+1 Stamina"),
        N( 7, DOMAIN_BLOOD, 4,   0xFF, 3, 3,0, NODE_REWARD_ACTION, ACTION_HEMORRHAGE,    "Hemorrhage"),
        N( 8, DOMAIN_BLOOD, 4,   6,    3, 3,2, NODE_REWARD_ACTION, ACTION_FEAST_FEAR,    "Feast Upon Fear"),
        N( 9, DOMAIN_BLOOD, 7,   8,    5, 4,1, NODE_REWARD_ACTION, ACTION_BAT_SWARM,     "Bat Swarm"),
        N(10, DOMAIN_BLOOD, 5,   9,    6, 5,1, NODE_REWARD_STAT,   STAT_NODE(DSTAT_MAXHP,5),        "+5 Max HP"),
    }},

    /* ── DOMAIN_CHARM ───────────────────────────────────────────────────────
       col:      0                1               2
       row 0:               [Persuade]
       row 1: [Noble Pres.] [+1 Charisma] [Prove Skill]
       row 2: [+1 Defense]  [+1 Int.]     [+1 Charisma]
       row 3:               [+1 Charisma]              ← prereq 4+5
       row 4:               [Duelist's Mark]            ← prereq 7            */
    { DOMAIN_CHARM, DOMAIN_TYPE_BASE, 0xFF, 0xFF, {
        N( 0, DOMAIN_CHARM, 0xFF,0xFF, 0, 0,1, NODE_REWARD_ACTION, ACTION_CALM,           "Persuade"),
        N( 1, DOMAIN_CHARM, 0,   0xFF, 1, 1,0, NODE_REWARD_ACTION, ACTION_NOBLE_PRESENCE, "Noble Presence"),
        N( 2, DOMAIN_CHARM, 0,   0xFF, 1, 1,1, NODE_REWARD_STAT,   STAT_NODE(DSTAT_CHARISMA,1),     "+1 Charisma"),
        N( 3, DOMAIN_CHARM, 0,   0xFF, 1, 1,2, NODE_REWARD_ACTION, ACTION_PROVE_SKILL,    "Prove Skill"),
        N( 4, DOMAIN_CHARM, 1,   0xFF, 2, 2,0, NODE_REWARD_STAT,   STAT_NODE(DSTAT_DEFENSE,1),      "+1 Defense"),
        N( 5, DOMAIN_CHARM, 2,   0xFF, 2, 2,1, NODE_REWARD_STAT,   STAT_NODE(DSTAT_INTELLIGENCE,1), "+1 Int."),
        N( 6, DOMAIN_CHARM, 3,   0xFF, 2, 2,2, NODE_REWARD_STAT,   STAT_NODE(DSTAT_CHARISMA,1),     "+1 Charisma"),
        N( 7, DOMAIN_CHARM, 4,   5,    3, 3,1, NODE_REWARD_STAT,   STAT_NODE(DSTAT_CHARISMA,1),     "+1 Charisma"),
        N( 8, DOMAIN_CHARM, 6,   7,    4, 4,1, NODE_REWARD_ACTION, ACTION_DUELIST_MARK,   "Duelist's Mark"),
    }},

    /* Fusion domains (nodes TBD) */
    { 4,  DOMAIN_TYPE_FUSION, DOMAIN_COMBAT,   DOMAIN_TRICKERY, {{0}} },
    { 5,  DOMAIN_TYPE_FUSION, DOMAIN_COMBAT,   DOMAIN_BLOOD,    {{0}} },
    { 6,  DOMAIN_TYPE_FUSION, DOMAIN_COMBAT,   DOMAIN_CHARM,    {{0}} },
    { 7,  DOMAIN_TYPE_FUSION, DOMAIN_TRICKERY, DOMAIN_BLOOD,    {{0}} },
    { 8,  DOMAIN_TYPE_FUSION, DOMAIN_TRICKERY, DOMAIN_CHARM,    {{0}} },
    { 9,  DOMAIN_TYPE_FUSION, DOMAIN_BLOOD,    DOMAIN_CHARM,    {{0}} },

    /* Story-gated domains (unlock conditions TBD) */
    { 10, DOMAIN_TYPE_STORY,  0xFF, 0xFF, {{0}} },
    { 11, DOMAIN_TYPE_STORY,  0xFF, 0xFF, {{0}} },
    { 12, DOMAIN_TYPE_STORY,  0xFF, 0xFF, {{0}} },
    { 13, DOMAIN_TYPE_STORY,  0xFF, 0xFF, {{0}} },
};

#undef N

/* ── Core logic ─────────────────────────────────────────────────────────── */

const char *domainName(int id) {
    switch (id) {
        case DOMAIN_COMBAT:   return "Combat";
        case DOMAIN_TRICKERY: return "Trickery";
        case DOMAIN_BLOOD:    return "Blood";
        case DOMAIN_CHARM:    return "Charm";
        default:              return "???";
    }
}

int domainXpToNext(uint8_t level) { return 20 + level * 15; }

int domainAwardXp(uint8_t domain_id, int amount) {
    if (domain_id >= DOMAIN_COUNT) return 0;
    PlayerDomainState *d = &player.domains[domain_id];
    int leveled = 0;
    d->xp = (uint16_t)(d->xp + amount);
    while (d->xp >= (uint16_t)domainXpToNext(d->level)) {
        d->xp = (uint16_t)(d->xp - domainXpToNext(d->level));
        if (d->level < 255) d->level++;
        if (d->perk_points < 255) d->perk_points++;
        leveled++;
    }
    return leveled;
}

int domainNodeUnlocked(uint8_t domain_id, uint8_t node_id) {
    if (domain_id >= DOMAIN_COUNT || node_id >= DOMAIN_NODE_MAX) return 0;
    return (player.domains[domain_id].unlocked_nodes >> node_id) & 1;
}

int domainUnlockNode(uint8_t domain_id, uint8_t node_id) {
    if (domain_id >= DOMAIN_COUNT || node_id >= DOMAIN_NODE_MAX) return 0;
    if (domainNodeUnlocked(domain_id, node_id)) return 0;
    player.domains[domain_id].unlocked_nodes |= (1u << node_id);
    if (player.domains[domain_id].perk_points > 0)
        player.domains[domain_id].perk_points--;
    return 1;
}

int domainCanUnlock(uint8_t domain_id, uint8_t node_id) {
    if (domain_id >= DOMAIN_COUNT || node_id >= DOMAIN_NODE_MAX) return 0;
    if (domainNodeUnlocked(domain_id, node_id)) return 0;
    const DomainNode *n = &domains[domain_id].nodes[node_id];
    if (n->name[0] == '\0') return 0;
    if (player.domains[domain_id].perk_points == 0) return 0;
    for (int i = 0; i < 2; i++) {
        if (n->prereq[i] == 0xFF) continue;
        if (!domainNodeUnlocked(domain_id, n->prereq[i])) return 0;
    }
    return 1;
}

int domainApplyNode(uint8_t domain_id, uint8_t node_id) {
    if (domain_id >= DOMAIN_COUNT || node_id >= DOMAIN_NODE_MAX) return 0;
    const DomainNode *n = &domains[domain_id].nodes[node_id];
    if (n->type != NODE_REWARD_STAT) return 1;
    uint8_t stat_id = (uint8_t)((n->reward_value >> 8) & 0xFF);
    uint8_t delta   = (uint8_t)(n->reward_value & 0xFF);
    switch (stat_id) {
        case DSTAT_ATTACK:       if ((int)player.attack+delta<=255)       player.attack+=delta;       break;
        case DSTAT_DEFENSE:      if ((int)player.defense+delta<=255)      player.defense+=delta;      break;
        case DSTAT_MAXHP:        player.maxHp+=delta; player.hp+=delta;                               break;
        case DSTAT_STAMINA:      if ((int)player.stamina+delta<=255)      player.stamina+=delta;      break;
        case DSTAT_INTELLIGENCE: if ((int)player.intelligence+delta<=255) player.intelligence+=delta; break;
        case DSTAT_PERCEPTION:   if ((int)player.perception+delta<=255)   player.perception+=delta;   break;
        case DSTAT_CHARISMA:     if ((int)player.charisma+delta<=255)     player.charisma+=delta;     break;
        case DSTAT_AGILITY:      if ((int)player.agility+delta<=255)      player.agility+=delta;      break;
    }
    return 1;
}

uint8_t domainPrimary(void) {
    uint8_t best = DOMAIN_COMBAT;
    for (uint8_t i = 1; i < DOMAIN_COUNT; i++)
        if (player.domains[i].level > player.domains[best].level)
            best = i;
    return best;
}

/* ── UI state ───────────────────────────────────────────────────────────── */

#define VISIBLE_ROWS 4   /* rows shown at once in tree; detail panel sits below */

static int g_domainSel   = 0;  /* overview: which panel */
static int g_inTree      = 0;  /* 0 = overview, 1 = tree */
static int g_curRow      = 0;  /* tree: cursor row */
static int g_curCol      = 1;  /* tree: cursor col */
static int g_treeScroll  = 0;  /* first visible row */

static int nodeCount(uint8_t did) {
    int n = 0;
    while (n < DOMAIN_NODE_MAX && domains[did].nodes[n].name[0] != '\0') n++;
    return n;
}

static int unlockedCount(uint8_t did) {
    int u = 0, total = nodeCount(did);
    for (int i = 0; i < total; i++)
        if (domainNodeUnlocked(did, (uint8_t)i)) u++;
    return u;
}

/* Finds node index at (row, col), or -1. */
static int nodeAtPos(uint8_t did, int row, int col) {
    int total = nodeCount(did);
    for (int i = 0; i < total; i++) {
        const DomainNode *n = &domains[did].nodes[i];
        if ((int)n->tree_row == row && (int)n->tree_col == col) return i;
    }
    return -1;
}

/* Finds node index in `row` nearest to `preferred_col`, or -1. */
static int findInRow(uint8_t did, int row, int preferred_col) {
    int best = -1, bestDist = 99;
    int total = nodeCount(did);
    for (int i = 0; i < total; i++) {
        const DomainNode *n = &domains[did].nodes[i];
        if ((int)n->tree_row != row) continue;
        int d = (int)n->tree_col - preferred_col;
        if (d < 0) d = -d;
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

/* ── handleDomainsInput ─────────────────────────────────────────────────── */

void handleDomainsInput(int key) {
    if (!g_inTree) {
        switch (key) {
            case VK_LEFT:  if (g_domainSel > 0) g_domainSel--; break;
            case VK_RIGHT: if (g_domainSel < 3) g_domainSel++; break;
            case VK_RETURN:
                if (nodeCount((uint8_t)g_domainSel) > 0) {
                    g_inTree     = 1;
                    g_curRow     = 0;
                    g_treeScroll = 0;
                    int ni = findInRow((uint8_t)g_domainSel, 0, 1);
                    g_curCol = ni >= 0 ? domains[g_domainSel].nodes[ni].tree_col : 1;
                }
                break;
            case VK_ESCAPE: case 'P': state = STATE_WORLD; break;
            case 'F': case 'f':
                if (player.focusedDomain == (uint8_t)g_domainSel)
                    player.focusedDomain = DOMAIN_NONE;
                else
                    player.focusedDomain = (uint8_t)g_domainSel;
                break;
        }
        return;
    }

    uint8_t did = (uint8_t)g_domainSel;
    switch (key) {
        case VK_UP: {
            int ni = findInRow(did, g_curRow - 1, g_curCol);
            if (ni >= 0) {
                g_curRow--;
                g_curCol = domains[did].nodes[ni].tree_col;
                if (g_curRow < g_treeScroll) g_treeScroll = g_curRow;
            }
            break;
        }
        case VK_DOWN: {
            int ni = findInRow(did, g_curRow + 1, g_curCol);
            if (ni >= 0) {
                g_curRow++;
                g_curCol = domains[did].nodes[ni].tree_col;
                if (g_curRow >= g_treeScroll + VISIBLE_ROWS)
                    g_treeScroll = g_curRow - VISIBLE_ROWS + 1;
            }
            break;
        }
        case VK_LEFT: {
            int best = -1, bestCol = -1, total = nodeCount(did);
            for (int i = 0; i < total; i++) {
                const DomainNode *n = &domains[did].nodes[i];
                if ((int)n->tree_row != g_curRow || (int)n->tree_col >= g_curCol) continue;
                if ((int)n->tree_col > bestCol) { bestCol = n->tree_col; best = i; }
            }
            if (best >= 0) g_curCol = bestCol;
            break;
        }
        case VK_RIGHT: {
            int best = -1, bestCol = 99, total = nodeCount(did);
            for (int i = 0; i < total; i++) {
                const DomainNode *n = &domains[did].nodes[i];
                if ((int)n->tree_row != g_curRow || (int)n->tree_col <= g_curCol) continue;
                if ((int)n->tree_col < bestCol) { bestCol = n->tree_col; best = i; }
            }
            if (best >= 0) g_curCol = bestCol;
            break;
        }
        case VK_RETURN: {
            int ni = nodeAtPos(did, g_curRow, g_curCol);
            if (ni >= 0 && domainCanUnlock(did, (uint8_t)ni)) {
                domainUnlockNode(did, (uint8_t)ni);
                domainApplyNode(did, (uint8_t)ni);
            }
            break;
        }
        case VK_ESCAPE: case 'P':
            g_inTree = 0;
            break;
    }
}

/* ── Render helpers ─────────────────────────────────────────────────────── */

static uint32_t domainAccent(int id) {
    switch (id) {
        case DOMAIN_COMBAT:   return rgb(220, 90,  90);
        case DOMAIN_TRICKERY: return rgb(90,  180, 90);
        case DOMAIN_BLOOD:    return rgb(160, 60,  210);
        case DOMAIN_CHARM:    return rgb(90,  160, 220);
        default:              return rgb(180, 180, 180);
    }
}

/* L-shaped connector: px,py = bottom-center of parent; cx,cy = top-center of child */
static void drawConnector(int px, int py, int cx, int cy, uint32_t col) {
    if (py >= cy) return;
    if (px == cx) {
        fillRect(px - 1, py, 2, cy - py, col);
        return;
    }
    int mid = (py + cy) / 2;
    int x0  = px < cx ? px : cx;
    int x1  = px < cx ? cx : px;
    fillRect(px - 1, py,       2,          mid - py + 1, col); /* down from parent   */
    fillRect(x0 - 1, mid,      x1 - x0 + 2, 2,           col); /* horizontal bridge  */
    fillRect(cx - 1, mid,      2,          cy - mid,     col); /* down to child      */
}

/* ── renderDomains: overview ─────────────────────────────────────────────── */

static void renderOverview(void) {
    const int BOX_X = 40, BOX_Y = 40;
    const int BOX_W = gfxWidth  - 80;
    const int BOX_H = gfxHeight - 80;
    const int GAP   = 8;
    const int PW    = (BOX_W - 3 * GAP) / 4;
    const int PH    = BOX_H - 56;
    const int PY    = BOX_Y + 44;

    fillRect(BOX_X, BOX_Y, BOX_W, BOX_H, rgb(8, 6, 18));
    drawText(BOX_X + 16, BOX_Y + 12, "DOMAINS", rgb(180, 160, 255), 2);

    for (int i = 0; i < 4; i++) {
        int px  = BOX_X + i * (PW + GAP);
        int sel = (i == g_domainSel);
        uint32_t accent = domainAccent(i);
        uint32_t bdCol  = sel ? accent        : rgb(40, 36, 60);
        uint32_t bgCol  = sel ? rgb(18,14,36) : rgb(12,10,24);

        fillRect(px,       PY,       PW, PH, bgCol);
        fillRect(px,       PY,       PW,  2, bdCol);
        fillRect(px,       PY+PH-2,  PW,  2, bdCol);
        fillRect(px,       PY,        2, PH, bdCol);
        fillRect(px+PW-2,  PY,        2, PH, bdCol);

        const PlayerDomainState *d = &player.domains[i];
        int tx = px + 10, ty = PY + 14;

        drawText(tx, ty, domainName(i), sel ? accent : rgb(120,110,160), 2);
        ty += 28;

        char buf[32];
        snprintf(buf, sizeof(buf), "Lv. %d", d->level);
        drawText(tx, ty, buf, rgb(200,200,200), 2);
        ty += 24;

        int barW = PW - 20, next = domainXpToNext(d->level);
        int fill = next > 0 ? (int)((long)d->xp * barW / next) : 0;
        fillRect(tx, ty, barW, 8, rgb(30,25,50));
        if (fill > 0) fillRect(tx, ty, fill, 8, accent);
        ty += 12;
        snprintf(buf, sizeof(buf), "%d / %d xp", d->xp, next);
        drawText(tx, ty, buf, rgb(100,95,130), 1);
        ty += 16;

        snprintf(buf, sizeof(buf), "%d / %d nodes", unlockedCount((uint8_t)i), nodeCount((uint8_t)i));
        drawText(tx, ty, buf, rgb(100,95,130), 1);
        ty += 14;
        if (d->perk_points > 0) {
            snprintf(buf, sizeof(buf), "%d pt%s available",
                     d->perk_points, d->perk_points == 1 ? "" : "s");
            drawText(tx, ty, buf, rgb(200,180,60), 1);
        }

        if (player.focusedDomain == (uint8_t)i) {
            drawText(px + PW - 52, PY + 8, "FOCUS", accent, 1);
        }
    }

    drawText(BOX_X + 16, BOX_Y + BOX_H - 20,
             "LEFT/RIGHT: select   ENTER: open tree   F: toggle focus   P/ESC: close",
             rgb(55,50,80), 1);
}

/* ── renderDomains: tree ─────────────────────────────────────────────────── */

static void renderTree(void) {
    const int BOX_X  = 40, BOX_Y = 40;
    const int BOX_W  = gfxWidth  - 80;
    const int BOX_H  = gfxHeight - 80;
    const int TM_X   = BOX_X + 16;
    const int TM_Y   = BOX_Y + 44;         /* below header */
    const int CELL_W = (BOX_W - 32) / 3;   /* 3-column grid */
    const int CELL_H = 46;
    const int NW     = 108;                 /* node box width  */
    const int NH     = 18;                  /* node box height */

    fillRect(BOX_X, BOX_Y, BOX_W, BOX_H, rgb(8,6,18));

    uint8_t did     = (uint8_t)g_domainSel;
    uint32_t accent = domainAccent(g_domainSel);
    const PlayerDomainState *d = &player.domains[did];

    /* Header */
    char buf[64];
    drawText(TM_X, BOX_Y + 12, domainName(g_domainSel), accent, 2);
    snprintf(buf, sizeof(buf), "Lv.%d   %d / %d xp   pts: %d",
             d->level, d->xp, domainXpToNext(d->level), d->perk_points);
    drawText(TM_X + 156, BOX_Y + 16, buf, rgb(140,130,180), 1);
    fillRect(BOX_X + 8, BOX_Y + 38, BOX_W - 16, 1, rgb(50,44,80));

    int total = nodeCount(did);

    /* Visible row range */
    int rowFirst = g_treeScroll;
    int rowLast  = g_treeScroll + VISIBLE_ROWS - 1;

    /* Scroll indicators */
    if (rowFirst > 0)
        drawText(TM_X + CELL_W + CELL_W/2 - 4, TM_Y - 14, "^", rgb(120,110,160), 1);
    {
        int maxRow = 0;
        for (int i = 0; i < total; i++)
            if ((int)domains[did].nodes[i].tree_row > maxRow)
                maxRow = domains[did].nodes[i].tree_row;
        if (rowLast < maxRow)
            drawText(TM_X + CELL_W + CELL_W/2 - 4,
                     TM_Y + VISIBLE_ROWS * CELL_H + 2, "v", rgb(120,110,160), 1);
    }

    /* ── Connector lines (drawn first, under nodes) ────────────────────── */
    for (int i = 0; i < total; i++) {
        const DomainNode *child = &domains[did].nodes[i];
        if ((int)child->tree_row < rowFirst || (int)child->tree_row > rowLast) continue;

        int cx = TM_X + child->tree_col * CELL_W + CELL_W / 2;
        int cy = TM_Y + (child->tree_row - g_treeScroll) * CELL_H + CELL_H / 2 - NH / 2;

        for (int p = 0; p < 2; p++) {
            if (child->prereq[p] == 0xFF) continue;
            const DomainNode *par = &domains[did].nodes[child->prereq[p]];
            if ((int)par->tree_row < rowFirst || (int)par->tree_row > rowLast) continue;
            int px = TM_X + par->tree_col * CELL_W + CELL_W / 2;
            int py = TM_Y + (par->tree_row - g_treeScroll) * CELL_H + CELL_H / 2 + NH / 2;

            int prereqUnlocked = domainNodeUnlocked(did, child->prereq[p]);
            uint32_t lineCol   = prereqUnlocked ? rgb(60,100,60) : rgb(35,32,55);
            drawConnector(px, py, cx, cy, lineCol);
        }
    }

    /* ── Node boxes ────────────────────────────────────────────────────── */
    int selNodeId = nodeAtPos(did, g_curRow, g_curCol);

    for (int i = 0; i < total; i++) {
        const DomainNode *n = &domains[did].nodes[i];
        if ((int)n->tree_row < rowFirst || (int)n->tree_row > rowLast) continue;
        int ncx = TM_X + n->tree_col * CELL_W + CELL_W / 2;
        int ncy = TM_Y + (n->tree_row - g_treeScroll) * CELL_H + CELL_H / 2;
        int nx  = ncx - NW / 2;
        int ny  = ncy - NH / 2;

        int unlocked = domainNodeUnlocked(did, (uint8_t)i);
        int cando    = !unlocked && domainCanUnlock(did, (uint8_t)i);
        int isSel    = (i == selNodeId);

        uint32_t bgCol, bdCol, txCol;
        if (unlocked) {
            bgCol = rgb(18, 50, 18); bdCol = rgb(60, 160, 60);  txCol = rgb(100,220,100);
        } else if (cando) {
            bgCol = rgb(38, 36,  8); bdCol = rgb(160,140, 20);  txCol = rgb(210,190, 60);
        } else {
            bgCol = rgb(10, 10, 20); bdCol = rgb(40,  36, 60);  txCol = rgb(65,  60, 95);
        }

        /* selection highlight — outer glow */
        if (isSel) fillRect(nx - 3, ny - 3, NW + 6, NH + 6, accent);

        fillRect(nx, ny, NW, NH, bgCol);
        fillRect(nx, ny,       NW,  1, bdCol);
        fillRect(nx, ny+NH-1,  NW,  1, bdCol);
        fillRect(nx, ny,        1, NH, bdCol);
        fillRect(nx+NW-1, ny,   1, NH, bdCol);

        /* Centered name text */
        int nameLen = 0;
        while (n->name[nameLen] && nameLen < 16) nameLen++;
        int textW = nameLen * 8;
        int tx    = nx + (NW - textW) / 2;
        drawText(tx, ny + (NH - 8) / 2, n->name, isSel ? rgb(255,255,255) : txCol, 1);
    }

    /* ── Detail panel ───────────────────────────────────────────────────── */
    int detY = TM_Y + VISIBLE_ROWS * CELL_H + 14;
    fillRect(BOX_X + 8, detY - 4, BOX_W - 16, 1, rgb(50,44,80));

    if (selNodeId >= 0) {
        const DomainNode *n = &domains[did].nodes[selNodeId];
        int unlocked = domainNodeUnlocked(did, (uint8_t)selNodeId);
        int cando    = !unlocked && domainCanUnlock(did, (uint8_t)selNodeId);

        /* What it gives */
        if (n->type == NODE_REWARD_ACTION) {
            const ActionDef *a = getActionDef((uint8_t)n->reward_value);
            snprintf(buf, sizeof(buf), "Unlocks: %s", a ? a->desc : "???");
        } else {
            static const char *statNames[] = {
                "Attack","Defense","Max HP","Stamina",
                "Intelligence","Perception","Charisma","Agility"
            };
            uint8_t sid   = (uint8_t)((n->reward_value >> 8) & 0xFF);
            uint8_t delta = (uint8_t)(n->reward_value & 0xFF);
            snprintf(buf, sizeof(buf), "+%d %s", delta, sid < 8 ? statNames[sid] : "?");
        }
        drawText(TM_X, detY + 4, buf, rgb(160,155,200), 1);

        /* Lock reason */
        if (!unlocked) {
            char req[48] = "";
            if (d->perk_points == 0) {
                snprintf(req, sizeof(req), "No perk points available");
            } else {
                for (int p = 0; p < 2; p++) {
                    if (n->prereq[p] == 0xFF) continue;
                    if (!domainNodeUnlocked(did, n->prereq[p])) {
                        snprintf(req, sizeof(req), "Need: %s first",
                                 domains[did].nodes[n->prereq[p]].name);
                        break;
                    }
                }
            }
            if (req[0]) drawText(TM_X, detY + 18, req, rgb(160,80,80), 1);
        }

        /* Action hint */
        const char *hint = unlocked ? "Already unlocked"
                         : cando    ? "ENTER: unlock"
                         :            "";
        if (hint[0]) drawText(TM_X, detY + 32, hint, cando ? rgb(180,160,40) : rgb(80,75,100), 1);
    }

    drawText(BOX_X + BOX_W - 130, BOX_Y + BOX_H - 20, "Arrows: move   P/ESC: back", rgb(55,50,80), 1);
}

void renderDomains(void) {
    if (g_inTree) renderTree();
    else          renderOverview();
}
