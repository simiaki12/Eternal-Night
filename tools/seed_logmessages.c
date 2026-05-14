/* tools/seed_logmessages.c — writes assets/data/log_messages.dat
 * Run once: make seed_logmessages
 * Edit entries below and re-run to regenerate. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ---- mirrors of log_messages.h (keep in sync) ---- */
#define LOG_MSG_MAX 128

#define LOGTRIG_ACTION        0
#define LOGTRIG_ENEMY_KILLED  1
#define LOGTRIG_COMBAT_START  2
#define LOGTRIG_TURN_END      3
#define LOGTRIG_PLAYER_HURT   4
#define LOGTRIG_PLAYER_LOW_HP 5

#define ENEMYCOND_NONE        0
#define ENEMYCOND_SIZE_GTE    1
#define ENEMYCOND_HP_LTE_PCT  2
#define ENEMYCOND_HP_GTE_PCT  3
#define ENEMYCOND_HAS_FLAG    4
#define ENEMYCOND_COUNT_GTE   5

#define LOGFX_NONE            0
#define LOGFX_ADD_MOD         1
#define LOGFX_REMOVE_MOD      2
#define LOGFX_ENEMY_FLEE      3
#define LOGFX_ENEMY_STUN      4
#define LOGFX_ENEMY_ATK_DOWN  5
#define LOGFX_HEAL_PLAYER     6
#define LOGFX_DAMAGE_PLAYER   7

#define LOGFX_TARGET_FOCUSED  0
#define LOGFX_TARGET_WEAKEST  1
#define LOGFX_TARGET_BIGGEST  2
#define LOGFX_TARGET_RANDOM   3
#define LOGFX_TARGET_ALL      4

/* ENCOUNTER_* values match ACT_CAT_* bits */
#define ENCOUNTER_COMBAT        1
#define ENCOUNTER_SOCIAL        2
#define ENCOUNTER_INVESTIGATION 4
#define ENCOUNTER_HUNT          8
#define ENCOUNTER_ENVIRONMENTAL 16

/* ENCOUNTER_MOD_* bits */
#define ENCOUNTER_MOD_DARK        (1<<0)
#define ENCOUNTER_MOD_RAINING     (1<<1)
#define ENCOUNTER_MOD_HOLY_GROUND (1<<2)
#define ENCOUNTER_MOD_CROWDED     (1<<3)
#define ENCOUNTER_MOD_BURNING     (1<<4)

/* ENEMY_* flags */
#define ENEMY_HAS_WEAPON (1<<0)
#define ENEMY_STUNNABLE  (1<<3)

typedef struct {
    char    text[28];
    uint8_t trigger;
    uint8_t actionId;
    uint8_t enemyDefId;
    uint8_t encounterType;
    uint8_t modRequired;
    uint8_t chance;
    uint8_t enemyCond;
    uint8_t enemyCondVal;
    uint8_t effectType;
    uint8_t effectTarget;
    uint8_t effectValue;
    uint8_t _pad;
} LogMessage; /* 40 bytes */

#define MSG(txt, trig, act, edef, enc, mod, ch, cond, condval, fx, fxtgt, fxval) \
    { txt, trig, act, edef, enc, mod, ch, cond, condval, fx, fxtgt, fxval, 0 }

static LogMessage msgs[] = {
    /* text                          trig                  act   edef  enc     mod  ch  cond                 condval  fx                   fxtgt                 fxval */
    MSG("The dark closes in.",       LOGTRIG_COMBAT_START, 0xFF, 0xFF, 0xFF,   0,   30, ENEMYCOND_NONE,      0,       LOGFX_ADD_MOD,        LOGFX_TARGET_FOCUSED, ENCOUNTER_MOD_DARK),
    MSG("You feel the ground shake.",LOGTRIG_COMBAT_START, 0xFF, 0xFF, 0xFF,   0,   80, ENEMYCOND_SIZE_GTE,  4,       LOGFX_NONE,           LOGFX_TARGET_FOCUSED, 0),
    MSG("Your vision narrows.",      LOGTRIG_PLAYER_LOW_HP,0xFF, 0xFF, 0xFF,   0,   60, ENEMYCOND_NONE,      0,       LOGFX_NONE,           LOGFX_TARGET_FOCUSED, 0),
    MSG("It hesitates.",             LOGTRIG_TURN_END,     0xFF, 0xFF, 0xFF,   0,   40, ENEMYCOND_HP_LTE_PCT,25,      LOGFX_ENEMY_ATK_DOWN, LOGFX_TARGET_WEAKEST, 1),
    MSG("Their nerve breaks.",       LOGTRIG_ENEMY_KILLED, 0xFF, 0xFF, 0xFF,   0,   40, ENEMYCOND_COUNT_GTE, 1,       LOGFX_ENEMY_ATK_DOWN, LOGFX_TARGET_ALL,     1),
    MSG("The crowd stirs.",          LOGTRIG_PLAYER_HURT,  0xFF, 0xFF, 0xFF,   ENCOUNTER_MOD_CROWDED, 40, ENEMYCOND_NONE, 0, LOGFX_NONE,  LOGFX_TARGET_FOCUSED, 0),
    MSG("Your howl echoes.",         LOGTRIG_ACTION,       24,   0xFF, 0xFF,   0,   50, ENEMYCOND_NONE,      0,       LOGFX_NONE,           LOGFX_TARGET_FOCUSED, 0),
    MSG("One of them flees!",        LOGTRIG_PLAYER_LOW_HP,0xFF, 0xFF, 0xFF,   0,   20, ENEMYCOND_COUNT_GTE, 2,       LOGFX_ENEMY_FLEE,     LOGFX_TARGET_WEAKEST, 0),
};

int main(void) {
    FILE *f = fopen("assets/data/log_messages.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot write assets/data/log_messages.dat\n"); return 1; }

    int n = (int)(sizeof(msgs) / sizeof(msgs[0]));
    if (n > LOG_MSG_MAX) n = LOG_MSG_MAX;
    fwrite(&(uint8_t){(uint8_t)n}, 1, 1, f);
    fwrite(msgs, sizeof(LogMessage), (size_t)n, f);
    fclose(f);

    printf("Written assets/data/log_messages.dat  (%d messages, %d bytes)\n",
           n, 1 + n * (int)sizeof(LogMessage));
    return 0;
}
