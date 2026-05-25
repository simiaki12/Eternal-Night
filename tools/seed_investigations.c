/* tools/seed_investigations.c — writes the initial assets/data/investigations.dat
 * Run once: make seed_investigations
 * After that, edit with make investigation_editor. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define INV_CLUE_SLOTS 8

#define INV_FLAG_STAGED      (1<<0)
#define INV_FLAG_RETURNABLE  (1<<1)

#define INV_PRESSURE_DECAY   3

/* 80 bytes — must match investigations.h */
typedef struct {
    uint8_t id;
    char    name[24];
    char    pressureText[36];
    uint8_t clueIds[INV_CLUE_SLOTS];
    uint8_t clueCount;
    uint8_t turnLimit;
    uint8_t keyMask;
    uint8_t rewardItem;
    uint8_t rewardQuest;
    uint8_t pressureType;
    uint8_t flags;
    uint8_t _pad[4];
} InvestigationDef;

typedef char _check[(sizeof(InvestigationDef) == 80) ? 1 : -1];

/* keyMask bits correspond to positions in clueIds[]:
 *   bit 0 → clueIds[0] (clue 0, "blood still warm")
 *   bit 2 → clueIds[2] (clue 2, "noble crest")
 * Both must be found for the scene to resolve. */
static const InvestigationDef defaults[] = {
    {
        0,
        "The Merchant's Death",
        "Guards will seal the scene at dawn.",
        { 0, 1, 2, 3, 4, 0xFF, 0xFF, 0xFF },
        5,          /* clueCount */
        8,          /* turnLimit */
        0x05,       /* keyMask: positions 0 and 2 */
        0xFF,       /* rewardItem: none */
        0xFF,       /* rewardQuest: none */
        INV_PRESSURE_DECAY,
        INV_FLAG_STAGED,
        { 0, 0, 0, 0 }
    },
};

int main(void) {
    FILE *f = fopen("assets/data/investigations.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot open assets/data/investigations.dat\n"); return 1; }
    uint8_t n = (uint8_t)(sizeof(defaults) / sizeof(defaults[0]));
    fwrite(&n, 1, 1, f);
    fwrite(defaults, sizeof(InvestigationDef), n, f);
    fclose(f);
    printf("Wrote %d investigation(s) to assets/data/investigations.dat\n", n);
    return 0;
}
