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
    uint8_t  _pad[2];
} ActionDef;

typedef char _check_size[(sizeof(ActionDef) == 64) ? 1 : -1];
/* ----------------------------------------------------------------------- */

static const ActionDef defaults[] = {
    { 0, 0,                    70,  0, "Slash",        "a1",  "A quick, reliable strike.",        DOMAIN_COMBAT,   ACT_CAT_COMBAT,                {0} },
    { 1, 0,                    35,  4, "Strong Attack", "a2",  "Heavy blow dealing bonus damage.", DOMAIN_COMBAT,   ACT_CAT_COMBAT,                {0} },
    { 2, ACT_CTX_PLAYER_HURT,  55, 10, "Regenerate",   "a3",  "Draw on vitality to restore HP.",  DOMAIN_BLOOD,    ACT_CAT_COMBAT,                {0} },
    { 3, 0,                    28,  0, "Parry",        "a4",  "Brace and halve incoming damage.", DOMAIN_COMBAT,   ACT_CAT_COMBAT,                {0} },
    { 4, ACT_CTX_ENEMY_WEAPON, 48,  0, "Disarm",       "a5",  "Strip the weapon from the enemy.", DOMAIN_TRICKERY, ACT_CAT_COMBAT,                {0} },
    { 5, ACT_CTX_FIRST_TURN,   60,  6, "Moonstep",     "a6",  "Strike first. Skip the counter.",  DOMAIN_TRICKERY, ACT_CAT_COMBAT,                {0} },
    { 6, ACT_CTX_CAN_STUN,     42,  0, "Stun",         "a7",  "Stun the enemy, skip their turn.", DOMAIN_COMBAT,   ACT_CAT_COMBAT,                {0} },
    { 7, 0,                    22,  0, "Persuade",     "a8",  "Persuade the enemy, lower aggro.", DOMAIN_CHARM,    ACT_CAT_COMBAT|ACT_CAT_SOCIAL, {0} },
    { 8, 0,                    20,  0, "Blindspot",    "a9",  "Find cover. Avoid the counter.",   DOMAIN_TRICKERY, ACT_CAT_COMBAT,                {0} },
    { 9, ACT_CTX_EXECUTABLE,   78, 15, "Death star",   "a10", "Lethal blow on a weakened enemy.", DOMAIN_COMBAT,   ACT_CAT_COMBAT,                {0} },
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
