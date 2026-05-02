/* tools/migrate_map.c — convert old two-layer map format to new sparse-event format.
 *
 * Old: [w][h][w*h gfx][w*h loc][spawnX][spawnY][transition\0][portalCount][pid,sx,sy,dest\0]...
 * New: [w][h][spawnX][spawnY][w*h gfx][eventCount][eventCount × MapEvent(40 bytes)]
 *
 * Usage: migrate_map <mapfile> [mapfile2 ...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MAX_PORTALS     16
#define LOC_PORTAL_BASE 0xE0
#define MAX_MAP_EVENTS  255

typedef enum {
    MAP_EV_ENEMY   = 0,
    MAP_EV_TOWN    = 1,
    MAP_EV_DUNGEON = 2,
    MAP_EV_PORTAL  = 3,
} MapEventType;

typedef struct {
    uint8_t x, y;
    uint8_t type;
    uint8_t id;
    uint8_t destX, destY;
    char    destMap[32];
    uint8_t _pad[2];
} MapEvent; /* 40 bytes */

static int migrateFile(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open: %s\n", path); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    if (sz < 2) { fprintf(stderr, "%s: too small\n", path); fclose(f); return 1; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return 1; }
    (void)fread(buf, 1, (size_t)sz, f);
    fclose(f);

    int w = buf[0], h = buf[1];
    int n = w * h;
    if (sz < 2 + n + n) {
        fprintf(stderr, "%s: truncated (need %d bytes, got %ld)\n", path, 2 + n + n, sz);
        free(buf);
        return 1;
    }

    uint8_t *gfxLayer = buf + 2;
    uint8_t *locLayer = buf + 2 + n;

    /* Parse optional tail: spawnX, spawnY, transition\0, portalCount, portals */
    uint8_t spawnX = 2, spawnY = 2;
    char    transTarget[64] = {0};
    char    portalTargets[MAX_PORTALS][64] = {{0}};
    uint8_t portalSpawnX[MAX_PORTALS]      = {0};
    uint8_t portalSpawnY[MAX_PORTALS]      = {0};

    long tailOffset = 2 + n + n;
    if (sz > tailOffset + 1) {
        spawnX = buf[tailOffset];
        spawnY = buf[tailOffset + 1];
        long i = tailOffset + 2;
        int ti = 0;
        while (i < sz && buf[i] != 0 && ti < 63)
            transTarget[ti++] = (char)buf[i++];
        transTarget[ti] = '\0';
        if (i < sz) i++; /* skip null terminator */

        if (i < sz) {
            int pc = buf[i++];
            for (int p = 0; p < pc && i < sz; p++) {
                int pid = buf[i++];
                int sx  = (i < sz) ? buf[i++] : 0;
                int sy  = (i < sz) ? buf[i++] : 0;
                int di = 0;
                char dest[64] = {0};
                while (i < sz && buf[i] != 0 && di < 63)
                    dest[di++] = (char)buf[i++];
                dest[di] = '\0';
                if (i < sz) i++;
                if (pid >= 0 && pid < MAX_PORTALS) {
                    strncpy(portalTargets[pid], dest, 63);
                    portalSpawnX[pid] = (uint8_t)sx;
                    portalSpawnY[pid] = (uint8_t)sy;
                }
            }
        }
    }

    /* Build event list from loc layer */
    MapEvent events[MAX_MAP_EVENTS];
    int      eventCount = 0;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t loc = locLayer[y * w + x];
            if (loc == 0) continue;
            if (eventCount >= MAX_MAP_EVENTS) {
                fprintf(stderr, "%s: too many events (max %d)\n", path, MAX_MAP_EVENTS);
                break;
            }

            MapEvent *ev = &events[eventCount++];
            memset(ev, 0, sizeof(*ev));
            ev->x = (uint8_t)x;
            ev->y = (uint8_t)y;

            if (loc >= 1 && loc <= 15) {
                ev->type = MAP_EV_ENEMY;
                ev->id   = loc;
            } else if (loc == 0xFE) {
                ev->type = MAP_EV_TOWN;
                /* carry dungeon transition target if set */
                if (transTarget[0])
                    strncpy(ev->destMap, transTarget, 31);
            } else if (loc == 0xFF) {
                ev->type = MAP_EV_DUNGEON;
                if (transTarget[0])
                    strncpy(ev->destMap, transTarget, 31);
            } else if (loc >= LOC_PORTAL_BASE && loc < (uint8_t)(LOC_PORTAL_BASE + MAX_PORTALS)) {
                int pid = loc - LOC_PORTAL_BASE;
                ev->type  = MAP_EV_PORTAL;
                ev->destX = portalSpawnX[pid];
                ev->destY = portalSpawnY[pid];
                if (portalTargets[pid][0])
                    strncpy(ev->destMap, portalTargets[pid], 31);
            } else {
                eventCount--; /* unknown loc — skip */
            }
        }
    }

    /* Write new format */
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Cannot write: %s\n", path); free(buf); return 1; }

    uint8_t hdr[4] = { (uint8_t)w, (uint8_t)h, spawnX, spawnY };
    fwrite(hdr, 1, 4, f);
    fwrite(gfxLayer, 1, (size_t)n, f);
    uint8_t ec = (uint8_t)eventCount;
    fwrite(&ec, 1, 1, f);
    fwrite(events, sizeof(MapEvent), (size_t)eventCount, f);
    fclose(f);
    free(buf);

    printf("%s: %dx%d, spawn(%d,%d), %d event(s) migrated\n",
           path, w, h, spawnX, spawnY, eventCount);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: migrate_map <mapfile> [mapfile2 ...]\n");
        return 1;
    }
    int ret = 0;
    for (int i = 1; i < argc; i++)
        ret |= migrateFile(argv[i]);
    return ret;
}
