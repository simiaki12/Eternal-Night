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

/* Dense transition matrix: progress[from][to], 0 = no route. */
#define GRAPH_STATES 6
#define ENC_TYPE_COUNT 5
typedef struct { uint8_t progress[GRAPH_STATES][GRAPH_STATES]; } TransMatrix;

#define ENC_IDX_COMBAT        0
#define ENC_IDX_SOCIAL        1
#define ENC_IDX_INVESTIGATION 2
#define ENC_IDX_HUNT          3
#define ENC_IDX_ENVIRONMENTAL 4

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
    uint8_t  graphMask;       /* derived at write time from mats[] */
    Effect   onPlay;
    Effect   fallback;
    TransMatrix mats[ENC_TYPE_COUNT];
} ActionDef; /* 249 bytes in memory; only used graphs are written */

typedef char _check_size[(sizeof(ActionDef) == 249) ? 1 : -1];
#define ACT_DISK_HEAD 69
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

/* Environmental graph states (must match seed_states.c) */
#define ES_STABLE     0
#define ES_ESCALATING 1
#define ES_CRITICAL   2
#define ES_RESOLVED   3
#define ES_DISASTER   4

/* Combat graph states (must match seed_states.c) */
#define CS_SQUARE 0
#define CS_TRADE  1
#define CS_STAG   2
#define CS_FRENZY 3
#define CS_BROKEN 4

/* columns: id  ctx  wt  name  img  desc  domain  cat  flags  [graph suffix] */

/* Graph suffix: graphMask (filled in at write time), onPlay, fallback, mats.
   A matrix row reads "from this state, these destinations and how much
   progress each banks" — R(from, [to]=n, ...). Anything not named is 0,
   meaning that route cannot be taken from that state. */
#define FX(t, v, c) { t, v, c }
#define NFX         { 0, 0, 0 }
#define R(from, ...) [from] = { __VA_ARGS__ }
#define M(graph, ...) [graph] = { { __VA_ARGS__ } }
#define G(op, fb, ...) 0, op, fb, { __VA_ARGS__ }
#define NOGRAPH        0, NFX, NFX, { { { { 0 } } } }
#define ONPLAY(t, v, c) 0, FX(t, v, c), NFX, { { { { 0 } } } }

static const ActionDef defaults[] = {
    /* Starters — available without any unlock */
    { 0, 0,                    70,   "Slash",        "a1",  "A quick, reliable strike.",        DOMAIN_COMBAT,   ACT_CAT_COMBAT,                ACT_FLAG_STARTER,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_SQUARE, [CS_TRADE]=60), R(CS_TRADE, [CS_STAG]=20))) },
    { 7, 0,                    22,   "Persuade",     "a8",  "Persuade the enemy, lower aggro.", DOMAIN_CHARM,    ACT_CAT_COMBAT|ACT_CAT_SOCIAL, ACT_FLAG_STARTER,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_FRENZY, [CS_TRADE]=45)),
             M(ENC_IDX_SOCIAL, R(SS_STRANGER, [SS_TRUST]=40), R(SS_SUSP, [SS_TRUST]=35))) },
    /* Domain: Combat */
    { 1, 0,                    35,   "Strong Attack", "a2",  "Heavy blow dealing bonus damage.", DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0,
        G(NFX, FX(EFX_PROGRESS_NEXT,15,100), M(ENC_IDX_COMBAT, R(CS_TRADE, [CS_STAG]=45), R(CS_STAG, [CS_BROKEN]=35))) },
    { 3, 0,                    28,   "Parry",        "a4",  "Brace and blunt their assault.",   DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0,
        G(FX(EFX_APPLY_STATUS,STATUS_GUARDED,100), NFX, { { { 0 } } }) },
    { 6, ACT_CTX_CAN_STUN,     42,   "Stun",         "a7",  "Rattle them off their footing.",   DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_TRADE, [CS_STAG]=50))) },
    { 10, 0,                   45,   "Intimidate",   "a11", "Unnerve them before blows land.",  DOMAIN_COMBAT,   ACT_CAT_COMBAT|ACT_CAT_SOCIAL, 0,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_SQUARE, [CS_STAG]=35)),
             M(ENC_IDX_SOCIAL, R(SS_STRANGER, [SS_FEAR]=50), R(SS_HOSTILE, [SS_FEAR]=40))) },
    { 11, ACT_CTX_PLAYER_HURT, 65,   "Counter",      "a12", "Strike hard while wounded.",       DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_TRADE, [CS_STAG]=55), R(CS_STAG, [CS_BROKEN]=40))) },
    { 12, 0,                   35,   "War Cry",      "a13", "A fierce cry staggers the foe.",   DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0,
        G(FX(EFX_APPLY_STATUS,STATUS_RAGE,60), NFX, M(ENC_IDX_COMBAT, R(CS_TRADE, [CS_STAG]=40))) },
    { 13, 0,                   30,   "Threaten",     "a14", "Force the threat of violence.",    DOMAIN_COMBAT,   ACT_CAT_SOCIAL,                0,
        G(NFX, NFX, M(ENC_IDX_SOCIAL, R(SS_STRANGER, [SS_FEAR]=55), R(SS_SUSP, [SS_FEAR]=40), R(SS_TRUST, [SS_FEAR]=45))) },
    { 14, 0,                   55,   "Ambush",       "a15", "Strike from cover. One less.",     DOMAIN_COMBAT,   ACT_CAT_HUNT,                  0,
        G(FX(EFX_KILL,1,100), NFX, M(ENC_IDX_HUNT, R(HS_DISTURBED, [HS_TERRIFIED]=55), R(HS_ALERTED, [HS_BROKEN]=30), R(HS_PREPARING, [HS_BROKEN]=35))) },
    /* Domain: Trickery */
    { 4, ACT_CTX_ENEMY_WEAPON, 48,   "Disarm",       "a5",  "Strip the weapon from the enemy.", DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_TRADE, [CS_STAG]=45))) },
    { 5, ACT_CTX_FIRST_TURN,   60,   "Moonstep",     "a6",  "Open from the shadows.",           DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_SQUARE, [CS_STAG]=55))) },
    { 8, 0,                    20,   "Blindspot",    "a9",  "Slip back out of their reach.",    DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_TRADE, [CS_SQUARE]=40))) },
    { 15, 0,                   35,   "Vanish",       "a16", "Disappear; reset the exchange.",   DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_TRADE, [CS_SQUARE]=50))) },
    { 16, 0,                   40,   "Poison Blade", "a17", "Coat the blade in slow toxin.",    DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_TRADE, [CS_STAG]=35))) },
    { 17, 0,                   38,   "Set Trap",     "a18", "Lay a trap for the opening.",      DOMAIN_TRICKERY, ACT_CAT_COMBAT|ACT_CAT_HUNT,   0,
        G(FX(EFX_KILL,1,60), NFX, M(ENC_IDX_COMBAT, R(CS_SQUARE, [CS_STAG]=40)),
             M(ENC_IDX_HUNT, R(HS_ALERTED, [HS_TERRIFIED]=45), R(HS_PREPARING, [HS_ALERTED]=45))) },
    { 18, 0,                   45,   "Deceive",      "a19", "Spin a web of lies, escape.",      DOMAIN_TRICKERY, ACT_CAT_SOCIAL,                0,
        G(NFX, NFX, M(ENC_IDX_SOCIAL, R(SS_SUSP, [SS_TRUST]=40), R(SS_HOSTILE, [SS_SUSP]=35))) },
    { 19, 0,                   30,   "Pickpocket",   "a20", "Lift gold from their pocket.",     DOMAIN_TRICKERY, ACT_CAT_SOCIAL,                0,
        NOGRAPH },
    { 20, 0,                   40, "Inspect",      "a21", "Read the scene. Piece it out.",    DOMAIN_TRICKERY, ACT_CAT_INVESTIGATION,         0,
        G(NFX, NFX, M(ENC_IDX_INVESTIGATION, R(IS_COLD, [IS_FAMILIARIZE]=50), R(IS_FAMILIARIZE, [IS_CONNECTING]=35), R(IS_CONNECTING, [IS_BREAKTHROUGH]=30))) },
    { 21, 0,                   50,   "Track",        "a22", "Read the ground, close in.",       DOMAIN_TRICKERY, ACT_CAT_HUNT,                  0,
        G(NFX, NFX, M(ENC_IDX_INVESTIGATION, R(IS_COLD, [IS_FAMILIARIZE]=40)),
             M(ENC_IDX_HUNT, R(HS_ORGANIZED, [HS_DISTURBED]=50), R(HS_ALERTED, [HS_DISTURBED]=35))) },
    /* Domain: Blood */
    { 2, ACT_CTX_PLAYER_HURT,  55,  "Regenerate",   "a3",  "Draw on vitality to restore HP.",  DOMAIN_BLOOD,    ACT_CAT_COMBAT,                0,
        G(FX(EFX_HEAL_HP,10,100), NFX, { { { 0 } } }) },
    { 22, ACT_CTX_REQUIRES_DARK,55,  "Blood Drain",  "a23", "Drain blood. Restore your HP.",    DOMAIN_BLOOD,    ACT_CAT_COMBAT,                0,
        G(FX(EFX_HEAL_HP,6,100), NFX, M(ENC_IDX_COMBAT, R(CS_TRADE, [CS_STAG]=40))) },
    { 23, ACT_CTX_BLOCKED_HOLY,65,  "Lethal Bite",  "a24", "Pierce deep. Finish the reeling.", DOMAIN_BLOOD,    ACT_CAT_COMBAT,                0,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_STAG, [CS_BROKEN]=60))) },
    { 24, 0,                   40,   "Blood Howl",   "a25", "A howl that rocks them all.",      DOMAIN_BLOOD,    ACT_CAT_COMBAT,                ACT_FLAG_ALL_TARGETS,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_SQUARE, [CS_TRADE]=50), R(CS_FRENZY, [CS_STAG]=40))) },
    { 25, 0,                   30,  "Blood Surge",  "a26", "Savage burst. Costs your HP.",     DOMAIN_BLOOD,    ACT_CAT_COMBAT,                0,
        G(FX(EFX_DAMAGE_HP,8,100), NFX, M(ENC_IDX_COMBAT, R(CS_TRADE, [CS_STAG]=60))) },
    { 26, 0,                   50,   "Blood Scent",  "a27", "Follow the blood. Cut one off.",   DOMAIN_BLOOD,    ACT_CAT_HUNT,                  0,
        G(NFX, NFX, M(ENC_IDX_INVESTIGATION, R(IS_CONNECTING, [IS_BREAKTHROUGH]=30)),
             M(ENC_IDX_HUNT, R(HS_ORGANIZED, [HS_TERRIFIED]=35), R(HS_DISTURBED, [HS_BROKEN]=30))) },
    /* Domain: Charm */
    { 27, 0,                   45,   "Dominate",     "a28", "Bend their will, calm the storm.", DOMAIN_CHARM,    ACT_CAT_COMBAT|ACT_CAT_SOCIAL, 0,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_TRADE, [CS_STAG]=30), R(CS_FRENZY, [CS_TRADE]=50)),
             M(ENC_IDX_SOCIAL, R(SS_HOSTILE, [SS_FEAR]=50))) },
    { 28, ACT_CTX_CAN_STUN,    48,   "Mesmerize",    "a29", "Trap the mind mid-swing.",         DOMAIN_CHARM,    ACT_CAT_COMBAT,                0,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_TRADE, [CS_STAG]=50))) },
    { 29, 0,                   35,   "Bribe",        "a30", "Coin loosens every tongue.",       DOMAIN_CHARM,    ACT_CAT_SOCIAL,                0,
        G(FX(EFX_METER,15,100), NFX, M(ENC_IDX_SOCIAL, R(SS_STRANGER, [SS_GREED]=50), R(SS_SUSP, [SS_GREED]=45))) },
    { 30, 0,                   50,   "Silver Tongue","a31", "Words of silver end conflict.",    DOMAIN_CHARM,    ACT_CAT_SOCIAL,                0,
        G(NFX, NFX, M(ENC_IDX_SOCIAL, R(SS_STRANGER, [SS_TRUST]=50), R(SS_SUSP, [SS_TRUST]=45), R(SS_FEAR, [SS_TRUST]=40))) },
    { 31, 0,                   40, "Interrogate",  "a32", "Press them. Names, times, faces.", DOMAIN_CHARM,    ACT_CAT_INVESTIGATION,         0,
        G(NFX, NFX, M(ENC_IDX_INVESTIGATION, R(IS_FAMILIARIZE, [IS_CONNECTING]=45), R(IS_CONNECTING, [IS_BREAKTHROUGH]=35), R(IS_BREAKTHROUGH, [IS_UNRAVELED]=40))) },
    /* Environmental */
    { 9, ACT_CTX_EXECUTABLE,   78,  "Death star",   "a10", "Lethal blow on a staggered foe.",  DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0,
        G(NFX, NFX, M(ENC_IDX_COMBAT, R(CS_STAG, [CS_BROKEN]=80))) },
    { 32, ACT_CTX_REQUIRES_DARK,60, "Feed",         "a33", "Feed on the dark to restore HP.",  DOMAIN_BLOOD,    ACT_CAT_ENVIRONMENTAL,         0,
        ONPLAY(EFX_HEAL_HP, 12, 100) },
    { 33, 0,                   35,   "Blend In",     "a34", "Melt into shadow, skip hit.",      DOMAIN_TRICKERY, ACT_CAT_ENVIRONMENTAL,         0,
        NOGRAPH },
    { 34, 0,                   40,  "Rally",        "a35", "Gather resolve, restore HP.",      DOMAIN_COMBAT,   ACT_CAT_ENVIRONMENTAL,         0,
        ONPLAY(EFX_HEAL_HP, 10, 100) },
    /* Special: always injected into social encounters */
    { 35, 0,                    0,   "Demand",       "",    "Force the issue. End it now.",     DOMAIN_NONE,     ACT_CAT_SOCIAL,                0,
        NOGRAPH },
    /* Hunt special */
    { 36, ACT_CTX_ROUTED,      55,  "Massacre",     "a37", "They are running. No mercy.",      DOMAIN_BLOOD,    ACT_CAT_HUNT,                  0,
        G(FX(EFX_KILL,3,100), NFX, M(ENC_IDX_HUNT, R(HS_TERRIFIED, [HS_BROKEN]=70))) },
};

/* ACTION_COUNT = 37 (includes Massacre at id 36) */
/* A graph is "used" if any cell in its matrix is non-zero — derived here so
   there is no per-type count to keep in sync with the data by hand. */
static uint8_t deriveGraphMask(const ActionDef *a) {
    uint8_t mask = 0;
    for (int t = 0; t < ENC_TYPE_COUNT; t++)
        for (int i = 0; i < GRAPH_STATES && !((mask >> t) & 1); i++)
            for (int j = 0; j < GRAPH_STATES; j++)
                if (a->mats[t].progress[i][j]) { mask |= (uint8_t)(1u << t); break; }
    return mask;
}

int main(void) {
    FILE *f = fopen("assets/data/actions.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot open assets/data/actions.dat\n"); return 1; }
    uint8_t n = (uint8_t)(sizeof(defaults) / sizeof(defaults[0]));
    fwrite(&n, 1, 1, f);

    long bytes = 1;
    for (int i = 0; i < n; i++) {
        ActionDef a = defaults[i];
        a.graphMask = deriveGraphMask(&a);
        fwrite(&a, 1, ACT_DISK_HEAD, f);
        bytes += ACT_DISK_HEAD;
        for (int t = 0; t < ENC_TYPE_COUNT; t++) {
            if (!((a.graphMask >> t) & 1)) continue;
            fwrite(&a.mats[t], 1, sizeof(TransMatrix), f);
            bytes += sizeof(TransMatrix);
        }
    }
    fclose(f);
    printf("Wrote %d actions (%ld bytes) to assets/data/actions.dat\n", n, bytes);
    return 0;
}
