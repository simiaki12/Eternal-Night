/* Writes the default action table to assets/data/actions.dat */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ---- mirror of src/gameplay/actions.h and domains.h (keep in sync) ---- */
#define ACT_CTX_FIRST_TURN    (1<<0)
#define ACT_CTX_ENEMY_WEAPON  (1<<1)
#define ACT_CTX_EXECUTABLE    (1<<2)
#define ACT_CTX_CAN_STUN      (1<<3)
#define ACT_CTX_PLAYER_HURT   (1<<4)
#define ACT_CTX_REQUIRES_DARK (1<<5)
#define ACT_CTX_BLOCKED_HOLY  (1<<6)
#define ACT_CTX_ROUTED        (1<<7)

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

/* Action flags */
#define ACT_FLAG_STARTER     (1<<0)
#define ACT_FLAG_ALL_TARGETS (1<<1)

/* Social graph state ids (must match seed_states.c) */
#define SS_STRANGER 0
#define SS_SUSP     1
#define SS_FEAR     2
#define SS_TRUST    3
#define SS_HOSTILE  4
#define SS_GREED    5

typedef struct { uint8_t type, value, chance; } Effect;
#define TRANSITION_SOURCES 8
typedef struct { uint8_t to; uint8_t progress[TRANSITION_SOURCES]; } Transition;

typedef struct {
    uint8_t  id;
    uint8_t  contextFlags;
    uint8_t  baseWeight;
    char     name[16];
    char     imgName[8];
    char     desc[32];
    uint8_t  domain;
    uint8_t  encounterCat;
    uint8_t  actionFlags;
    Transition transitions[3];
    Effect     onPlay;
    Effect     fallback;
    uint16_t   tCounts;       /* 3 bits per encounter-type index */
} ActionDef; /* 98 bytes */

typedef char _check_size[(sizeof(ActionDef) == 98) ? 1 : -1];
/* ----------------------------------------------------------------------- */

/* Effect types (mirror of effects.h) */
#define EFX_PROGRESS_NEXT 2
#define EFX_HEAL_HP       3
#define EFX_DAMAGE_HP     4
#define EFX_APPLY_STATUS  6
#define EFX_METER         8

/* Status ids (must match seed_statuses.c) */
#define STATUS_RAGE    0
#define STATUS_GUARDED 6

/* Investigation graph states — the case arc (must match seed_states.c) */
#define IS_COLD         0
#define IS_FAMILIARIZE  1
#define IS_CONNECTING   2
#define IS_BREAKTHROUGH 3
#define IS_UNRAVELED    4

/* Hunt graph states (must match seed_states.c) */
#define HS_ORGANIZED 0
#define HS_DISTURBED 1
#define HS_ALERTED   2
#define HS_TERRIFIED 3
#define HS_BROKEN    4
#define HS_PREPARING 5
#define EFX_KILL     9

/* Combat graph states (must match seed_states.c) */
#define CS_SQUARE 0
#define CS_TRADE  1
#define CS_STAG   2
#define CS_FRENZY 3
#define CS_BROKEN 4

/* columns: id  ctx  wt  name  img  desc  domain  cat  flags  [graph suffix] */

/* State-graph suffix: 3 transitions, onPlay, fallback, tCounts.
   Rows without a suffix zero-fill: no transitions, no effects. */
/* A transition = one destination + how much progress each source state
   banks toward it. Sources left out simply cannot take that route. */
#define TR(dest, ...) { dest, { __VA_ARGS__ } }
#define NTR           { 0, { 0 } }
#define FX(t, v, c) { t, v, c }
#define NFX         { 0, 0, 0 }
#define TC(c, s, i, h, e) (uint16_t)((c) | ((s) << 3) | ((i) << 6) | ((h) << 9) | ((e) << 12))
#define G(t1, t2, t3, op, fb, tc) { t1, t2, t3 }, op, fb, tc, 0

/* zeroed transitions + an onPlay effect (fallback/tCounts zero-fill) */
#define ONPLAY(t, v, c)  {NTR,NTR,NTR}, { t, v, c }, {0,0,0}, 0, 0
static const ActionDef defaults[] = {
    /* Starters — available without any unlock */
    { 0, 0,                    70,   "Slash",        "a1",  "A quick, reliable strike.",        DOMAIN_COMBAT,   ACT_CAT_COMBAT,                ACT_FLAG_STARTER,
        G(TR(CS_TRADE, [CS_SQUARE]=60), TR(CS_STAG, [CS_TRADE]=20), NTR, NFX, NFX, TC(2,0,0,0,0)) },
    { 7, 0,                    22,   "Persuade",     "a8",  "Persuade the enemy, lower aggro.", DOMAIN_CHARM,    ACT_CAT_COMBAT|ACT_CAT_SOCIAL, ACT_FLAG_STARTER,
        G(TR(CS_TRADE, [CS_FRENZY]=45), TR(SS_TRUST, [SS_STRANGER]=40, [SS_SUSP]=35), NTR, NFX, NFX, TC(1,1,0,0,0)) },
    /* Domain: Combat */
    { 1, 0,                    35,   "Strong Attack", "a2",  "Heavy blow dealing bonus damage.", DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0,
        G(TR(CS_STAG, [CS_TRADE]=45), TR(CS_BROKEN, [CS_STAG]=35), NTR, NFX, FX(EFX_PROGRESS_NEXT,15,100), TC(2,0,0,0,0)) },
    { 3, 0,                    28,   "Parry",        "a4",  "Brace and blunt their assault.",   DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0,
        G(NTR, NTR, NTR, FX(EFX_APPLY_STATUS,STATUS_GUARDED,100), NFX, TC(0,0,0,0,0)) },
    { 6, ACT_CTX_CAN_STUN,     42,   "Stun",         "a7",  "Rattle them off their footing.",   DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0,
        G(TR(CS_STAG, [CS_TRADE]=50), NTR, NTR, NFX, NFX, TC(1,0,0,0,0)) },
    { 10, 0,                   45,   "Intimidate",   "a11", "Unnerve them before blows land.",  DOMAIN_COMBAT,   ACT_CAT_COMBAT|ACT_CAT_SOCIAL, 0,
        G(TR(CS_STAG, [CS_SQUARE]=35), TR(SS_FEAR, [SS_STRANGER]=50, [SS_HOSTILE]=40), NTR, NFX, NFX, TC(1,1,0,0,0)) },
    { 11, ACT_CTX_PLAYER_HURT, 65,   "Counter",      "a12", "Strike hard while wounded.",       DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0,
        G(TR(CS_STAG, [CS_TRADE]=55), TR(CS_BROKEN, [CS_STAG]=40), NTR, NFX, NFX, TC(2,0,0,0,0)) },
    { 12, 0,                   35,   "War Cry",      "a13", "A fierce cry staggers the foe.",   DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0,
        G(TR(CS_STAG, [CS_TRADE]=40), NTR, NTR, FX(EFX_APPLY_STATUS,STATUS_RAGE,60), NFX, TC(1,0,0,0,0)) },
    { 13, 0,                   30,   "Threaten",     "a14", "Force the threat of violence.",    DOMAIN_COMBAT,   ACT_CAT_SOCIAL,                0,
        G(TR(SS_FEAR, [SS_STRANGER]=55, [SS_TRUST]=45, [SS_SUSP]=40), NTR, NTR, NFX, NFX, TC(0,1,0,0,0)) },
    { 14, 0,                   55,   "Ambush",       "a15", "Strike from cover. One less.",     DOMAIN_COMBAT,   ACT_CAT_HUNT,                  0,
        G(TR(HS_TERRIFIED, [HS_DISTURBED]=55), TR(HS_BROKEN, [HS_ALERTED]=30, [HS_PREPARING]=35), NTR, FX(EFX_KILL,1,100), NFX, TC(0,0,0,2,0)) },
    /* Domain: Trickery */
    { 4, ACT_CTX_ENEMY_WEAPON, 48,   "Disarm",       "a5",  "Strip the weapon from the enemy.", DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0,
        G(TR(CS_STAG, [CS_TRADE]=45), NTR, NTR, NFX, NFX, TC(1,0,0,0,0)) },
    { 5, ACT_CTX_FIRST_TURN,   60,   "Moonstep",     "a6",  "Open from the shadows.",           DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0,
        G(TR(CS_STAG, [CS_SQUARE]=55), NTR, NTR, NFX, NFX, TC(1,0,0,0,0)) },
    { 8, 0,                    20,   "Blindspot",    "a9",  "Slip back out of their reach.",    DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0,
        G(TR(CS_SQUARE, [CS_TRADE]=40), NTR, NTR, NFX, NFX, TC(1,0,0,0,0)) },
    { 15, 0,                   35,   "Vanish",       "a16", "Disappear; reset the exchange.",   DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0,
        G(TR(CS_SQUARE, [CS_TRADE]=50), NTR, NTR, NFX, NFX, TC(1,0,0,0,0)) },
    { 16, 0,                   40,   "Poison Blade", "a17", "Coat the blade in slow toxin.",    DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0,
        G(TR(CS_STAG, [CS_TRADE]=35), NTR, NTR, NFX, NFX, TC(1,0,0,0,0)) },
    { 17, 0,                   38,   "Set Trap",     "a18", "Lay a trap for the opening.",      DOMAIN_TRICKERY, ACT_CAT_COMBAT|ACT_CAT_HUNT,   0,
        G(TR(CS_STAG, [CS_SQUARE]=40), TR(HS_TERRIFIED, [HS_ALERTED]=45), TR(HS_ALERTED, [HS_PREPARING]=45), FX(EFX_KILL,1,60), NFX, TC(1,0,0,2,0)) },
    { 18, 0,                   45,   "Deceive",      "a19", "Spin a web of lies, escape.",      DOMAIN_TRICKERY, ACT_CAT_SOCIAL,                0,
        G(TR(SS_TRUST, [SS_SUSP]=40), TR(SS_SUSP, [SS_HOSTILE]=35), NTR, NFX, NFX, TC(0,2,0,0,0)) },
    { 19, 0,                   30,   "Pickpocket",   "a20", "Lift gold from their pocket.",     DOMAIN_TRICKERY, ACT_CAT_SOCIAL,                0 },
    { 20, 0,                   40, "Inspect",      "a21", "Read the scene. Piece it out.",    DOMAIN_TRICKERY, ACT_CAT_INVESTIGATION,         0,
        G(TR(IS_FAMILIARIZE, [IS_COLD]=50), TR(IS_CONNECTING, [IS_FAMILIARIZE]=35), TR(IS_BREAKTHROUGH, [IS_CONNECTING]=30), NFX, NFX, TC(0,0,3,0,0)) },
    { 21, 0,                   50,   "Track",        "a22", "Read the ground, close in.",       DOMAIN_TRICKERY, ACT_CAT_HUNT,                  0,
        G(TR(IS_FAMILIARIZE, [IS_COLD]=40), TR(HS_DISTURBED, [HS_ORGANIZED]=50, [HS_ALERTED]=35), NTR, NFX, NFX, TC(0,0,1,1,0)) },
    /* Domain: Blood */
    { 2, ACT_CTX_PLAYER_HURT,  55,  "Regenerate",   "a3",  "Draw on vitality to restore HP.",  DOMAIN_BLOOD,    ACT_CAT_COMBAT,                0,
        G(NTR, NTR, NTR, FX(EFX_HEAL_HP,10,100), NFX, TC(0,0,0,0,0)) },
    { 22, ACT_CTX_REQUIRES_DARK,55,  "Blood Drain",  "a23", "Drain blood. Restore your HP.",    DOMAIN_BLOOD,    ACT_CAT_COMBAT,                0,
        G(TR(CS_STAG, [CS_TRADE]=40), NTR, NTR, FX(EFX_HEAL_HP,6,100), NFX, TC(1,0,0,0,0)) },
    { 23, ACT_CTX_BLOCKED_HOLY,65,  "Lethal Bite",  "a24", "Pierce deep. Finish the reeling.", DOMAIN_BLOOD,    ACT_CAT_COMBAT,                0,
        G(TR(CS_BROKEN, [CS_STAG]=60), NTR, NTR, NFX, NFX, TC(1,0,0,0,0)) },
    { 24, 0,                   40,   "Blood Howl",   "a25", "A howl that rocks them all.",      DOMAIN_BLOOD,    ACT_CAT_COMBAT,                ACT_FLAG_ALL_TARGETS,
        G(TR(CS_TRADE, [CS_SQUARE]=50), TR(CS_STAG, [CS_FRENZY]=40), NTR, NFX, NFX, TC(2,0,0,0,0)) },
    { 25, 0,                   30,  "Blood Surge",  "a26", "Savage burst. Costs your HP.",     DOMAIN_BLOOD,    ACT_CAT_COMBAT,                0,
        G(TR(CS_STAG, [CS_TRADE]=60), NTR, NTR, FX(EFX_DAMAGE_HP,8,100), NFX, TC(1,0,0,0,0)) },
    { 26, 0,                   50,   "Blood Scent",  "a27", "Follow the blood. Cut one off.",   DOMAIN_BLOOD,    ACT_CAT_HUNT,                  0,
        G(TR(IS_BREAKTHROUGH, [IS_CONNECTING]=30), TR(HS_TERRIFIED, [HS_ORGANIZED]=35), TR(HS_BROKEN, [HS_DISTURBED]=30), NFX, NFX, TC(0,0,1,2,0)) },
    /* Domain: Charm */
    { 27, 0,                   45,   "Dominate",     "a28", "Bend their will, calm the storm.", DOMAIN_CHARM,    ACT_CAT_COMBAT|ACT_CAT_SOCIAL, 0,
        G(TR(CS_TRADE, [CS_FRENZY]=50), TR(CS_STAG, [CS_TRADE]=30), TR(SS_FEAR, [SS_HOSTILE]=50), NFX, NFX, TC(2,1,0,0,0)) },
    { 28, ACT_CTX_CAN_STUN,    48,   "Mesmerize",    "a29", "Trap the mind mid-swing.",         DOMAIN_CHARM,    ACT_CAT_COMBAT,                0,
        G(TR(CS_STAG, [CS_TRADE]=50), NTR, NTR, NFX, NFX, TC(1,0,0,0,0)) },
    { 29, 0,                   35,   "Bribe",        "a30", "Coin loosens every tongue.",       DOMAIN_CHARM,    ACT_CAT_SOCIAL,                0,
        G(TR(SS_GREED, [SS_STRANGER]=50, [SS_SUSP]=45), NTR, NTR, FX(EFX_METER,15,100), NFX, TC(0,1,0,0,0)) },
    { 30, 0,                   50,   "Silver Tongue","a31", "Words of silver end conflict.",    DOMAIN_CHARM,    ACT_CAT_SOCIAL,                0,
        G(TR(SS_TRUST, [SS_STRANGER]=50, [SS_SUSP]=45, [SS_FEAR]=40), NTR, NTR, NFX, NFX, TC(0,1,0,0,0)) },
    { 31, 0,                   40, "Interrogate",  "a32", "Press them. Names, times, faces.", DOMAIN_CHARM,    ACT_CAT_INVESTIGATION,         0,
        G(TR(IS_CONNECTING, [IS_FAMILIARIZE]=45), TR(IS_BREAKTHROUGH, [IS_CONNECTING]=35), TR(IS_UNRAVELED, [IS_BREAKTHROUGH]=40), NFX, NFX, TC(0,0,3,0,0)) },
    /* Environmental */
    { 9, ACT_CTX_EXECUTABLE,   78,  "Death star",   "a10", "Lethal blow on a staggered foe.",  DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0,
        G(TR(CS_BROKEN, [CS_STAG]=80), NTR, NTR, NFX, NFX, TC(1,0,0,0,0)) },
    { 32, ACT_CTX_REQUIRES_DARK,60, "Feed",         "a33", "Feed on the dark to restore HP.",  DOMAIN_BLOOD,    ACT_CAT_ENVIRONMENTAL,         0,
        ONPLAY(EFX_HEAL_HP, 12, 100) },
    { 33, 0,                   35,   "Blend In",     "a34", "Melt into shadow, skip hit.",      DOMAIN_TRICKERY, ACT_CAT_ENVIRONMENTAL,         0 },
    { 34, 0,                   40,  "Rally",        "a35", "Gather resolve, restore HP.",      DOMAIN_COMBAT,   ACT_CAT_ENVIRONMENTAL,         0,
        ONPLAY(EFX_HEAL_HP, 10, 100) },
    /* Special: always injected into social encounters */
    { 35, 0,                    0,   "Demand",       "",    "Force the issue. End it now.",     DOMAIN_NONE,     ACT_CAT_SOCIAL,                0 },
    /* Hunt special */
    { 36, ACT_CTX_ROUTED,      55,  "Massacre",     "a37", "They are running. No mercy.",      DOMAIN_BLOOD,    ACT_CAT_HUNT,                  0,
        G(TR(HS_BROKEN, [HS_TERRIFIED]=70), NTR, NTR, FX(EFX_KILL,3,100), NFX, TC(0,0,0,1,0)) },
};

/* ACTION_COUNT = 37 (includes Massacre at id 36) */
int main(void) {
    FILE *f = fopen("assets/data/actions.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot open assets/data/actions.dat\n"); return 1; }
    uint8_t n = (uint8_t)(sizeof(defaults) / sizeof(defaults[0]));
    fwrite(&n, 1, 1, f);
    fwrite(defaults, sizeof(ActionDef), n, f);
    fclose(f);
    printf("Wrote %d actions to assets/data/actions.dat\n", n);
    return 0;
}
