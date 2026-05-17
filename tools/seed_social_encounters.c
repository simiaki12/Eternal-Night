/* tools/seed_social_encounters.c — writes the initial assets/data/social_encounters.dat
 * Run once: make seed_social_encounters
 * After that, edit here and re-run, or use the editor when it exists. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    uint16_t flags;
    uint8_t  id;
    uint8_t  npc_id;
    uint8_t  reward_partial;
    uint8_t  reward_full;
    uint8_t  disp_start;
    uint8_t  _pad;
} SocialEncounterDef;

typedef char check_size[(sizeof(SocialEncounterDef) == 8) ? 1 : -1];

#define SE_FLAG_ONE_SHOT      (1<<0)
#define SE_FLAG_DISP_OVERRIDE (1<<1)
#define SE_FLAG_HIDDEN        (1<<2)

static SocialEncounterDef defs[] = {
    /* flags            id  npc  part full  disp  pad */
    {  0,               0,   0,   0,   1,    0,    0 }, /* Village Elder: intro */
    {  SE_FLAG_HIDDEN,  1,   0,   2,   3,    0,    0 }, /* Village Elder: post-quest follow-up */
    {  0,               2,   1,   4,   5,    0,    0 }, /* Merchant: barter */
};

int main(void) {
    FILE *f = fopen("assets/data/social_encounters.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot write assets/data/social_encounters.dat\n"); return 1; }

    int n = (int)(sizeof(defs) / sizeof(defs[0]));
    fwrite(&(uint8_t){(uint8_t)n}, 1, 1, f);
    fwrite(defs, sizeof(SocialEncounterDef), n, f);
    fclose(f);

    printf("Written assets/data/social_encounters.dat  (%d encounters, %d bytes)\n",
        n, 1 + n * (int)sizeof(SocialEncounterDef));
    return 0;
}
