#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "quests.h"
#include "game.h"
#include "gfx.h"
#include "player.h"
#include "items.h"
#include "enemies.h"

QuestDef      questDefs[MAX_QUESTS];
int           questDefCount = 0;
QuestLocation questLocations[QUEST_LOCATION_MAX];
int           questLocationCount = 0;
QuestState    questSt;
QuestLogState questLogSt;

/* -----------------------------------------------------------------------
 * Notification queue
 * ----------------------------------------------------------------------- */

#define NOTIF_MAX     4
#define NOTIF_DURATION_MS 4000

typedef struct {
    char  line1[48]; /* "QuestName: Objective 1/1 completed!" */
    char  line2[24]; /* "+10 XP" or "" */
    DWORD expiry;
} QuestNotif;

static QuestNotif g_notifs[NOTIF_MAX];
static int        g_notifCount = 0;

static void pushNotif(const char *l1, const char *l2) {
    if (g_notifCount < NOTIF_MAX) {
        QuestNotif *n = &g_notifs[g_notifCount++];
        strncpy(n->line1, l1, sizeof(n->line1) - 1); n->line1[sizeof(n->line1)-1] = '\0';
        strncpy(n->line2, l2, sizeof(n->line2) - 1); n->line2[sizeof(n->line2)-1] = '\0';
        n->expiry = GetTickCount() + NOTIF_DURATION_MS;
    }
}

int loadQuests(PakData data) {
    memset(&questSt, 0, sizeof(questSt));
    questLocationCount = 0;
    if (!data.data || data.size < 1) return 0;

    const uint8_t *d = (const uint8_t *)data.data;
    uint8_t count = d[0];
    if (count > MAX_QUESTS) count = MAX_QUESTS;

    uint32_t questBytes = 1 + (uint32_t)count * sizeof(QuestDef);
    if (data.size < questBytes) return 0;

    memcpy(questDefs, d + 1, (size_t)count * sizeof(QuestDef));
    questDefCount = count;

    /* Optional location table immediately after quest block */
    if (data.size > questBytes) {
        uint8_t lc = d[questBytes];
        if (lc > QUEST_LOCATION_MAX) lc = QUEST_LOCATION_MAX;
        uint32_t locBytes = 1 + (uint32_t)lc * sizeof(QuestLocation);
        if (data.size >= questBytes + locBytes) {
            memcpy(questLocations, d + questBytes + 1, (size_t)lc * sizeof(QuestLocation));
            questLocationCount = lc;
        }
    }

    /* Activate always-on quests immediately */
    for (int i = 0; i < questDefCount; i++) {
        if (questDefs[i].startType == TRIG_ALWAYS)
            questSt.status[i] = QUEST_ACTIVE;
    }
    return questDefCount;
}

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

static void objTargetName(const QuestObjective *o, char *buf, int bufLen);

/* Try to complete a quest: check all objectives, grant reward if done. */
static void checkCompletion(int qi) {
    const QuestDef *q = &questDefs[qi];
    for (int j = 0; j < q->objectiveCount; j++) {
        if (questSt.progress[qi][j] < q->objectives[j].required)
            return;
    }
    questSt.status[qi] = QUEST_DONE;
    if (q->rewardXp)             awardXp(q->rewardXp);
    if (q->rewardItemId != 0xFF) addItem(q->rewardItemId);

    char l1[48], l2[24];
    snprintf(l1, sizeof(l1), "%.23s: Quest complete!", q->name);
    if (q->rewardXp)
        snprintf(l2, sizeof(l2), "+%d XP", q->rewardXp);
    else
        l2[0] = '\0';
    pushNotif(l1, l2);
}

/* Try to activate a quest based on a trigger type + id. */
static void tryActivate(uint8_t trigType, uint8_t trigId) {
    for (int i = 0; i < questDefCount; i++) {
        if (questSt.status[i] != QUEST_INACTIVE) continue;
        if (questDefs[i].startType == trigType && questDefs[i].startId == trigId)
            questSt.status[i] = QUEST_ACTIVE;
    }
}

/* Advance all active quests matching a given objective type + targetId. */
static void advanceObjectives(uint8_t objType, uint8_t targetId) {
    for (int i = 0; i < questDefCount; i++) {
        if (questSt.status[i] != QUEST_ACTIVE) continue;
        const QuestDef *q = &questDefs[i];
        for (int j = 0; j < q->objectiveCount; j++) {
            if (q->objectives[j].type != objType) continue;
            if (q->objectives[j].targetId != targetId) continue;
            if (questSt.progress[i][j] >= q->objectives[j].required) continue;
            questSt.progress[i][j]++;

            /* Notify objective progress (only if not yet triggering completion) */
            if (questSt.progress[i][j] < q->objectives[j].required) {
                char tgt[20], l1[48];
                objTargetName(&q->objectives[j], tgt, sizeof(tgt));
                snprintf(l1, sizeof(l1), "%.14s: %s %d/%d",
                    q->name,
                    tgt,
                    questSt.progress[i][j],
                    q->objectives[j].required);
                pushNotif(l1, "");
            }
        }
        checkCompletion(i);
    }
}

/* -----------------------------------------------------------------------
 * Public trigger hooks — called by game systems
 * ----------------------------------------------------------------------- */

void questOnEnemyKilled(uint8_t enemyId) {
    if (questDefCount == 0) return;
    tryActivate(TRIG_KILL, enemyId);
    advanceObjectives(OBJ_KILL, enemyId);
}

void questOnItemGained(uint8_t itemId) {
    if (questDefCount == 0) return;
    tryActivate(TRIG_ITEM, itemId);
    advanceObjectives(OBJ_GET_ITEM, itemId);
}

void questOnZoneEntered(uint8_t locTile) {
    if (questDefCount == 0) return;
    tryActivate(TRIG_ZONE, locTile);
    advanceObjectives(OBJ_VISIT_ZONE, locTile);
}

void questOnDialogDone(int treeId) {
    if (questDefCount == 0 || treeId < 0 || treeId > 254) return;
    tryActivate(TRIG_DIALOG, (uint8_t)treeId);
    advanceObjectives(OBJ_TALK_NPC, (uint8_t)treeId);
}

/* Returns 1 if base filename of mapName matches loc->mapId exactly. */
static int locMapMatches(const char *mapName, const char *mapId) {
    const char *slash = strrchr(mapName, '/');
    const char *base  = slash ? slash + 1 : mapName;
    int i = 0;
    while (i < 7 && mapId[i] && base[i] == mapId[i]) i++;
    if (mapId[i] != '\0') return 0;
    return base[i] == '.' || base[i] == '\0';
}

void questOnLocationReached(const char *currentMapName, uint8_t x, uint8_t y) {
    if (questDefCount == 0 || questLocationCount == 0) return;

    /* Activation triggers */
    for (int i = 0; i < questDefCount; i++) {
        if (questSt.status[i] != QUEST_INACTIVE) continue;
        if (questDefs[i].startType != TRIG_LOCATION) continue;
        uint8_t lid = questDefs[i].startId;
        if (lid >= questLocationCount) continue;
        const QuestLocation *loc = &questLocations[lid];
        if (loc->x != x || loc->y != y) continue;
        if (!locMapMatches(currentMapName, loc->mapId)) continue;
        questSt.status[i] = QUEST_ACTIVE;
    }

    /* Objective progress */
    for (int i = 0; i < questDefCount; i++) {
        if (questSt.status[i] != QUEST_ACTIVE) continue;
        const QuestDef *q = &questDefs[i];
        for (int j = 0; j < q->objectiveCount; j++) {
            const QuestObjective *o = &q->objectives[j];
            if (o->type != OBJ_VISIT_LOCATION) continue;
            if (questSt.progress[i][j] >= o->required) continue;
            uint8_t lid = o->targetId;
            if (lid >= questLocationCount) continue;
            const QuestLocation *loc = &questLocations[lid];
            if (loc->x != x || loc->y != y) continue;
            if (!locMapMatches(currentMapName, loc->mapId)) continue;
            questSt.progress[i][j]++;
            if (questSt.progress[i][j] < o->required) {
                char l1[48];
                snprintf(l1, sizeof(l1), "%.18s: location %d/%d",
                         q->name, questSt.progress[i][j], o->required);
                pushNotif(l1, "");
            }
        }
        checkCompletion(i);
    }
}

/* -----------------------------------------------------------------------
 * Quest log UI
 * ----------------------------------------------------------------------- */

static const char *objTypeName(uint8_t t) {
    switch (t) {
        case OBJ_KILL:           return "Kill";
        case OBJ_GET_ITEM:       return "Obtain";
        case OBJ_VISIT_ZONE:     return "Visit";
        case OBJ_TALK_NPC:       return "Talk";
        case OBJ_VISIT_LOCATION: return "Reach";
        default:                 return "???";
    }
}

static void objTargetName(const QuestObjective *o, char *buf, int bufLen) {
    switch (o->type) {
        case OBJ_KILL:
            if (o->targetId < (uint8_t)enemyDefCount)
                snprintf(buf, (size_t)bufLen, "%s", enemyDefs[o->targetId].name);
            else
                snprintf(buf, (size_t)bufLen, "enemy #%d", o->targetId);
            break;
        case OBJ_GET_ITEM:
            snprintf(buf, (size_t)bufLen, "%s", itemName(o->targetId));
            break;
        case OBJ_VISIT_ZONE:
            snprintf(buf, (size_t)bufLen, "zone 0x%02X", o->targetId);
            break;
        case OBJ_TALK_NPC:
            snprintf(buf, (size_t)bufLen, "NPC tree %d", o->targetId);
            break;
        case OBJ_VISIT_LOCATION: {
            uint8_t lid = o->targetId;
            if (lid < questLocationCount)
                snprintf(buf, (size_t)bufLen, "%s (%d,%d)",
                         questLocations[lid].mapId,
                         questLocations[lid].x,
                         questLocations[lid].y);
            else
                snprintf(buf, (size_t)bufLen, "location #%d", lid);
            break;
        }
        default:
            snprintf(buf, (size_t)bufLen, "???");
            break;
    }
}

void handleQuestLogInput(int key) {
    /* Count displayable quests (active + done, not inactive hidden) */
    int visible = 0;
    for (int i = 0; i < questDefCount; i++) {
        uint8_t st = questSt.status[i];
        if (st == QUEST_INACTIVE) continue;
        if ((questDefs[i].flags & QUEST_HIDDEN) && st == QUEST_INACTIVE) continue;
        visible++;
    }

    switch (key) {
        case VK_UP:
            if (questLogSt.sel > 0) questLogSt.sel--;
            break;
        case VK_DOWN:
            if (questLogSt.sel < visible - 1) questLogSt.sel++;
            break;
        case VK_ESCAPE:
        case 'J':
            state = questLogSt.returnState;
            break;
    }
}

void renderQuestLog(void) {
    const int boxX  = 40;
    const int boxY  = 40;
    const int boxW  = gfxWidth  - 80;
    const int boxH  = gfxHeight - 80;
    const int textX = boxX + 14;
    const int lineH = 20;
    const int scale = 2;

    fillRect(boxX, boxY, boxW, boxH, rgb(5, 15, 40));
    fillRect(boxX, boxY, boxW, 2, rgb(80, 120, 200));

    int y = boxY + 8;
    drawText(textX, y, "QUEST LOG", rgb(220, 200, 100), scale);
    drawText(textX + 200, y, "J / Esc to close", rgb(80, 80, 100), 1);
    y += lineH + 4;

    if (questDefCount == 0) {
        drawText(textX, y, "No quests loaded.", rgb(100, 100, 100), scale);
        return;
    }

    /* Separate active and done quests into two sections */
    int visIdx  = 0;
    int anyShown = 0;

    /* Section: Active */
    int activeCount = 0;
    for (int i = 0; i < questDefCount; i++)
        if (questSt.status[i] == QUEST_ACTIVE) activeCount++;

    if (activeCount > 0) {
        drawText(textX, y, "-- Active --", rgb(100, 140, 200), 1);
        y += 14;

        for (int i = 0; i < questDefCount; i++) {
            if (questSt.status[i] != QUEST_ACTIVE) continue;
            if (y + lineH > boxY + boxH - 10) break;

            const QuestDef *q = &questDefs[i];
            int sel = (visIdx == questLogSt.sel);
            uint32_t nameColor = sel ? rgb(255, 255, 100) : rgb(220, 220, 220);

            char buf[64];
            snprintf(buf, sizeof(buf), "%s%s%s",
                sel ? "> " : "  ",
                q->name,
                (q->flags & QUEST_MAIN) ? "  [main]" : "");
            drawText(textX, y, buf, nameColor, scale);
            y += lineH;

            /* Show objectives for selected quest */
            if (sel) {
                for (int j = 0; j < q->objectiveCount; j++) {
                    if (y + 14 > boxY + boxH - 10) break;
                    const QuestObjective *o = &q->objectives[j];
                    char tgtBuf[20];
                    objTargetName(o, tgtBuf, sizeof(tgtBuf));
                    uint8_t prog = questSt.progress[i][j];
                    int done = (prog >= o->required);
                    snprintf(buf, sizeof(buf), "    %s %s: %d/%d%s",
                        objTypeName(o->type), tgtBuf,
                        prog, o->required,
                        done ? " [done]" : "");
                    drawText(textX, y, buf, done ? rgb(100, 200, 100) : rgb(160, 160, 160), 1);
                    y += 14;
                }
                y += 4;
            }

            visIdx++;
            anyShown = 1;
        }
    }

    /* Section: Completed */
    int doneCount = 0;
    for (int i = 0; i < questDefCount; i++)
        if (questSt.status[i] == QUEST_DONE) doneCount++;

    if (doneCount > 0 && y + 14 < boxY + boxH - 10) {
        y += 4;
        drawText(textX, y, "-- Completed --", rgb(80, 120, 80), 1);
        y += 14;

        for (int i = 0; i < questDefCount; i++) {
            if (questSt.status[i] != QUEST_DONE) continue;
            if (y + lineH > boxY + boxH - 10) break;

            int sel = (visIdx == questLogSt.sel);
            char buf[48];
            snprintf(buf, sizeof(buf), "%s[x] %s",
                sel ? "> " : "  ",
                questDefs[i].name);
            drawText(textX, y, buf, sel ? rgb(180, 220, 180) : rgb(80, 130, 80), scale);
            y += lineH;
            visIdx++;
            anyShown = 1;
        }
    }

    if (!anyShown)
        drawText(textX, y, "No active quests.", rgb(100, 100, 100), scale);
}

void renderQuestNotifications(void) {
    if (g_notifCount == 0) return;

    DWORD now = GetTickCount();

    /* Expire old entries */
    int i = 0;
    while (i < g_notifCount) {
        if (now >= g_notifs[i].expiry) {
            /* Shift remaining down */
            for (int k = i; k < g_notifCount - 1; k++)
                g_notifs[k] = g_notifs[k + 1];
            g_notifCount--;
        } else {
            i++;
        }
    }

    /* Draw from top of stack downward, top-right corner */
    const int lineH  = 14;
    const int padX   = 8;
    const int startY = 8;

    for (int n = 0; n < g_notifCount; n++) {
        int y = startY + n * (lineH * 2 + 4);
        int x1 = gfxWidth - (int)strlen(g_notifs[n].line1) * 8 - padX;
        drawText(x1, y, g_notifs[n].line1, rgb(180, 230, 180), 1);
        if (g_notifs[n].line2[0]) {
            int x2 = gfxWidth - (int)strlen(g_notifs[n].line2) * 8 - padX;
            drawText(x2, y + lineH, g_notifs[n].line2, rgb(255, 220, 80), 1);
        }
    }
}
