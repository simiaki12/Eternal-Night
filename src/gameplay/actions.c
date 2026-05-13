#include "actions.h"
#include "gfx.h"
#include "player.h"
#include "items.h"
#include "domains.h"
#include <string.h>

ActionDef actionDefs[ACTION_MAX];
int       actionDefCount = 0;

static const ActionDef builtinDefs[] = {
    { ACTION_ATTACK,   0,                    70,  0,  "Slash",         "a1",  "A quick, reliable strike.",        DOMAIN_COMBAT,   ACT_CAT_COMBAT,                    {0} },
    { ACTION_STRONG,   0,                    35,  4,  "Strong Attack",  "a2",  "Heavy blow dealing bonus damage.", DOMAIN_COMBAT,   ACT_CAT_COMBAT,                    {0} },
    { ACTION_HEAL,     ACT_CTX_PLAYER_HURT,  55, 10,  "Regenerate",    "a3",  "Draw on vitality to restore HP.",  DOMAIN_BLOOD,    ACT_CAT_COMBAT,                    {0} },
    { ACTION_DEFEND,   0,                    28,  0,  "Parry",         "a4",  "Brace and halve incoming damage.", DOMAIN_COMBAT,   ACT_CAT_COMBAT,                    {0} },
    { ACTION_DISARM,   ACT_CTX_ENEMY_WEAPON, 48,  0,  "Disarm",        "a5",  "Strip the weapon from the enemy.", DOMAIN_TRICKERY, ACT_CAT_COMBAT,                    {0} },
    { ACTION_BACKSTAB, ACT_CTX_FIRST_TURN,   60,  6,  "Moonstep",      "a6",  "Strike first. Skip the counter.", DOMAIN_TRICKERY, ACT_CAT_COMBAT,                    {0} },
    { ACTION_STUN,     ACT_CTX_CAN_STUN,     42,  0,  "Stun",          "a7",  "Stun the enemy, skip their turn.", DOMAIN_COMBAT,   ACT_CAT_COMBAT,                    {0} },
    { ACTION_CALM,     0,                    22,  0,  "Persuade",      "a8",  "Persuade the enemy, lower aggro.", DOMAIN_CHARM,    ACT_CAT_COMBAT|ACT_CAT_SOCIAL,     {0} },
    { ACTION_HIDE,     0,                    20,  0,  "Blindspot",     "a9",  "Find cover. Avoid the counter.",   DOMAIN_TRICKERY, ACT_CAT_COMBAT,                    {0} },
    { ACTION_EXECUTE,  ACT_CTX_EXECUTABLE,   78, 15,  "Death star",    "a10", "Lethal blow on a weakened enemy.", DOMAIN_COMBAT,   ACT_CAT_COMBAT,                    {0} },
    /* Domain: Combat */
    { ACTION_INTIMIDATE, 0,                  45,  0,  "Intimidate",    "a11", "Lower enemy attack, skip ctr.",    DOMAIN_COMBAT,   ACT_CAT_COMBAT|ACT_CAT_SOCIAL,     {0} },
    { ACTION_COUNTER,    ACT_CTX_PLAYER_HURT,65,  8,  "Counter",       "a12", "Strike hard while wounded.",       DOMAIN_COMBAT,   ACT_CAT_COMBAT,                    {0} },
    { ACTION_WAR_CRY,    0,                  35,  0,  "War Cry",       "a13", "A fierce cry staggers the foe.",   DOMAIN_COMBAT,   ACT_CAT_COMBAT,                    {0} },
    { ACTION_THREATEN,   0,                  30,  0,  "Threaten",      "a14", "Force the threat of violence.",    DOMAIN_COMBAT,   ACT_CAT_SOCIAL,                    {0} },
    { ACTION_AMBUSH,     ACT_CTX_FIRST_TURN, 55,  8,  "Ambush",        "a15", "Strike from cover, skip ctr.",     DOMAIN_COMBAT,   ACT_CAT_HUNT,                      {0} },
    /* Domain: Trickery */
    { ACTION_VANISH,     0,                  35,  0,  "Vanish",        "a16", "Disappear, skip enemy attack.",    DOMAIN_TRICKERY, ACT_CAT_COMBAT,                    {0} },
    { ACTION_POISON,     0,                  40,  3,  "Poison Blade",  "a17", "Coat the blade in slow toxin.",    DOMAIN_TRICKERY, ACT_CAT_COMBAT,                    {0} },
    { ACTION_SET_TRAP,   0,                  38,  5,  "Set Trap",      "a18", "Lay a trap, skip next attack.",    DOMAIN_TRICKERY, ACT_CAT_COMBAT|ACT_CAT_HUNT,       {0} },
    { ACTION_DECEIVE,    0,                  45,  0,  "Deceive",       "a19", "Spin a web of lies, escape.",      DOMAIN_TRICKERY, ACT_CAT_SOCIAL,                    {0} },
    { ACTION_PICKPOCKET, 0,                  30,  0,  "Pickpocket",    "a20", "Lift gold from their pocket.",     DOMAIN_TRICKERY, ACT_CAT_SOCIAL,                    {0} },
    { ACTION_INSPECT,    0,                  40,  0,  "Inspect",       "a21", "Study weakness, cut defense.",     DOMAIN_TRICKERY, ACT_CAT_INVESTIGATION,             {0} },
    { ACTION_TRACK,      ACT_CTX_FIRST_TURN, 50,  4,  "Track",         "a22", "Follow the trail, first blow.",    DOMAIN_TRICKERY, ACT_CAT_HUNT,                      {0} },
    /* Domain: Blood */
    { ACTION_BLOOD_DRAIN,ACT_CTX_REQUIRES_DARK,55,6,  "Blood Drain",   "a23", "Drain blood. Restore your HP.",    DOMAIN_BLOOD,    ACT_CAT_COMBAT,                    {0} },
    { ACTION_BITE,       ACT_CTX_BLOCKED_HOLY,65, 12, "Lethal Bite",   "a24", "Pierce deep. Massive damage.",     DOMAIN_BLOOD,    ACT_CAT_COMBAT,                    {0} },
    { ACTION_BLOOD_HOWL, 0,                  40,  5,  "Blood Howl",    "a25", "Howl and strike every enemy.",     DOMAIN_BLOOD,    ACT_CAT_COMBAT,                    {0} },
    { ACTION_BLOOD_SURGE,0,                  30, 18,  "Blood Surge",   "a26", "Savage burst. Costs your HP.",     DOMAIN_BLOOD,    ACT_CAT_COMBAT,                    {0} },
    { ACTION_BLOOD_SCENT,0,                  50,  8,  "Blood Scent",   "a27", "Track wounds. Bonus on prey.",     DOMAIN_BLOOD,    ACT_CAT_HUNT,                      {0} },
    /* Domain: Charm */
    { ACTION_DOMINATE,   0,                  45,  0,  "Dominate",      "a28", "Bend their will, end the fight.",  DOMAIN_CHARM,    ACT_CAT_COMBAT|ACT_CAT_SOCIAL,     {0} },
    { ACTION_MESMERIZE,  ACT_CTX_CAN_STUN,   48,  0,  "Mesmerize",     "a29", "Trap the mind, stun and skip.",    DOMAIN_CHARM,    ACT_CAT_COMBAT,                    {0} },
    { ACTION_BRIBE,      0,                  35,  0,  "Bribe",         "a30", "Pay them off to end the fight.",   DOMAIN_CHARM,    ACT_CAT_SOCIAL,                    {0} },
    { ACTION_SILVER_TONGUE,0,                50,  0,  "Silver Tongue", "a31", "Words of silver end conflict.",    DOMAIN_CHARM,    ACT_CAT_SOCIAL,                    {0} },
    { ACTION_INTERROGATE,0,                  40,  0,  "Interrogate",   "a32", "Demand answers, cut defense.",     DOMAIN_CHARM,    ACT_CAT_INVESTIGATION,             {0} },
    /* Environmental */
    { ACTION_FEED,      ACT_CTX_REQUIRES_DARK,60, 15, "Feed",          "a33", "Feed on the dark to restore HP.",  DOMAIN_BLOOD,    ACT_CAT_ENVIRONMENTAL,             {0} },
    { ACTION_BLEND_IN,   0,                  35,  0,  "Blend In",      "a34", "Melt into shadow, skip hit.",      DOMAIN_TRICKERY, ACT_CAT_ENVIRONMENTAL,             {0} },
    { ACTION_RALLY,      0,                  40, 12,  "Rally",         "a35", "Gather resolve, restore HP.",      DOMAIN_COMBAT,   ACT_CAT_ENVIRONMENTAL,             {0} },
};
#define BUILTIN_COUNT (int)(sizeof(builtinDefs)/sizeof(builtinDefs[0]))

/* Format: [1 count][N × sizeof(ActionDef)] */
int loadActions(PakData data) {
    if (!data.data || data.size < 1) return 0;
    uint8_t n = ((const uint8_t *)data.data)[0];
    if (n > ACTION_MAX) n = ACTION_MAX;
    uint32_t expected = 1 + (uint32_t)n * sizeof(ActionDef);
    if (data.size < expected) return 0;
    memcpy(actionDefs, (const uint8_t *)data.data + 1, (size_t)n * sizeof(ActionDef));
    actionDefCount = n;
    return n;
}

const ActionDef *getActionDef(uint8_t id) {
    for (int i = 0; i < actionDefCount; i++)
        if (actionDefs[i].id == id) return &actionDefs[i];
    for (int i = 0; i < BUILTIN_COUNT; i++)
        if (builtinDefs[i].id == id) return &builtinDefs[i];
    return NULL;
}

uint8_t actionGetDomain(uint8_t id) {
    const ActionDef *def = getActionDef(id);
    return def ? def->domain : 0xFF;
}

int buildActionPool(uint8_t out[ACTION_MAX], uint8_t encounterCat) {
    int count = 0;

    #define ADD(id) do { \
        if ((id) == 0xFF) break; \
        const ActionDef *_def = getActionDef(id); \
        if (!_def) break; \
        if (encounterCat != ACT_CAT_ANY && _def->encounterCat != 0 \
            && !(_def->encounterCat & encounterCat)) break; \
        int dup = 0; \
        for (int _j = 0; _j < count; _j++) if (out[_j] == (id)) { dup = 1; break; } \
        if (!dup) out[count++] = (id); \
    } while (0)

    ADD(ACTION_ATTACK);

    /* Equipment-granted actions */
    for (int s = 0; s < EQUIP_SLOTS; s++) {
        if (player.equipped[s] == ITEM_UNEQUIPPED) continue;
        const ItemDef *d = itemGetDef(player.equipped[s]);
        if (!d) continue;
        for (int j = 0; j < 4; j++) ADD(d->actions[j]);
    }

    /* Domain node-unlocked actions */
    for (int di = 0; di < DOMAIN_COUNT; di++) {
        const Domain *dom = &domains[di];
        for (int ni = 0; ni < DOMAIN_NODE_MAX; ni++) {
            const DomainNode *node = &dom->nodes[ni];
            if (node->name[0] == '\0') break; /* end of defined nodes */
            if (node->type != NODE_REWARD_ACTION) continue;
            if (!domainNodeUnlocked((uint8_t)di, (uint8_t)ni)) continue;
            ADD((uint8_t)node->reward_value);
        }
    }

    #undef ADD
    return count;
}

void renderActionPanel(const char *title, const uint8_t *ids, int count, int sel) {
    const int PW   = 300;
    const int LH   = 22;
    const int PH   = 56 + count * LH + 48;
    const int PX   = gfxWidth  / 2 - PW / 2;
    const int PY   = gfxHeight / 2 - PH / 2;
    const int TX   = PX + 16;

    fillRect(PX,        PY,       PW, PH,  rgb(8,  14, 30));
    fillRect(PX,        PY,       PW,  1,  rgb(80, 120, 200));
    fillRect(PX,        PY+PH-1,  PW,  1,  rgb(80, 120, 200));
    fillRect(PX,        PY,        1, PH,  rgb(80, 120, 200));
    fillRect(PX+PW-1,   PY,        1, PH,  rgb(80, 120, 200));

    drawText(TX, PY + 10, title, rgb(200, 220, 255), 2);
    fillRect(PX + 8, PY + 32, PW - 16, 1, rgb(50, 70, 130));

    int y = PY + 40;
    for (int i = 0; i < count; i++) {
        const ActionDef *a = getActionDef(ids[i]);
        if (!a) continue;
        int s = (i == sel);
        char buf[20];
        buf[0] = s ? '>' : ' '; buf[1] = ' ';
        int len = 0; while (a->name[len] && len < 16) len++;
        for (int c = 0; c < len; c++) buf[2 + c] = a->name[c];
        buf[2 + len] = '\0';
        uint32_t col = s ? rgb(255, 255, 100) : rgb(160, 180, 220);
        drawText(TX, y, buf, col, 2);
        y += LH;
    }

    fillRect(PX + 8, y + 4, PW - 16, 1, rgb(50, 70, 130));
    y += 12;
    if (sel >= 0 && sel < count) {
        const ActionDef *a = getActionDef(ids[sel]);
        if (a && a->desc[0])
            drawText(TX, y, a->desc, rgb(140, 170, 210), 1);
    }

    drawText(TX, PY + PH - 14, "UP/DN  A/ESC: close", rgb(50, 60, 90), 1);
}
