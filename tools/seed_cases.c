/* tools/seed_cases.c — writes assets/data/cases.dat
 *
 * A case is an investigation arc that spans several scenes. Its position on
 * the shared investigation graph (Cold Trail -> Familiarizing -> Connecting
 * -> Breakthrough -> Unraveled) persists in the save, so scenes revisited
 * later in the case surface material that was invisible the first time.
 *
 * Run once: make seed_cases */

#include <stdio.h>
#include <stdint.h>

#define CASE_FLAG_HIDDEN (1<<0)

/* Investigation graph states — mirror of seed_states.c */
#define IS_COLD         0
#define IS_FAMILIARIZE  1
#define IS_CONNECTING   2
#define IS_BREAKTHROUGH 3
#define IS_UNRAVELED    4

typedef struct {
    uint8_t id;
    char    name[24];
    uint8_t startState;
    uint8_t rewardQuest;
    uint8_t setFlag;
    uint8_t flags;
    uint8_t _pad[3];
} CaseDef;

typedef char _chk[(sizeof(CaseDef) == 32) ? 1 : -1];

static const CaseDef defs[] = {
    /* id  name                     start     quest  flag  flags */
    { 0, "The Merchant's Death",  IS_COLD,  0xFF,   50,   0, {0,0,0} },
    { 1, "Missing Nightwatch",    IS_COLD,  0xFF,   51,   CASE_FLAG_HIDDEN, {0,0,0} },
};

int main(void) {
    FILE *f = fopen("assets/data/cases.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot write assets/data/cases.dat\n"); return 1; }
    uint8_t n = (uint8_t)(sizeof(defs) / sizeof(defs[0]));
    fwrite(&n, 1, 1, f);
    fwrite(defs, sizeof(CaseDef), n, f);
    fclose(f);
    printf("Wrote %d case(s) to assets/data/cases.dat\n", n);
    return 0;
}
