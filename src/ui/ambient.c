#include "ambient.h"
#include "quests.h"
#include "world.h"
#include <string.h>

AmbientEntry ambientDefs[MAX_AMBIENT];
int          ambientCount = 0;

static uint8_t g_shown[MAX_AMBIENT];

int loadAmbient(PakData data) {
    if (!data.data || data.size < 1) return 0;
    const uint8_t *p = data.data;
    uint8_t n = *p++;
    if (n > MAX_AMBIENT) n = MAX_AMBIENT;
    size_t available = (data.size - 1) / sizeof(AmbientEntry);
    if ((size_t)n > available) n = (uint8_t)available;
    memcpy(ambientDefs, p, (size_t)n * sizeof(AmbientEntry));
    ambientCount = n;
    memset(g_shown, 0, sizeof(g_shown));
    return ambientCount;
}

static int mapMatches(const char *mapId, const char *mapName) {
    const char *slash = strrchr(mapName, '/');
    const char *base  = slash ? slash + 1 : mapName;
    size_t idLen = strlen(mapId);
    return strncmp(base, mapId, idLen) == 0 &&
           (base[idLen] == '.' || base[idLen] == '\0');
}

void ambientCheckLocation(const char *mapName, uint8_t x, uint8_t y) {
    for (int i = 0; i < ambientCount; i++) {
        AmbientEntry *e = &ambientDefs[i];
        if (x < e->leftX || x > e->rightX || y < e->topY || y > e->bottomY) continue;
        if (!mapMatches(e->mapId, mapName)) continue;
        if (e->flags != AMBIENT_ALWAYS && g_shown[i]) continue;
        if (e->questIdx != 0xFF) {
            if (e->questIdx >= MAX_QUESTS) continue;
            if (questSt.status[e->questIdx] != e->questStatus) continue;
        }
        if (e->flags != AMBIENT_ALWAYS) g_shown[i] = 1;
        ambientShow(e->text);
        break;
    }
}
