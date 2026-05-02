#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#define MAX_W         255
#define MAX_H         255
#define MAX_MAPFILES  32
#define MAX_MAP_EVENTS 255

/* Tile types — keep in sync with src/world/world.h */
#define GFX_GRASS          0
#define GFX_WALL           1
#define GFX_TREE           2
#define GFX_RIVER          3
#define GFX_BRIDGE         4
#define GFX_ROAD           5
#define GFX_BUILDING_FLOOR 6
#define GFX_HILLS          7
#define GFX_MOUNTAINS      8
#define GFX_CAVE_FLOOR     9
#define GFX_CAVE_WALL      10
#define GFX_TAVERN_WALL    11
#define IS_GFX_PASSABLE(g) ((g)==GFX_GRASS||(g)==GFX_BRIDGE||(g)==GFX_ROAD|| \
                            (g)==GFX_BUILDING_FLOOR||(g)==GFX_HILLS||(g)==GFX_CAVE_FLOOR)

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

static uint8_t   mapGfx[MAX_W * MAX_H];
static MapEvent  events[MAX_MAP_EVENTS];
static int       eventCount = 0;
static int       mapW = 10, mapH = 8;
static int       curX = 0, curY = 0;
static int       spawnX = 2, spawnY = 2;
static char      outfile[64] = "assets/maps/map1.bin";

/* --- Event helpers ------------------------------------------------ */

static MapEvent *findEvAt(int x, int y) {
    for (int i = 0; i < eventCount; i++)
        if (events[i].x == (uint8_t)x && events[i].y == (uint8_t)y)
            return &events[i];
    return NULL;
}

static void removeEvAt(int x, int y) {
    for (int i = 0; i < eventCount; i++) {
        if (events[i].x == (uint8_t)x && events[i].y == (uint8_t)y) {
            events[i] = events[--eventCount];
            return;
        }
    }
}

static MapEvent *getOrAddEv(int x, int y) {
    MapEvent *ev = findEvAt(x, y);
    if (ev) return ev;
    if (eventCount >= MAX_MAP_EVENTS) return NULL;
    ev = &events[eventCount++];
    memset(ev, 0, sizeof(*ev));
    ev->x = (uint8_t)x;
    ev->y = (uint8_t)y;
    return ev;
}

/* --- File discovery ----------------------------------------------- */

static char mapFiles[MAX_MAPFILES][64];
static int  mapFileCount = 0;

static void scanMaps(void) {
    mapFileCount = 0;
    DIR *d = opendir("assets/maps");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && mapFileCount < MAX_MAPFILES) {
        int len = (int)strlen(e->d_name);
        if (len > 4 && len < 51 && strcmp(e->d_name + len - 4, ".bin") == 0) {
            char *p = mapFiles[mapFileCount++];
            memcpy(p, "assets/maps/", 12);
            memcpy(p + 12, e->d_name, (size_t)len + 1);
        }
    }
    closedir(d);
    for (int i = 0; i < mapFileCount - 1; i++)
        for (int j = i + 1; j < mapFileCount; j++)
            if (strcmp(mapFiles[i], mapFiles[j]) > 0) {
                char tmp[64];
                memcpy(tmp, mapFiles[i], 64);
                memcpy(mapFiles[i], mapFiles[j], 64);
                memcpy(mapFiles[j], tmp, 64);
            }
}

static void resetMapState(void) {
    memset(mapGfx, 0, sizeof(mapGfx));
    eventCount = 0;
    mapW = 10; mapH = 8;
    spawnX = 2; spawnY = 2;
    curX = 0; curY = 0;
}

/* --- File I/O (new format) --------------------------------------- */

static void loadMap(void) {
    FILE *f = fopen(outfile, "rb");
    if (!f) return;

    uint8_t hdr[4];
    if (fread(hdr, 1, 4, f) < 4) { fclose(f); return; }
    mapW   = hdr[0]; mapH   = hdr[1];
    spawnX = hdr[2]; spawnY = hdr[3];

    int n = mapW * mapH;
    (void)fread(mapGfx, 1, (size_t)n, f);

    uint8_t ec = 0;
    if (fread(&ec, 1, 1, f) == 1) {
        eventCount = ec;
        if (eventCount > MAX_MAP_EVENTS) eventCount = MAX_MAP_EVENTS;
        (void)fread(events, sizeof(MapEvent), (size_t)eventCount, f);
    }
    fclose(f);
}

static void saveMap(void) {
    FILE *f = fopen(outfile, "wb");
    if (!f) return;

    uint8_t hdr[4] = { (uint8_t)mapW, (uint8_t)mapH, (uint8_t)spawnX, (uint8_t)spawnY };
    fwrite(hdr, 1, 4, f);
    fwrite(mapGfx, 1, (size_t)(mapW * mapH), f);
    uint8_t ec = (uint8_t)eventCount;
    fwrite(&ec, 1, 1, f);
    fwrite(events, sizeof(MapEvent), (size_t)eventCount, f);
    fclose(f);
}

/* --- Display ------------------------------------------------------ */

static const char *gfxName(uint8_t gfx) {
    switch (gfx) {
        case GFX_GRASS:          return "grass";
        case GFX_WALL:           return "wall";
        case GFX_TREE:           return "tree";
        case GFX_RIVER:          return "river";
        case GFX_BRIDGE:         return "bridge";
        case GFX_ROAD:           return "road";
        case GFX_BUILDING_FLOOR: return "bldg floor";
        case GFX_HILLS:          return "hills";
        case GFX_MOUNTAINS:      return "mountains";
        case GFX_CAVE_FLOOR:     return "cave floor";
        case GFX_CAVE_WALL:      return "cave wall";
        case GFX_TAVERN_WALL:    return "tavern wall";
        default:                 return "?";
    }
}

static void drawMap(void) {
    for (int y = 0; y < mapH; y++) {
        for (int x = 0; x < mapW; x++) {
            uint8_t       gfx = mapGfx[y * mapW + x];
            const MapEvent *ev = findEvAt(x, y);
            char ch;
            int  attr;

            if (x == spawnX && y == spawnY && IS_GFX_PASSABLE(gfx)) {
                ch = '@'; attr = COLOR_PAIR(6);
            } else if (ev) {
                switch (ev->type) {
                    case MAP_EV_ENEMY:
                        ch   = (ev->id <= 9) ? ('0' + ev->id) : ('A' + ev->id - 10);
                        attr = COLOR_PAIR(2);
                        break;
                    case MAP_EV_TOWN:
                        ch = 'T'; attr = COLOR_PAIR(3); break;
                    case MAP_EV_DUNGEON:
                        ch = 'D'; attr = COLOR_PAIR(4); break;
                    case MAP_EV_PORTAL:
                        ch = 'P'; attr = COLOR_PAIR(7); break;
                    default:
                        ch = '?'; attr = COLOR_PAIR(1); break;
                }
            } else {
                switch (gfx) {
                    case GFX_WALL:           ch = '#'; attr = COLOR_PAIR(1);           break;
                    case GFX_TREE:           ch = '^'; attr = COLOR_PAIR(5) | A_BOLD;  break;
                    case GFX_RIVER:          ch = '~'; attr = COLOR_PAIR(7);           break;
                    case GFX_BRIDGE:         ch = '='; attr = COLOR_PAIR(3);           break;
                    case GFX_ROAD:           ch = ':'; attr = COLOR_PAIR(1);           break;
                    case GFX_BUILDING_FLOOR: ch = '+'; attr = COLOR_PAIR(6);           break;
                    case GFX_HILLS:          ch = 'n'; attr = COLOR_PAIR(5);           break;
                    case GFX_MOUNTAINS:      ch = 'M'; attr = COLOR_PAIR(1) | A_BOLD;  break;
                    case GFX_CAVE_FLOOR:     ch = ','; attr = COLOR_PAIR(4);           break;
                    case GFX_CAVE_WALL:      ch = '%'; attr = COLOR_PAIR(4) | A_BOLD;  break;
                    case GFX_TAVERN_WALL:    ch = '|'; attr = COLOR_PAIR(3) | A_BOLD;  break;
                    default:                 ch = '.'; attr = COLOR_PAIR(5);           break;
                }
            }

            if (x == curX && y == curY) attr |= A_REVERSE;
            attron(attr);
            mvaddch(y + 3, x * 2, ch);
            attroff(attr);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc >= 2) { strncpy(outfile, argv[1], sizeof(outfile)-1); outfile[sizeof(outfile)-1] = '\0'; }

    int fileFound = 1;
    { FILE *probe = fopen(outfile, "rb"); if (!probe) fileFound = 0; else fclose(probe); }

    loadMap();

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    start_color();

    init_pair(1, COLOR_WHITE,   COLOR_BLACK);  /* wall/road  */
    init_pair(2, COLOR_RED,     COLOR_BLACK);  /* enemy      */
    init_pair(3, COLOR_YELLOW,  COLOR_BLACK);  /* town       */
    init_pair(4, COLOR_MAGENTA, COLOR_BLACK);  /* dungeon    */
    init_pair(5, COLOR_GREEN,   COLOR_BLACK);  /* floor/tree */
    init_pair(6, COLOR_CYAN,    COLOR_BLACK);  /* spawn      */
    init_pair(7, COLOR_BLUE,    COLOR_BLACK);  /* portal     */

    int running = 1;
    int dirty   = 0;

    while (running) {
        clear();
        mvprintw(0, 0, "MAP EDITOR  %dx%d  spawn(%d,%d)  [%s]  %s",
                 mapW, mapH, spawnX, spawnY,
                 dirty ? "unsaved" : "saved",
                 fileFound ? outfile : "NEW FILE");
        if (!fileFound) {
            attron(COLOR_PAIR(2));
            mvprintw(1, 0, "WARNING: '%s' not found — starting blank map", outfile);
            attroff(COLOR_PAIR(2));
        }
        mvprintw(1 + !fileFound, 0,
                 "Arrows=move  W=wall F=floor A=tree V=river B=bridge K=road U=bldg H=hills M=mount E=cavefloor X=cavewall Y=tavern");
        mvprintw(2 + !fileFound, 0,
                 "1-9=enemy  T=town  D=dungeon  R=portal  DEL=rm event  C=clear  P=spawn  S=save  O=open  N=new  Q=quit");

        drawMap();

        /* Status line: event at cursor */
        const MapEvent *cev = findEvAt(curX, curY);
        char evDesc[128] = "none";
        if (cev) {
            switch (cev->type) {
                case MAP_EV_ENEMY:
                    snprintf(evDesc, sizeof(evDesc), "enemy pool %d", cev->id);
                    break;
                case MAP_EV_TOWN:
                    snprintf(evDesc, sizeof(evDesc), "town");
                    break;
                case MAP_EV_DUNGEON:
                    snprintf(evDesc, sizeof(evDesc), "dungeon%s%s",
                             cev->destMap[0] ? " -> " : "", cev->destMap);
                    break;
                case MAP_EV_PORTAL:
                    snprintf(evDesc, sizeof(evDesc), "portal -> %s @ (%d,%d)",
                             cev->destMap[0] ? cev->destMap : "(unset)",
                             cev->destX, cev->destY);
                    break;
            }
        }
        mvprintw(mapH + 4, 0, "(%d,%d)  gfx=%s  event=%s  total events=%d",
                 curX, curY, gfxName(mapGfx[curY * mapW + curX]), evDesc, eventCount);

        refresh();

        int ch = getch();
        int idx = curY * mapW + curX;

        switch (ch) {
            case KEY_UP:    if (curY > 0)      curY--; break;
            case KEY_DOWN:  if (curY < mapH-1) curY++; break;
            case KEY_LEFT:  if (curX > 0)      curX--; break;
            case KEY_RIGHT: if (curX < mapW-1) curX++; break;

            /* Tile painting */
            case 'w': case 'W':
                mapGfx[idx] = GFX_WALL; removeEvAt(curX, curY); dirty = 1; break;
            case 'f': case 'F':
                mapGfx[idx] = GFX_GRASS; dirty = 1; break;
            case 'a': case 'A':
                mapGfx[idx] = GFX_TREE; removeEvAt(curX, curY); dirty = 1; break;
            case 'v': case 'V':
                mapGfx[idx] = GFX_RIVER; removeEvAt(curX, curY); dirty = 1; break;
            case 'b': case 'B':
                mapGfx[idx] = GFX_BRIDGE; dirty = 1; break;
            case 'k': case 'K':
                mapGfx[idx] = GFX_ROAD; dirty = 1; break;
            case 'u': case 'U':
                mapGfx[idx] = GFX_BUILDING_FLOOR; dirty = 1; break;
            case 'h': case 'H':
                mapGfx[idx] = GFX_HILLS; dirty = 1; break;
            case 'm': case 'M':
                mapGfx[idx] = GFX_MOUNTAINS; removeEvAt(curX, curY); dirty = 1; break;
            case 'e': case 'E':
                mapGfx[idx] = GFX_CAVE_FLOOR; dirty = 1; break;
            case 'x': case 'X':
                mapGfx[idx] = GFX_CAVE_WALL; removeEvAt(curX, curY); dirty = 1; break;
            case 'y': case 'Y':
                mapGfx[idx] = GFX_TAVERN_WALL; removeEvAt(curX, curY); dirty = 1; break;
            case 'c': case 'C':
                mapGfx[idx] = GFX_GRASS; removeEvAt(curX, curY); dirty = 1; break;

            /* Event placement */
            case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9': {
                MapEvent *ev = getOrAddEv(curX, curY);
                if (ev) { ev->type = MAP_EV_ENEMY; ev->id = (uint8_t)(ch - '0'); dirty = 1; }
                break;
            }
            case 't': case 'T': {
                MapEvent *ev = getOrAddEv(curX, curY);
                if (ev) { ev->type = MAP_EV_TOWN; ev->id = 0; dirty = 1; }
                break;
            }
            case 'd': case 'D': {
                echo(); curs_set(1);
                mvprintw(mapH + 6, 0, "Dest map (blank=dungeon state): assets/maps/");
                clrtoeol(); refresh();
                char dest[19] = {0};
                getnstr(dest, (int)sizeof(dest) - 1);
                noecho(); curs_set(0);
                MapEvent *ev = getOrAddEv(curX, curY);
                if (ev) {
                    ev->type = MAP_EV_DUNGEON;
                    ev->id   = 0;
                    if (dest[0])
                        snprintf(ev->destMap, sizeof(ev->destMap), "assets/maps/%s", dest);
                    else
                        ev->destMap[0] = '\0';
                    dirty = 1;
                }
                break;
            }
            case 'r': case 'R': {
                echo(); curs_set(1);
                mvprintw(mapH + 6, 0, "Portal dest: assets/maps/");
                clrtoeol(); refresh();
                char dest[19] = {0};
                getnstr(dest, (int)sizeof(dest) - 1);
                mvprintw(mapH + 7, 0, "Spawn X in dest: ");
                clrtoeol(); refresh();
                char numBuf[8] = {0};
                getnstr(numBuf, (int)sizeof(numBuf) - 1);
                int sx = atoi(numBuf);
                mvprintw(mapH + 8, 0, "Spawn Y in dest: ");
                clrtoeol(); refresh();
                memset(numBuf, 0, sizeof(numBuf));
                getnstr(numBuf, (int)sizeof(numBuf) - 1);
                int sy = atoi(numBuf);
                noecho(); curs_set(0);
                if (dest[0]) {
                    MapEvent *ev = getOrAddEv(curX, curY);
                    if (ev) {
                        ev->type  = MAP_EV_PORTAL;
                        ev->id    = 0;
                        ev->destX = (uint8_t)sx;
                        ev->destY = (uint8_t)sy;
                        snprintf(ev->destMap, sizeof(ev->destMap), "assets/maps/%s", dest);
                        dirty = 1;
                    }
                }
                break;
            }
            case KEY_DC: case 127: /* DEL / backspace */
                removeEvAt(curX, curY);
                dirty = 1;
                break;

            case 'p': case 'P':
                if (IS_GFX_PASSABLE(mapGfx[idx])) { spawnX = curX; spawnY = curY; dirty = 1; }
                break;

            case 's': case 'S':
                saveMap(); dirty = 0; fileFound = 1; break;

            case 'o': case 'O': {
                if (dirty) { saveMap(); dirty = 0; fileFound = 1; }
                scanMaps();
                int sel = 0;
                for (int i = 0; i < mapFileCount; i++)
                    if (strcmp(mapFiles[i], outfile) == 0) { sel = i; break; }
                int browsing = 1;
                while (browsing) {
                    clear();
                    mvprintw(0, 0, "OPEN MAP — Arrows=select  Enter=open  Esc=cancel");
                    for (int i = 0; i < mapFileCount; i++) {
                        if (i == sel) attron(A_REVERSE);
                        mvprintw(i + 2, 2, "%s", mapFiles[i]);
                        if (i == sel) attroff(A_REVERSE);
                    }
                    refresh();
                    int c = getch();
                    if      (c == KEY_UP   && sel > 0)             sel--;
                    else if (c == KEY_DOWN && sel < mapFileCount-1) sel++;
                    else if (c == '\n' || c == KEY_ENTER) {
                        strncpy(outfile, mapFiles[sel], sizeof(outfile)-1);
                        outfile[sizeof(outfile)-1] = '\0';
                        resetMapState();
                        loadMap();
                        fileFound = 1;
                        browsing = 0;
                    } else if (c == 27) browsing = 0;
                }
                break;
            }

            case 'n': case 'N': {
                if (dirty) { saveMap(); dirty = 0; fileFound = 1; }
                echo(); curs_set(1);
                mvprintw(mapH + 6, 0, "New map name: assets/maps/");
                clrtoeol();
                char nameBuf[32] = {0};
                getnstr(nameBuf, (int)sizeof(nameBuf)-1);
                if (nameBuf[0]) {
                    char dimBuf[8] = {0};
                    mvprintw(mapH + 7, 0, "Width  (1-%d): ", MAX_W);
                    clrtoeol(); refresh();
                    getnstr(dimBuf, (int)sizeof(dimBuf)-1);
                    int newW = atoi(dimBuf);
                    mvprintw(mapH + 8, 0, "Height (1-%d): ", MAX_H);
                    clrtoeol(); refresh();
                    memset(dimBuf, 0, sizeof(dimBuf));
                    getnstr(dimBuf, (int)sizeof(dimBuf)-1);
                    int newH = atoi(dimBuf);
                    noecho(); curs_set(0);
                    snprintf(outfile, sizeof(outfile), "assets/maps/%s.bin", nameBuf);
                    resetMapState();
                    if (newW >= 1 && newW <= MAX_W) mapW = newW;
                    if (newH >= 1 && newH <= MAX_H) mapH = newH;
                    fileFound = 0;
                    dirty = 1;
                } else {
                    noecho(); curs_set(0);
                }
                break;
            }

            case 'q': case 'Q':
                running = 0;
                break;
        }
    }

    endwin();

    if (dirty) {
        printf("Unsaved changes. Save? (y/n): ");
        fflush(stdout);
        int c = getchar();
        if (c == 'y' || c == 'Y') saveMap();
    }

    return 0;
}
