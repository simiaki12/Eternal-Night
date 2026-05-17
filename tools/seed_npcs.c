/* tools/seed_npcs.c — writes the initial assets/npcs.dat
 * Run once: make seed_npcs
 * After that, use the npc_editor or edit here and re-run. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    char    name[16];
    char    imgName[3];
    uint8_t treeId;
    uint8_t x;
    uint8_t y;
    char    mapId[8];
    uint8_t resistance;
    uint8_t patience;
    uint8_t social_power;
    uint8_t move_mask;
    uint8_t tags;
    uint8_t base_standing;
    uint8_t npc_flags;
    uint8_t _pad[3];
} NpcDef;

typedef char check_size[(sizeof(NpcDef) == 40) ? 1 : -1];

/* NPC_TAG_* mirrors from social.h */
#define NPC_TAG_FEARFUL   (1<<0)
#define NPC_TAG_NOBLE     (1<<1)
#define NPC_TAG_CRIMINAL  (1<<2)
#define NPC_TAG_CONNECTED (1<<3)
#define NPC_TAG_FEARLESS  (1<<4)

/* NPC_MOVE_* mirrors from social.h */
#define NPC_MOVE_DISMISS  (1<<0)
#define NPC_MOVE_THREATEN (1<<1)
#define NPC_MOVE_LEAVE    (1<<2)

static NpcDef defs[] = {
    /*  name            img  tree  x   y  mapId    res  pat  pow  mov  tags               stand  flags  pad */
    { "Village Elder", "op",  1,   1,  1, "map1",   40,  0,   1, NPC_MOVE_DISMISS, NPC_TAG_NOBLE,  60,  0, {0} },
    { "Merchant",      "sh",  0,   3,  3, "map1",   30,  3,   0,  0,  NPC_TAG_CRIMINAL,  50,  0, {0} },
};

int main(void) {
    FILE *f = fopen("assets/data/npcs.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot write assets/data/npcs.dat\n"); return 1; }

    int n = (int)(sizeof(defs) / sizeof(defs[0]));
    fwrite(&(uint8_t){(uint8_t)n}, 1, 1, f);
    fwrite(defs, sizeof(NpcDef), n, f);
    fclose(f);

    printf("Written assets/data/npcs.dat  (%d NPCs, %d bytes)\n",
        n, 1 + n * (int)sizeof(NpcDef));
    return 0;
}
