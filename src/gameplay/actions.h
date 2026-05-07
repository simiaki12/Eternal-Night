#pragma once
#include <stdint.h>
#include "pak.h"

#define ACTION_MAX 64

/* Context flags — conditions required for an action to appear in the draw pool */
#define ACT_CTX_FIRST_TURN   (1<<0)
#define ACT_CTX_ENEMY_WEAPON (1<<1)
#define ACT_CTX_EXECUTABLE   (1<<2)
#define ACT_CTX_CAN_STUN     (1<<3)
#define ACT_CTX_PLAYER_HURT  (1<<4)

/* Canonical action IDs — values must match id fields in actions.dat */
typedef enum {
    ACTION_ATTACK   = 0,
    ACTION_STRONG   = 1,
    ACTION_HEAL     = 2,
    ACTION_DEFEND   = 3,
    ACTION_DISARM   = 4,
    ACTION_BACKSTAB = 5,
    ACTION_STUN     = 6,
    ACTION_CALM     = 7,
    ACTION_HIDE     = 8,
    ACTION_EXECUTE  = 9,
    ACTION_COUNT    = 10
} ActionId;

/* 64 bytes — pak-friendly, no pointers */
typedef struct {
    uint8_t  id;
    uint8_t  contextFlags;
    uint8_t  baseWeight;
    uint8_t  power;
    char     name[16];
    char     imgName[8];
    char     desc[32];
    uint8_t  _pad[4];
} ActionDef;

typedef char _check_actiondef_size[(sizeof(ActionDef) == 64) ? 1 : -1];

extern ActionDef actionDefs[ACTION_MAX];
extern int       actionDefCount;

int              loadActions(PakData data);
const ActionDef *getActionDef(uint8_t id);
int              buildActionPool(uint8_t out[ACTION_MAX]);
void             renderActionPanel(const char *title, const uint8_t *ids, int count, int sel);
uint8_t          actionGetDomain(uint8_t id); /* returns DOMAIN_* or 0xFF if unaffiliated */
