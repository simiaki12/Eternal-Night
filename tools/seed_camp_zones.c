/* tools/seed_camp_zones.c — writes the initial assets/data/camp_zones.dat
 * Run once: make seed_camp_zones
 * After that, edit with make camp_zone_editor. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define CAMP_FLAG_REPEATABLE (1<<0)

typedef struct {
    char    mapId[8];
    uint8_t leftX, rightX, topY, bottomY;
    uint8_t huntEncId;
    uint8_t clearedFlag;   /* world flag; 0xFF = never cleared */
    uint8_t flags;
    uint8_t _pad[1];
} CampZone;

typedef char _check_zone[(sizeof(CampZone) == 16) ? 1 : -1];

/* Camp zone 0: Gate district on the opening map
 * Covers tiles (3,3)-(6,6) on map "world".
 * Hunt encounter 0 (Gate Patrol).
 * World flag 0 marks it cleared — enemies on map should be removed
 * by the world map logic once flag 0 is set.
 */
static const CampZone defaults[] = {
    {
        /* mapId      */ "world",
        /* leftX      */ 3,
        /* rightX     */ 6,
        /* topY       */ 3,
        /* bottomY    */ 6,
        /* huntEncId  */ 0,
        /* clearedFlag*/ 0,    /* world flag 0 = gate patrol cleared */
        /* flags      */ 0,
        /* _pad       */ { 0 },
    },
};

int main(void) {
    FILE *f = fopen("assets/data/camp_zones.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot open assets/data/camp_zones.dat\n"); return 1; }
    uint8_t n = (uint8_t)(sizeof(defaults) / sizeof(defaults[0]));
    fwrite(&n, 1, 1, f);
    fwrite(defaults, sizeof(CampZone), n, f);
    fclose(f);
    printf("Wrote %d camp zone(s) to assets/data/camp_zones.dat (%zu bytes each)\n",
        n, sizeof(CampZone));
    return 0;
}
