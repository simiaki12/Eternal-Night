/* tools/seed_hunt_encounters.c — writes the initial assets/data/hunt_encounters.dat
 * Run once: make seed_hunt_encounters
 * After that, edit with make hunt_encounter_editor. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define HUNT_ENC_MAX    16
#define HUNT_STATE_MAX   8
#define HUNT_EDGE_MAX    4
#define HUNT_ACTION_MAX  3

#define HUNT_STATE_TERMINAL (1<<0)
#define HUNT_STATE_SUCCESS  (1<<1)
#define HUNT_STATE_COMBAT   (1<<2)

#define HUNT_FLAG_REPEATABLE (1<<0)

/* Action IDs — must match actions.dat */
#define ACTION_AMBUSH       14
#define ACTION_TRACK        21
#define ACTION_SET_TRAP     17
#define ACTION_BLOOD_SCENT  26
#define ACTION_BLOOD_HOWL   24
#define ACTION_MASSACRE     36

typedef struct {
    uint8_t actionIds[HUNT_ACTION_MAX];
    uint8_t nextState;
    uint8_t setFlag;
    uint8_t _pad[3];
} HuntEdge;

typedef char _check_edge[(sizeof(HuntEdge) == 8) ? 1 : -1];

typedef struct {
    char     description[24];
    HuntEdge edges[HUNT_EDGE_MAX];
    uint8_t  edgeCount;
    uint8_t  turnBudget;
    uint8_t  timeoutNext;
    uint8_t  damage;
    uint8_t  flags;
    uint8_t  _pad[3];
} HuntStateDef;

typedef char _check_state[(sizeof(HuntStateDef) == 64) ? 1 : -1];

typedef struct {
    uint8_t      id;
    char         name[24];
    uint8_t      enemyPoolId;
    uint8_t      enemyCount;
    uint8_t      stateCount;
    uint8_t      startState;
    uint8_t      flags;
    uint8_t      _pad[2];
    HuntStateDef states[HUNT_STATE_MAX];
} HuntEncounterDef;

typedef char _check_enc[(sizeof(HuntEncounterDef) == 544) ? 1 : -1];

/* Hunt Encounter 0: Gate Patrol
 *
 * A squad of 6 soldiers guards the city gate.  Azrael can pick them off
 * one by one from the rooftops and shadows.
 *
 * States:
 *   0 UNAWARE    — squad oblivious; Ambush or Track for stealth kills
 *                  timeout 3 turns -> ALERT
 *   1 ALERT      — squad on edge; Track or Set Trap
 *                  timeout 2 turns -> FIGHTING BACK (combat)
 *   2 SCATTERED  — squad in disarray; any hunt action
 *                  timeout 0 (no timeout)
 *                  Blood Howl -> BROKEN
 *   3 BROKEN     — squad completely demoralised; Massacre available
 *                  success terminal
 *   4 FIGHT BACK — combat fallback terminal
 */
static const HuntEncounterDef defaults[] = {
    {
        /* id */ 0,
        /* name */ "Gate Patrol",
        /* enemyPoolId */ 1,          /* pool 1 — standard soldiers */
        /* enemyCount  */ 6,
        /* stateCount  */ 5,
        /* startState  */ 0,
        /* flags       */ 0,
        /* _pad        */ { 0, 0 },
        /* states */ {
            /* [0] UNAWARE */
            {
                "They haven't seen you.",
                {
                    /* Ambush -> UNAWARE (kill 1, stay hidden) */
                    { {ACTION_AMBUSH,     0xFF, 0xFF}, 0,    0xFF, {0,0,0} },
                    /* Track -> SCATTERED (careful pursuit) */
                    { {ACTION_TRACK,      0xFF, 0xFF}, 2,    0xFF, {0,0,0} },
                    /* Blood Scent -> UNAWARE (locate prey) */
                    { {ACTION_BLOOD_SCENT,0xFF, 0xFF}, 0,    0xFF, {0,0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, {0,0,0} },
                },
                /* edgeCount  */ 3,
                /* turnBudget */ 3,
                /* timeoutNext*/ 1,    /* -> ALERT */
                /* damage     */ 0,
                /* flags      */ 0,
                { 0, 0, 0 }
            },
            /* [1] ALERT */
            {
                "They sense something.",
                {
                    /* Track -> ALERT (precision kill, maintain alert) */
                    { {ACTION_TRACK,    0xFF, 0xFF}, 1,    0xFF, {0,0,0} },
                    /* Set Trap -> SCATTERED */
                    { {ACTION_SET_TRAP, 0xFF, 0xFF}, 2,    0xFF, {0,0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, {0,0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, {0,0,0} },
                },
                /* edgeCount  */ 2,
                /* turnBudget */ 2,
                /* timeoutNext*/ 0xFF, /* -> combat fallback */
                /* damage     */ 0,
                /* flags      */ HUNT_STATE_COMBAT,
                { 0, 0, 0 }
            },
            /* [2] SCATTERED */
            {
                "They scramble.",
                {
                    /* Ambush / Track / Set Trap -> SCATTERED (keep killing) */
                    { {ACTION_AMBUSH,     ACTION_TRACK, ACTION_SET_TRAP}, 2, 0xFF, {0,0,0} },
                    /* Blood Howl -> BROKEN */
                    { {ACTION_BLOOD_HOWL, 0xFF,         0xFF},            3, 0xFF, {0,0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, {0,0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, {0,0,0} },
                },
                /* edgeCount  */ 2,
                /* turnBudget */ 0,    /* no timeout */
                /* timeoutNext*/ 0xFF,
                /* damage     */ 0,
                /* flags      */ 0,
                { 0, 0, 0 }
            },
            /* [3] BROKEN — terminal success; Massacre finishes them */
            {
                "They flee or cower.",
                {
                    /* Massacre -> success terminal */
                    { {ACTION_MASSACRE, 0xFF, 0xFF}, 0xFF, 0xFF, {0,0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, {0,0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, {0,0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, {0,0,0} },
                },
                /* edgeCount  */ 1,
                /* turnBudget */ 0,
                /* timeoutNext*/ 0xFF,
                /* damage     */ 0,
                /* flags      */ HUNT_STATE_TERMINAL | HUNT_STATE_SUCCESS,
                { 0, 0, 0 }
            },
            /* [4] FIGHT BACK — combat fallback terminal */
            {
                "They rally and charge!",
                {
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, {0,0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, {0,0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, {0,0,0} },
                    { {0xFF, 0xFF, 0xFF}, 0xFF, 0xFF, {0,0,0} },
                },
                /* edgeCount  */ 0,
                /* turnBudget */ 0,
                /* timeoutNext*/ 0xFF,
                /* damage     */ 0,
                /* flags      */ HUNT_STATE_TERMINAL | HUNT_STATE_COMBAT,
                { 0, 0, 0 }
            },
            /* [5-7] unused */
            { {0}, {{0}}, 0,0,0,0,0,{0} },
            { {0}, {{0}}, 0,0,0,0,0,{0} },
            { {0}, {{0}}, 0,0,0,0,0,{0} },
        }
    },
};

int main(void) {
    FILE *f = fopen("assets/data/hunt_encounters.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot open assets/data/hunt_encounters.dat\n"); return 1; }
    uint8_t n = (uint8_t)(sizeof(defaults) / sizeof(defaults[0]));
    fwrite(&n, 1, 1, f);
    fwrite(defaults, sizeof(HuntEncounterDef), n, f);
    fclose(f);
    printf("Wrote %d hunt encounter(s) to assets/data/hunt_encounters.dat (%zu bytes each)\n",
        n, sizeof(HuntEncounterDef));
    return 0;
}
