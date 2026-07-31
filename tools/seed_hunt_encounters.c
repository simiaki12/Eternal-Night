/* tools/seed_hunt_encounters.c — writes assets/data/hunt_encounters.dat
 * and assets/data/camp_zones.dat.
 *
 * Hunts run on the shared hunt morale graph (see seed_states.c):
 *   0 Organized  1 Disturbed  2 Alerted  3 Terrified  4 Broken  5 Preparing
 * The group is thinned by action/state EFX_KILL effects; the hunt is won
 * when the counter empties. Botched actions raise alert, which escalates
 * their posture along escalateTo[] and eventually makes them charge.
 *
 * Run once: make seed_hunt_encounters
 * After that, edit with make hunt_encounter_editor. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define GRAPH_STATES_MAX 8
#define HUNT_FLAG_REPEATABLE (1<<0)
#define CAMP_FLAG_REPEATABLE (1<<0)

/* Hunt graph states */
#define HS_ORGANIZED 0
#define HS_DISTURBED 1
#define HS_ALERTED   2
#define HS_TERRIFIED 3
#define HS_BROKEN    4
#define HS_PREPARING 5

#define MASK_ALL 0x3F  /* all six postures */
#define NONE     0xFF

typedef struct {
    uint8_t id;
    char    name[24];
    uint8_t enemyPoolId;
    uint8_t enemyCount;
    uint8_t stateMask;
    uint8_t tenacity;
    uint8_t escalateEvery;
    uint8_t alertLimit;
    uint8_t escalateTo[GRAPH_STATES_MAX];
    uint8_t flags;
    uint8_t setFlag;
    uint8_t _pad[3];
} HuntEncounterDef;

typedef char _chk[(sizeof(HuntEncounterDef) == 44) ? 1 : -1];

typedef struct {
    char    mapId[8];
    uint8_t leftX, rightX, topY, bottomY;
    uint8_t huntEncId;
    uint8_t clearedFlag;
    uint8_t flags;
    uint8_t _pad[1];
} CampZone;

typedef char _chk2[(sizeof(CampZone) == 16) ? 1 : -1];

/* Escalation ladders. Panic recovers toward Alerted — let them catch their
   breath and the rout you built is gone. */
#define LADDER_STANDARD { HS_DISTURBED, HS_ALERTED, HS_PREPARING, HS_ALERTED, NONE, NONE, NONE, NONE }
/* Disciplined troops skip straight to forming up */
#define LADDER_DRILLED  { HS_ALERTED, HS_PREPARING, HS_PREPARING, HS_ALERTED, NONE, NONE, NONE, NONE }

static const HuntEncounterDef defs[] = {
    /* Bandits: undisciplined — they break easily, but they are quick to notice */
    { 0, "Bandit Camp", 1, 5, MASK_ALL, 100, 2, 6,
      LADDER_STANDARD, 0, NONE, {0,0,0} },

    /* Wolf pack: hard to rattle (tenacity), never "prepares" — beasts either
       hold or bolt, so Preparing is carved out of the mask */
    { 1, "Wolf Pack", 0, 4, MASK_ALL & ~(1u << HS_PREPARING), 75, 3, 7,
      LADDER_STANDARD, 0, NONE, {0,0,0} },

    /* Cult circle: drilled and fearless — they cannot be Terrified, only
       broken outright, and they form up fast */
    { 2, "Cult Circle", 3, 6, MASK_ALL & ~(1u << HS_TERRIFIED), 60, 2, 5,
      LADDER_DRILLED, 0, NONE, {0,0,0} },
};

static const CampZone zones[] = {
    { "map1", 12, 15, 10, 13, 0, 40, 0, {0} },
    { "map1", 20, 23,  4,  7, 1, 41, 0, {0} },
};

int main(void) {
    FILE *f = fopen("assets/data/hunt_encounters.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot write hunt_encounters.dat\n"); return 1; }
    uint8_t n = (uint8_t)(sizeof(defs) / sizeof(defs[0]));
    fwrite(&n, 1, 1, f);
    fwrite(defs, sizeof(HuntEncounterDef), n, f);
    fclose(f);

    f = fopen("assets/data/camp_zones.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot write camp_zones.dat\n"); return 1; }
    uint8_t m = (uint8_t)(sizeof(zones) / sizeof(zones[0]));
    fwrite(&m, 1, 1, f);
    fwrite(zones, sizeof(CampZone), m, f);
    fclose(f);

    printf("Wrote %d hunt encounters, %d camp zones\n", n, m);
    return 0;
}
