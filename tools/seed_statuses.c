/* Writes the default status table to assets/data/statuses.dat */
#include <stdio.h>
#include <stdint.h>

/* ---- mirror of src/gameplay/statuses.h and effects.h (keep in sync) ---- */
typedef struct { uint8_t type, value, chance; } Effect;

#define DUR_TURNS 0
#define DUR_STEPS 1
#define DUR_PERM  2

#define STFX_NONE         0
#define STFX_PROGRESS_ALL 1
#define STFX_PROGRESS_DOM 2
#define STFX_THREAT_MOD   3
#define STFX_HP_TICK      4
#define STFX_WEIGHT_DOM   5

#define STATUS_NEGATIVE (1<<0)

#define DOMAIN_COMBAT   0
#define DOMAIN_TRICKERY 1
#define DOMAIN_BLOOD    2
#define DOMAIN_CHARM    3

#define EFX_APPLY_STATUS 6

typedef struct {
    uint8_t id;
    char    name[12];
    char    icon[2];
    uint8_t durType;
    uint8_t duration;
    uint8_t fxType;
    uint8_t fxValue;   /* signed where noted (int8_t) */
    uint8_t fxValue2;
    uint8_t flags;
    Effect  onExpire;
} StatusDef; /* 24 bytes */

typedef char _check_size[(sizeof(StatusDef) == 24) ? 1 : -1];

#define I8(v) ((uint8_t)(int8_t)(v))
#define NOFX  {0, 0, 0}
/* ----------------------------------------------------------------------- */

static const StatusDef defaults[] = {
    /* id  name          icon      dur        len  fx                 val      val2           flags */
    {  0, "Rage",        {'R','!'}, DUR_TURNS,  3, STFX_WEIGHT_DOM,  I8(+40), DOMAIN_COMBAT, 0,               NOFX },
    {  1, "Focused",     {'F','o'}, DUR_TURNS,  3, STFX_PROGRESS_ALL,I8(+15), 0,             0,               NOFX },
    {  2, "Poisoned",    {'P','x'}, DUR_STEPS, 40, STFX_HP_TICK,     I8(-1),  0,             STATUS_NEGATIVE, NOFX },
    /* untreated wound turns into sickness — effect-vocabulary chaining */
    {  3, "Wounded",     {'W','d'}, DUR_STEPS, 200, STFX_THREAT_MOD, I8(+2),  0,             STATUS_NEGATIVE,
         { EFX_APPLY_STATUS, 4, 100 } },
    {  4, "Sick",        {'S','k'}, DUR_PERM,   0, STFX_THREAT_MOD,  I8(+1),  0,             STATUS_NEGATIVE, NOFX },
    {  5, "Blessed",     {'B','+'}, DUR_STEPS, 150, STFX_THREAT_MOD, I8(-2),  0,             0,               NOFX },
    /* Parry's one-turn guard — active through this turn's pressure phase */
    {  6, "Guarded",     {'G','d'}, DUR_TURNS,  1, STFX_THREAT_MOD,  I8(-5),  0,             0,               NOFX },
};

int main(void) {
    FILE *f = fopen("assets/data/statuses.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot open assets/data/statuses.dat\n"); return 1; }
    uint8_t n = (uint8_t)(sizeof(defaults) / sizeof(defaults[0]));
    fwrite(&n, 1, 1, f);
    fwrite(defaults, sizeof(StatusDef), n, f);
    fclose(f);
    printf("Wrote %d statuses to assets/data/statuses.dat\n", n);
    return 0;
}
