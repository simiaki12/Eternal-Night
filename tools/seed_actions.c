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
#define ACT_FLAG_STARTER (1<<0)

typedef struct {
    uint8_t  id;
    uint8_t  contextFlags;
    uint8_t  baseWeight;
    uint8_t  power;
    char     name[16];
    char     imgName[8];
    char     desc[32];
    uint8_t  domain;
    uint8_t  encounterCat;
    uint8_t  actionFlags;
    uint8_t  _pad;
} ActionDef;

typedef char _check_size[(sizeof(ActionDef) == 64) ? 1 : -1];
/* ----------------------------------------------------------------------- */

/*                                                                                                               flags              pad */
static const ActionDef defaults[] = {
    /* Starters — available without any unlock */
    { 0, 0,                    70,  0, "Slash",        "a1",  "A quick, reliable strike.",        DOMAIN_COMBAT,   ACT_CAT_COMBAT,                ACT_FLAG_STARTER, 0 },
    { 7, 0,                    22,  0, "Persuade",     "a8",  "Persuade the enemy, lower aggro.", DOMAIN_CHARM,    ACT_CAT_COMBAT|ACT_CAT_SOCIAL, ACT_FLAG_STARTER, 0 },
    /* Domain: Combat */
    { 1, 0,                    35,  4, "Strong Attack", "a2",  "Heavy blow dealing bonus damage.", DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0, 0 },
    { 3, 0,                    28,  0, "Parry",        "a4",  "Brace and halve incoming damage.", DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0, 0 },
    { 6, ACT_CTX_CAN_STUN,     42,  0, "Stun",         "a7",  "Stun the enemy, skip their turn.", DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0, 0 },
    { 10, 0,                   45,  0, "Intimidate",   "a11", "Lower enemy attack, skip ctr.",    DOMAIN_COMBAT,   ACT_CAT_COMBAT|ACT_CAT_SOCIAL, 0, 0 },
    { 11, ACT_CTX_PLAYER_HURT, 65,  8, "Counter",      "a12", "Strike hard while wounded.",       DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0, 0 },
    { 12, 0,                   35,  0, "War Cry",      "a13", "A fierce cry staggers the foe.",   DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0, 0 },
    { 13, 0,                   30,  0, "Threaten",     "a14", "Force the threat of violence.",    DOMAIN_COMBAT,   ACT_CAT_SOCIAL,                0, 0 },
    { 14, ACT_CTX_FIRST_TURN,  55,  8, "Ambush",       "a15", "Strike from cover, skip ctr.",     DOMAIN_COMBAT,   ACT_CAT_HUNT,                  0, 0 },
    /* Domain: Trickery */
    { 4, ACT_CTX_ENEMY_WEAPON, 48,  0, "Disarm",       "a5",  "Strip the weapon from the enemy.", DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0, 0 },
    { 5, ACT_CTX_FIRST_TURN,   60,  6, "Moonstep",     "a6",  "Strike first. Skip the counter.",  DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0, 0 },
    { 8, 0,                    20,  0, "Blindspot",    "a9",  "Find cover. Avoid the counter.",   DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0, 0 },
    { 15, 0,                   35,  0, "Vanish",       "a16", "Disappear, skip enemy attack.",    DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0, 0 },
    { 16, 0,                   40,  3, "Poison Blade", "a17", "Coat the blade in slow toxin.",    DOMAIN_TRICKERY, ACT_CAT_COMBAT,                0, 0 },
    { 17, 0,                   38,  5, "Set Trap",     "a18", "Lay a trap, skip next attack.",    DOMAIN_TRICKERY, ACT_CAT_COMBAT|ACT_CAT_HUNT,   0, 0 },
    { 18, 0,                   45,  0, "Deceive",      "a19", "Spin a web of lies, escape.",      DOMAIN_TRICKERY, ACT_CAT_SOCIAL,                0, 0 },
    { 19, 0,                   30,  0, "Pickpocket",   "a20", "Lift gold from their pocket.",     DOMAIN_TRICKERY, ACT_CAT_SOCIAL,                0, 0 },
    { 20, 0,                   40,  0, "Inspect",      "a21", "Study weakness, cut defense.",     DOMAIN_TRICKERY, ACT_CAT_INVESTIGATION,         0, 0 },
    { 21, ACT_CTX_FIRST_TURN,  50,  4, "Track",        "a22", "Follow the trail, first blow.",    DOMAIN_TRICKERY, ACT_CAT_HUNT,                  0, 0 },
    /* Domain: Blood */
    { 2, ACT_CTX_PLAYER_HURT,  55, 10, "Regenerate",   "a3",  "Draw on vitality to restore HP.",  DOMAIN_BLOOD,    ACT_CAT_COMBAT,                0, 0 },
    { 22, ACT_CTX_REQUIRES_DARK,55, 6, "Blood Drain",  "a23", "Drain blood. Restore your HP.",    DOMAIN_BLOOD,    ACT_CAT_COMBAT,                0, 0 },
    { 23, ACT_CTX_BLOCKED_HOLY,65, 12, "Lethal Bite",  "a24", "Pierce deep. Massive damage.",     DOMAIN_BLOOD,    ACT_CAT_COMBAT,                0, 0 },
    { 24, 0,                   40,  5, "Blood Howl",   "a25", "Howl and strike every enemy.",     DOMAIN_BLOOD,    ACT_CAT_COMBAT,                0, 0 },
    { 25, 0,                   30, 18, "Blood Surge",  "a26", "Savage burst. Costs your HP.",     DOMAIN_BLOOD,    ACT_CAT_COMBAT,                0, 0 },
    { 26, 0,                   50,  8, "Blood Scent",  "a27", "Track wounds. Bonus on prey.",     DOMAIN_BLOOD,    ACT_CAT_HUNT,                  0, 0 },
    /* Domain: Charm */
    { 27, 0,                   45,  0, "Dominate",     "a28", "Bend their will, end the fight.",  DOMAIN_CHARM,    ACT_CAT_COMBAT|ACT_CAT_SOCIAL, 0, 0 },
    { 28, ACT_CTX_CAN_STUN,    48,  0, "Mesmerize",    "a29", "Trap the mind, stun and skip.",    DOMAIN_CHARM,    ACT_CAT_COMBAT,                0, 0 },
    { 29, 0,                   35,  0, "Bribe",        "a30", "Pay them off to end the fight.",   DOMAIN_CHARM,    ACT_CAT_SOCIAL,                0, 0 },
    { 30, 0,                   50,  0, "Silver Tongue","a31", "Words of silver end conflict.",    DOMAIN_CHARM,    ACT_CAT_SOCIAL,                0, 0 },
    { 31, 0,                   40,  0, "Interrogate",  "a32", "Demand answers, cut defense.",     DOMAIN_CHARM,    ACT_CAT_INVESTIGATION,         0, 0 },
    /* Environmental */
    { 9, ACT_CTX_EXECUTABLE,   78, 15, "Death star",   "a10", "Lethal blow on a weakened enemy.", DOMAIN_COMBAT,   ACT_CAT_COMBAT,                0, 0 },
    { 32, ACT_CTX_REQUIRES_DARK,60,15, "Feed",         "a33", "Feed on the dark to restore HP.",  DOMAIN_BLOOD,    ACT_CAT_ENVIRONMENTAL,         0, 0 },
    { 33, 0,                   35,  0, "Blend In",     "a34", "Melt into shadow, skip hit.",      DOMAIN_TRICKERY, ACT_CAT_ENVIRONMENTAL,         0, 0 },
    { 34, 0,                   40, 12, "Rally",        "a35", "Gather resolve, restore HP.",      DOMAIN_COMBAT,   ACT_CAT_ENVIRONMENTAL,         0, 0 },
};

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
