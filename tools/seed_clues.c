#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define CLUE_DEF_MAX     64
#define CLUE_FLAG_KEY        (1<<0)
#define CLUE_FLAG_MISLEADING (1<<1)
#define CLUE_FLAG_LINGERING  (1<<2)

/* Domain indices (must match domains.h) */
#define DOMAIN_COMBAT   0
#define DOMAIN_TRICKERY 1
#define DOMAIN_BLOOD    2
#define DOMAIN_CHARM    3
#define DOMAIN_NONE     0xFF

/* Action IDs for affinity (must match actions.dat) */
#define ACTION_INSPECT  20
#define ACTION_TRACK    21
#define ACTION_BLOOD_SCENT 26

typedef struct {
    uint8_t id;
    char    text[32];
    char    itemHint[32];
    uint8_t difficulty;
    uint8_t domain;
    uint8_t actionAffinity[4];
    uint8_t requiredItem;
    uint8_t invalidates;
    uint8_t flags;
    uint8_t minCaseState;
    uint8_t _pad[5];
} ClueDef;

typedef char _check[(sizeof(ClueDef) == 80) ? 1 : -1];

static const ClueDef defaults[] = {
    /* id  text                             itemHint                        diff  dom           affinity                               req   inv   flags */
    { 0,  "The blood is still warm.",       "",                             60,   DOMAIN_BLOOD,  {ACTION_BLOOD_SCENT, 0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, CLUE_FLAG_KEY,        0, {0} },
    { 1,  "Drag marks lead to the door.",   "",                             80,   DOMAIN_TRICKERY,{ACTION_INSPECT, ACTION_TRACK, 0xFF, 0xFF}, 0xFF, 0xFF, 0,              0, {0} },
    { 2,  "A noble crest on a torn cloth.", "",                             120,  DOMAIN_TRICKERY,{ACTION_INSPECT, 0xFF, 0xFF, 0xFF},       0xFF, 0xFF, CLUE_FLAG_KEY,   2, {0} },
    { 3,  "The victim fled willingly.",      "",                             100,  DOMAIN_CHARM,   {0xFF, 0xFF, 0xFF, 0xFF},                0xFF, 1,    CLUE_FLAG_MISLEADING, 1, {0} },
    { 4,  "Signs of struggle near the wall.","",                            70,   DOMAIN_TRICKERY,{ACTION_INSPECT, 0xFF, 0xFF, 0xFF},       0xFF, 0xFF, 0,               1, {0} },
};

int main(void) {
    FILE *f = fopen("assets/data/clues.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot open assets/data/clues.dat\n"); return 1; }
    uint8_t n = (uint8_t)(sizeof(defaults) / sizeof(defaults[0]));
    fwrite(&n, 1, 1, f);
    fwrite(defaults, sizeof(ClueDef), n, f);
    fclose(f);
    printf("Wrote %d clue(s) to assets/data/clues.dat\n", n);
    return 0;
}
