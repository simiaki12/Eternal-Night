#pragma once
#include <stdint.h>
#include "pak.h"
#include "game.h"

#define MAX_QUESTS    128
#define QUEST_MAX_OBJ   4

/* Objective types */
#define OBJ_KILL           0   /* kill required count of targetId enemy */
#define OBJ_GET_ITEM       1   /* acquire targetId item */
#define OBJ_VISIT_ZONE     2   /* step on map event tile with ev->id == targetId */
#define OBJ_TALK_NPC       3   /* complete dialog tree targetId */
#define OBJ_VISIT_LOCATION 4   /* reach questLocations[targetId] (map + tile coord) */

/* Start trigger types */
#define TRIG_ALWAYS   0   /* active from game start */
#define TRIG_DIALOG   1   /* activated when dialog tree startId closes */
#define TRIG_ZONE     2   /* activated when player steps on loc tile == startId */
#define TRIG_ITEM     3   /* activated when player acquires item startId */
#define TRIG_KILL     4   /* activated when player kills enemy startId */
#define TRIG_LOCATION 5   /* activated when player reaches questLocations[startId] */

/* Quest flags */
#define QUEST_MAIN   (1<<0)
#define QUEST_HIDDEN (1<<1)

/* Quest status */
#define QUEST_INACTIVE 0
#define QUEST_ACTIVE   1
#define QUEST_DONE     2

typedef struct {
    uint8_t type;      /* OBJ_* */
    uint8_t targetId;  /* enemy index / item id / loc tile / dialog tree id / location index */
    uint8_t required;  /* count required (1 for visit/talk) */
    uint8_t _pad;
} QuestObjective;  /* 4 bytes */

#define QUEST_LOCATION_MAX 64

/* Named map+tile location for OBJ_VISIT_LOCATION / TRIG_LOCATION.
 * mapId matches the base filename, e.g. "map1" matches "assets/maps/map1.bin". */
typedef struct {
    char    mapId[8];  /* base map filename without path or extension */
    uint8_t x, y;
    uint8_t _pad[2];
} QuestLocation;  /* 12 bytes */

/* Binary format:
 *   [uint8 questCount][questCount × 48 bytes QuestDef]
 *   [uint8 locationCount][locationCount × 12 bytes QuestLocation]   (optional section)
 */
typedef struct {
    char           name[24];
    uint8_t        objectiveCount;
    uint8_t        rewardXp;
    uint8_t        rewardItemId;   /* 0xFF = no item reward */
    uint8_t        flags;          /* QUEST_* */
    QuestObjective objectives[4]; /* 4 × 4 = 16 bytes */
    uint8_t        startType;      /* TRIG_* */
    uint8_t        startId;        /* which dialog/zone/item/enemy triggers start */
    uint8_t        _pad[2];
} QuestDef;  /* 48 bytes */

typedef struct {
    uint8_t status[MAX_QUESTS];
    uint8_t progress[MAX_QUESTS][QUEST_MAX_OBJ];
} QuestState;

typedef struct {
    int       sel;
    GameState returnState;
} QuestLogState;

extern QuestDef      questDefs[MAX_QUESTS];
extern int           questDefCount;
extern QuestLocation questLocations[QUEST_LOCATION_MAX];
extern int           questLocationCount;
extern QuestState    questSt;
extern QuestLogState questLogSt;

int  loadQuests(PakData data);

void questOnEnemyKilled(uint8_t enemyId);
void questOnItemGained(uint8_t itemId);
void questOnZoneEntered(uint8_t locTile);
void questOnDialogDone(int treeId);
void questOnLocationReached(const char *currentMapName, uint8_t x, uint8_t y);

void handleQuestLogInput(int key);
void renderQuestLog(void);
void renderQuestNotifications(void);
