#pragma once
#include <stdint.h>

#define DOMAIN_COUNT    14
#define DOMAIN_NODE_MAX 25

/* Base domain indices */
#define DOMAIN_COMBAT   0
#define DOMAIN_TRICKERY 1
#define DOMAIN_BLOOD    2
#define DOMAIN_CHARM    3
/* Fusion domains 4-9, story domains 10-13 — names TBD */
#define DOMAIN_NONE     0xFF /* sentinel: no domain selected / unaffiliated */

/* Stat IDs for NODE_REWARD_STAT — packed into reward_value as (stat<<8)|delta */
#define DSTAT_ATTACK       0
#define DSTAT_DEFENSE      1
#define DSTAT_MAXHP        2
#define DSTAT_STAMINA      3
#define DSTAT_INTELLIGENCE 4
#define DSTAT_PERCEPTION   5
#define DSTAT_CHARISMA     6
#define DSTAT_AGILITY      7

#define STAT_NODE(stat, delta) (int16_t)(((stat) << 8) | (uint8_t)(delta))

typedef enum {
    DOMAIN_TYPE_BASE   = 0,
    DOMAIN_TYPE_FUSION = 1,
    DOMAIN_TYPE_STORY  = 2
} DomainType;

typedef enum {
    NODE_REWARD_STAT    = 0,  /* reward_value = STAT_NODE(stat_id, delta) */
    NODE_REWARD_ACTION  = 1,  /* reward_value = ActionId unlocked */
    NODE_REWARD_UPGRADE = 2,  /* reward_value = packed (action_id | tier<<8) */
    NODE_REWARD_PASSIVE = 3,
    NODE_REWARD_VARIANCE= 4,
} NodeRewardType;

typedef struct {
    uint8_t        id;
    uint8_t        domain_id;
    uint8_t        prereq[2];     /* node ids required; 0xFF = none */
    uint8_t        cost;          /* minimum domain level required */
    uint8_t        tree_row;      /* visual row in tree (0 = top) */
    uint8_t        tree_col;      /* visual column in tree (0 = leftmost) */
    NodeRewardType type;
    int16_t        reward_value;
    char           name[16];      /* empty string = sentinel (no more nodes) */
} DomainNode;

typedef struct {
    uint8_t    id;
    DomainType type;
    uint8_t    parent_a;
    uint8_t    parent_b;
    DomainNode nodes[DOMAIN_NODE_MAX];
} Domain;

typedef struct {
    uint16_t xp;
    uint8_t  level;
    uint8_t  perk_points;
    uint32_t unlocked_nodes;  /* bitmask, bit i = node i unlocked */
} PlayerDomainState;

extern Domain domains[DOMAIN_COUNT];

const char *domainName(int id);
int         domainXpToNext(uint8_t level);
int         domainAwardXp(uint8_t domain_id, int amount);
int         domainNodeUnlocked(uint8_t domain_id, uint8_t node_id);
int         domainUnlockNode(uint8_t domain_id, uint8_t node_id);
int         domainCanUnlock(uint8_t domain_id, uint8_t node_id);
int         domainApplyNode(uint8_t domain_id, uint8_t node_id);
uint8_t     domainPrimary(void);

void        handleDomainsInput(int key);
void        renderDomains(void);
