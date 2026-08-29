#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "npcs.h"
#include "game.h"
#include "gfx.h"
#include "pak.h"
#include "dialog.h"
#include "world.h"
#include <stdio.h>
#include "quests.h"

NpcDef npcDefs[NPC_DEF_MAX];

const NpcDef *npcGetDef(uint8_t id) {
    for (int i = 0; i < npcDefCount; i++)
        if (npcDefs[i].id == id) return &npcDefs[i];
    return NULL;
}

int npcIndexById(uint8_t id) {
    for (int i = 0; i < npcDefCount; i++)
        if (npcDefs[i].id == id) return i;
    return -1;
}

int    npcDefCount = 0;

/* Returns 1 if mapId exactly matches the base filename of currentMapName.
 * "map1" matches "assets/maps/map1.bin" but NOT "map14.bin" or "bigmap1.bin". */
static int npcMapMatches(const char *mapId) {
    const char *slash = strrchr(currentMapName, '/');
    const char *base  = slash ? slash + 1 : currentMapName;
    size_t idLen = strlen(mapId);
    return strncmp(base, mapId, idLen) == 0 &&
           (base[idLen] == '.' || base[idLen] == '\0');
}

/* Lazily-loaded sprites per NPC slot (NULL = not yet loaded or no image) */
static PakData npcImgs[NPC_DEF_MAX];
static int     npcImgsLoaded = 0;

/* Binary format: [1 byte count][N × sizeof(NpcDef)] */
int loadNpcs(PakData data) {
    if (!data.data || data.size < 1) return 0;

    const uint8_t *d = (const uint8_t *)data.data;
    uint8_t count = d[0];
    if (count > NPC_DEF_MAX) count = NPC_DEF_MAX;

    uint32_t expected = 1 + (uint32_t)count * sizeof(NpcDef);
    if (data.size < expected) return 0;

    memcpy(npcDefs, d + 1, (size_t)count * sizeof(NpcDef));
    npcDefCount   = count;
    npcImgsLoaded = 0;
    return npcDefCount;
}

static void ensureImgsLoaded(void) {
    if (npcImgsLoaded) return;
    for (int i = 0; i < npcDefCount; i++) {
        if (npcDefs[i].imgName[0]) {
            char path[32];
            snprintf(path, sizeof(path), "assets/sprites/%.2s.bin", npcDefs[i].imgName);
            npcImgs[i] = pakRead(path);
        } else {
            npcImgs[i].data = NULL;
            npcImgs[i].size = 0;
        }
    }
    npcImgsLoaded = 1;
}

void renderNpcs(int tx, int ty, int rCamX, int rCamY) {
    if (npcDefCount == 0) return;
    ensureImgsLoaded();

    for (int i = 0; i < npcDefCount; i++) {
        const NpcDef *n = &npcDefs[i];
        if (!n->mapId[0]) continue;
        if (!npcMapMatches(n->mapId)) continue;
        if ((int)n->x != tx || (int)n->y != ty) continue;

        /* Iso screen position of this tile's top vertex */
        int cx = (tx - ty) * (TILE_W / 2) - rCamX + gfxWidth  / 2;
        int cy = (tx + ty) * (TILE_H / 2) - rCamY + gfxHeight / 2;

        if (npcImgs[i].data) {
            /* 8x8 sprite at scale 4 = 32x32, bottom-centred on tile face */
            drawBin(cx - 16, cy + TILE_H / 2 - 32, (const uint8_t *)npcImgs[i].data, 4, 0, 255);
        } else {
            fillRect(cx - 6, cy + TILE_H / 2 - 14, 12, 14, rgb(200, 0, 200));
        }
    }
}

static int npcIsAdjacent(const NpcDef *n) {
    if (!n->mapId[0] || !npcMapMatches(n->mapId)) return 0;
    int dx = (int)n->x - worldPlayerX;
    int dy = (int)n->y - worldPlayerY;
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1) return 0;
    if (dx != 0 && dy != 0) return 0;
    return 1;
}

int npcTryInteract(void) {
    for (int i = 0; i < npcDefCount; i++) {
        const NpcDef *n = &npcDefs[i];
        if (n->treeId == 0xFF) continue;
        if (!npcIsAdjacent(n)) continue;
        if (n->treeId < (uint8_t)dialogTreeCount) {
            questOnDialogDone(n->treeId);
            startDialog(n->treeId, STATE_WORLD);
            return 1;
        }
    }
    return 0;
}

int npcGetPrompt(char *buf, int len) {
    for (int i = 0; i < npcDefCount; i++) {
        const NpcDef *n = &npcDefs[i];
        if (n->treeId == 0xFF) continue;
        if (!npcIsAdjacent(n)) continue;
        snprintf(buf, len, "[E]: Talk with %s", n->name[0] ? n->name : "Stranger");
        return 1;
    }
    return 0;
}

int npcGetInteractable(int *outIdxs, int max) {
    int count = 0;
    for (int i = 0; i < npcDefCount && count < max; i++) {
        const NpcDef *n = &npcDefs[i];
        if (n->treeId == 0xFF) continue;
        if (!npcIsAdjacent(n)) continue;
        outIdxs[count++] = i;
    }
    return count;
}

void npcTriggerByIdx(int idx) {
    if (idx < 0 || idx >= npcDefCount) return;
    const NpcDef *n = &npcDefs[idx];
    if (n->treeId == 0xFF || n->treeId >= (uint8_t)dialogTreeCount) return;
    questOnDialogDone(n->treeId);
    startDialog(n->treeId, STATE_WORLD);
}
