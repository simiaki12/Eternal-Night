#include "actions.h"
#include <string.h>

ActionDef actionDefs[ACTION_MAX];
int       actionDefCount = 0;

static const ActionDef builtinDefs[] = {
    { ACTION_ATTACK,   0,                    70,  0,  "Slash",         "a1",  {0} },
    { ACTION_STRONG,   0,                    35,  4,  "Strong Attack",  "a2",  {0} },
    { ACTION_HEAL,     ACT_CTX_PLAYER_HURT,  55, 10,  "Regenerate",    "a3",  {0} },
    { ACTION_DEFEND,   0,                    28,  0,  "Parry",         "a4",  {0} },
    { ACTION_DISARM,   ACT_CTX_ENEMY_WEAPON, 48,  0,  "Disarm",        "a5",  {0} },
    { ACTION_BACKSTAB, ACT_CTX_FIRST_TURN,   60,  6,  "Moonstep",      "a6",  {0} },
    { ACTION_STUN,     ACT_CTX_CAN_STUN,     42,  0,  "Stun",          "a7",  {0} },
    { ACTION_CALM,     0,                    22,  0,  "Persuade",      "a8",  {0} },
    { ACTION_HIDE,     0,                    20,  0,  "Blindspot",     "a9",  {0} },
    { ACTION_EXECUTE,  ACT_CTX_EXECUTABLE,   78, 15,  "Death star",    "a10", {0} },
};
#define BUILTIN_COUNT (int)(sizeof(builtinDefs)/sizeof(builtinDefs[0]))

/* Format: [1 count][N × sizeof(ActionDef)] */
int loadActions(PakData data) {
    if (!data.data || data.size < 1) return 0;
    uint8_t n = ((const uint8_t *)data.data)[0];
    if (n > ACTION_MAX) n = ACTION_MAX;
    uint32_t expected = 1 + (uint32_t)n * sizeof(ActionDef);
    if (data.size < expected) return 0;
    memcpy(actionDefs, (const uint8_t *)data.data + 1, (size_t)n * sizeof(ActionDef));
    actionDefCount = n;
    return n;
}

const ActionDef *getActionDef(uint8_t id) {
    for (int i = 0; i < actionDefCount; i++)
        if (actionDefs[i].id == id) return &actionDefs[i];
    for (int i = 0; i < BUILTIN_COUNT; i++)
        if (builtinDefs[i].id == id) return &builtinDefs[i];
    return NULL;
}
