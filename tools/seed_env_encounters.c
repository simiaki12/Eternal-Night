/* tools/seed_env_encounters.c — writes the initial assets/data/env_encounters.dat
 * Run once: make seed_env_encounters
 * After that, edit with make env_encounter_editor. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define ENV_ENC_MAX   32
#define ENV_STATE_MAX  8
#define ENV_EDGE_MAX   4
#define ENV_ACTION_MAX 3

#define ENV_STATE_TERMINAL (1<<0)
#define ENV_STATE_SUCCESS  (1<<1)
#define ENV_FLAG_REPEATABLE (1<<0)

/* Action IDs — must match actions.dat */
#define ACTION_SPRINT   4
#define ACTION_JUMP     5
#define ACTION_CLIMB    6
#define ACTION_BRACE    7
#define ACTION_INSPECT  20

typedef struct {
    uint8_t actionIds[ENV_ACTION_MAX];
    uint8_t nextState;
    uint8_t setFlag;
    uint8_t rewardItem;
    uint8_t _pad[2];
} EnvEdge;

typedef char _check_edge[(sizeof(EnvEdge) == 8) ? 1 : -1];

typedef struct {
    char    description[24];
    EnvEdge edges[ENV_EDGE_MAX];
    uint8_t edgeCount;
    uint8_t turnBudget;
    uint8_t timeoutNext;
    uint8_t damage;
    uint8_t progressGain;
    uint8_t flags;
    uint8_t _pad[2];
} EnvStateDef;

typedef char _check_state[(sizeof(EnvStateDef) == 64) ? 1 : -1];

typedef struct {
    uint8_t     id;
    char        name[24];
    uint8_t     progressGoal;
    uint8_t     stateCount;
    uint8_t     startState;
    uint8_t     flags;
    uint8_t     _pad[3];
    EnvStateDef states[ENV_STATE_MAX];
} EnvEncounterDef;

typedef char _check_enc[(sizeof(EnvEncounterDef) == 544) ? 1 : -1];

/* Encounter 0: Collapsing Bridge
 *
 * States:
 *   0 CREAKING  — bridge is stressed, turn budget 3 → timeout to CRACKING
 *                 Sprint/Jump → CROSSED (success terminal)
 *   1 CRACKING  — boards giving way, damage on entry, turn budget 2 → COLLAPSED
 *                 Sprint → CROSSED (success terminal)
 *                 Brace  → CRACKING (stay, buy a turn)
 *   2 CROSSED   — terminal success, flag 0 = bridge crossed
 *   3 COLLAPSED — terminal failure, damage
 *
 * Progress is accumulated on each fired edge; reaching progressGoal ends immediately.
 */
static const EnvEncounterDef defaults[] = {
    {
        /* id */ 0,
        /* name */ "Collapsing Bridge",
        /* progressGoal */ 60,
        /* stateCount   */ 4,
        /* startState   */ 0,
        /* flags        */ 0,
        /* _pad         */ { 0, 0, 0 },
        /* states */ {
            /* [0] CREAKING */
            {
                "The bridge groans.",
                {
                    /* Sprint or Jump → CROSSED */
                    { {ACTION_SPRINT, ACTION_JUMP, 0xFF}, 2, 0xFF, 0xFF, {0,0} },
                    /* Inspect → stay, small progress, info */
                    { {ACTION_INSPECT, 0xFF,  0xFF},       0, 0xFF, 0xFF, {0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, 0xFF, {0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, 0xFF, {0,0} },
                },
                /* edgeCount */ 2,
                /* turnBudget */ 3,
                /* timeoutNext */ 1,   /* → CRACKING */
                /* damage */ 0,
                /* progressGain */ 25,
                /* flags */ 0,
                { 0, 0 }
            },
            /* [1] CRACKING */
            {
                "Boards splinter.",
                {
                    /* Sprint → CROSSED */
                    { {ACTION_SPRINT, 0xFF, 0xFF}, 2, 0xFF, 0xFF, {0,0} },
                    /* Brace → stay (nextState = current = 1) */
                    { {ACTION_BRACE,  0xFF, 0xFF}, 1, 0xFF, 0xFF, {0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, 0xFF, {0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, 0xFF, {0,0} },
                },
                /* edgeCount */ 2,
                /* turnBudget */ 2,
                /* timeoutNext */ 3,   /* → COLLAPSED */
                /* damage */ 8,
                /* progressGain */ 30,
                /* flags */ 0,
                { 0, 0 }
            },
            /* [2] CROSSED — terminal success */
            {
                "You reach the far side.",
                {
                    /* Finishing touch: catch breath */
                    { {ACTION_BRACE, 0xFF, 0xFF}, 0xFF, 0, 0xFF, {0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, 0xFF, {0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, 0xFF, {0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, 0xFF, {0,0} },
                },
                /* edgeCount */ 1,
                /* turnBudget */ 0,
                /* timeoutNext */ 0xFF,
                /* damage */ 0,
                /* progressGain */ 0,
                /* flags */ ENV_STATE_TERMINAL | ENV_STATE_SUCCESS,
                { 0, 0 }
            },
            /* [3] COLLAPSED — terminal failure */
            {
                "The bridge gives way.",
                {
                    /* Finishing touch: grab a rope */
                    { {ACTION_CLIMB, 0xFF, 0xFF}, 0xFF, 0xFF, 0xFF, {0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, 0xFF, {0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, 0xFF, {0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, 0xFF, {0,0} },
                },
                /* edgeCount */ 1,
                /* turnBudget */ 0,
                /* timeoutNext */ 0xFF,
                /* damage */ 20,
                /* progressGain */ 0,
                /* flags */ ENV_STATE_TERMINAL,
                { 0, 0 }
            },
            /* [4-7] unused */
            { {0}, {{0}}, 0,0,0,0,0,0,{0} },
            { {0}, {{0}}, 0,0,0,0,0,0,{0} },
            { {0}, {{0}}, 0,0,0,0,0,0,{0} },
            { {0}, {{0}}, 0,0,0,0,0,0,{0} },
        }
    },
};

int main(void) {
    FILE *f = fopen("assets/data/env_encounters.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot open assets/data/env_encounters.dat\n"); return 1; }
    uint8_t n = (uint8_t)(sizeof(defaults) / sizeof(defaults[0]));
    fwrite(&n, 1, 1, f);
    fwrite(defaults, sizeof(EnvEncounterDef), n, f);
    fclose(f);
    printf("Wrote %d env encounter(s) to assets/data/env_encounters.dat (%zu bytes each)\n",
        n, sizeof(EnvEncounterDef));
    return 0;
}
