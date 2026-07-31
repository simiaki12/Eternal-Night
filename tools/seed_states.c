/* Writes the five encounter state graphs to assets/data/states.dat */
#include <stdio.h>
#include <stdint.h>

/* ---- mirror of src/gameplay/effects.h and enc_graph.h (keep in sync) ---- */
typedef struct { uint8_t type, value, chance; } Effect;

#define GSTATE_TERMINAL (1<<0)
#define GSTATE_SUCCESS  (1<<1)
#define GSTATE_START    (1<<2)

typedef struct {
    char    name[16];
    int8_t  pressure;
    uint8_t flags;
    Effect  onEnter;
    uint8_t cardTint;
    uint8_t _pad[2];
} StateDef;

typedef char _check_size[(sizeof(StateDef) == 24) ? 1 : -1];

#define NOFX {0, 0, 0}
#define EFX_KILL 9
/* ----------------------------------------------------------------------- */

/* Canonical graph order = ACT_CAT bit positions:
   combat, social, investigation, hunt, environmental */

static const StateDef combatStates[] = {
    { "Squaring Up",   0, GSTATE_START,                  NOFX, 0, {0,0} },
    { "Trading Blows", 3, 0,                             NOFX, 0, {0,0} },
    { "Staggered",     0, 0,                             NOFX, 0, {0,0} },
    { "Frenzied",      8, 0,                             NOFX, 0, {0,0} },
    { "Broken",        0, GSTATE_TERMINAL|GSTATE_SUCCESS, NOFX, 0, {0,0} },
};

/* Social states mirror the old Disposition enum order (values 0-5) so
   existing NPC data keeps meaning. Pressure = willingness flow per turn. */
static const StateDef socialStates[] = {
    { "Stranger",    0, GSTATE_START, NOFX, 0, {0,0} },
    { "Suspicious", -3, 0,            NOFX, 0, {0,0} },
    { "Fearful",     4, 0,            NOFX, 0, {0,0} },
    { "Trusting",    8, 0,            NOFX, 0, {0,0} },
    { "Hostile",    -8, 0,            NOFX, 0, {0,0} },
    { "Greedy",      5, 0,            NOFX, 0, {0,0} },
};

/* The case arc — one instance per case, position persists in the save. */
static const StateDef investigationStates[] = {
    { "Cold Trail",    0, GSTATE_START,                  NOFX, 0, {0,0} },
    { "Familiarizing", 0, 0,                             NOFX, 0, {0,0} },
    { "Connecting",    0, 0,                             NOFX, 0, {0,0} },
    { "Breakthrough",  0, 0,                             NOFX, 0, {0,0} },
    { "Unraveled",     0, GSTATE_TERMINAL|GSTATE_SUCCESS, NOFX, 0, {0,0} },
};

/* The hunted group's morale. Pressure = damage per turn while they hold
   this posture; entering a rout state costs them bodies (EFX_KILL). The
   hunt ends when the enemy counter empties, not on a terminal state. */
static const StateDef huntStates[] = {
    { "Organized",   0, GSTATE_START, NOFX,             0, {0,0} },
    { "Disturbed",   2, 0,            NOFX,             0, {0,0} },
    { "Alerted",     4, 0,            NOFX,             0, {0,0} },
    { "Terrified",   0, 0,            {EFX_KILL,1,100}, 0, {0,0} },
    { "Broken",      0, 0,            {EFX_KILL,2,100}, 0, {0,0} },
    { "Preparing",   9, 0,            NOFX,             0, {0,0} },
};

static const StateDef envStates[] = {
    { "Stable",     0, GSTATE_START,                  NOFX, 0, {0,0} },
    { "Escalating", 2, 0,                             NOFX, 0, {0,0} },
    { "Critical",   5, 0,                             NOFX, 0, {0,0} },
    { "Resolved",   0, GSTATE_TERMINAL|GSTATE_SUCCESS, NOFX, 0, {0,0} },
    { "Disaster",   0, GSTATE_TERMINAL,                NOFX, 0, {0,0} },
};

typedef struct { const StateDef *states; uint8_t count; } Graph;

int main(void) {
    const Graph graphs[] = {
        { combatStates,        sizeof(combatStates)        / sizeof(StateDef) },
        { socialStates,        sizeof(socialStates)        / sizeof(StateDef) },
        { investigationStates, sizeof(investigationStates) / sizeof(StateDef) },
        { huntStates,          sizeof(huntStates)          / sizeof(StateDef) },
        { envStates,           sizeof(envStates)           / sizeof(StateDef) },
    };
    const uint8_t nGraphs = sizeof(graphs) / sizeof(graphs[0]);

    FILE *f = fopen("assets/data/states.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot open assets/data/states.dat\n"); return 1; }
    fwrite(&nGraphs, 1, 1, f);
    int total = 0;
    for (int g = 0; g < nGraphs; g++) {
        fwrite(&graphs[g].count, 1, 1, f);
        fwrite(graphs[g].states, sizeof(StateDef), graphs[g].count, f);
        total += graphs[g].count;
    }
    fclose(f);
    printf("Wrote %d graphs (%d states) to assets/data/states.dat\n", nGraphs, total);
    return 0;
}
