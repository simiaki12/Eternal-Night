/* Writes the default action table to assets/data/actions.dat */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define ACT_CTX_FIRST_TURN   (1<<0)
#define ACT_CTX_ENEMY_WEAPON (1<<1)
#define ACT_CTX_EXECUTABLE   (1<<2)
#define ACT_CTX_CAN_STUN     (1<<3)
#define ACT_CTX_PLAYER_HURT  (1<<4)

typedef struct {
    uint8_t  id;
    uint8_t  contextFlags;
    uint8_t  baseWeight;
    uint8_t  power;
    char     name[16];
    char     imgName[8];
    uint8_t  _pad[4];
} ActionDef;

static const ActionDef defaults[] = {
    { 0, 0,                    70,  0, "Slash",         "a1",  {0} },
    { 1, 0,                    35,  4, "Strong Attack",  "a2",  {0} },
    { 2, ACT_CTX_PLAYER_HURT,  55, 10, "Regenerate",    "a3",  {0} },
    { 3, 0,                    28,  0, "Parry",         "a4",  {0} },
    { 4, ACT_CTX_ENEMY_WEAPON, 48,  0, "Disarm",        "a5",  {0} },
    { 5, ACT_CTX_FIRST_TURN,   60,  6, "Moonstep",      "a6",  {0} },
    { 6, ACT_CTX_CAN_STUN,     42,  0, "Stun",          "a7",  {0} },
    { 7, 0,                    22,  0, "Persuade",      "a8",  {0} },
    { 8, 0,                    20,  0, "Blindspot",     "a9",  {0} },
    { 9, ACT_CTX_EXECUTABLE,   78, 15, "Death star",    "a10", {0} },
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
