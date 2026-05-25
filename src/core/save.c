#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "save.h"
#include "player.h"
#include "items.h"
#include "quests.h"
#include "world.h"

#define SAVE_MAGIC   0x53415645u  /* "SAVE" */
#define SAVE_VERSION 10  /* bumped: PlayerData gained worldFlags[16] */

/* Layout (fixed, do not reorder):
 *   4  magic
 *   1  version
 *   sizeof(PlayerData)
 *   sizeof(Inventory)
 *   sizeof(QuestState)
 *   64 currentMapName
 *   4  worldPlayerX
 *   4  worldPlayerY
 */

static int write_save(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) return 0;

    uint32_t magic   = SAVE_MAGIC;
    uint8_t  version = SAVE_VERSION;

    fwrite(&magic,         4,                   1, f);
    fwrite(&version,       1,                   1, f);
    fwrite(&player,        sizeof(PlayerData),  1, f);
    fwrite(&inventory,     sizeof(Inventory),   1, f);
    fwrite(&questSt,       sizeof(QuestState),  1, f);
    fwrite(currentMapName, 64,                  1, f);
    fwrite(&worldPlayerX,  sizeof(int),         1, f);
    fwrite(&worldPlayerY,  sizeof(int),         1, f);

    fclose(f);
    return 1;
}

int saveGameNew(void) {
    return write_save("quicksave.sav");
}

int saveGameAs(const char *name) {
    /* Sanitize: keep alphanumeric and spaces (→ underscore), max 32 chars */
    char safe[33];
    int j = 0;
    for (int i = 0; name[i] && j < 32; i++) {
        char c = name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            safe[j++] = c;
        else if (c == ' ' && j > 0)
            safe[j++] = '_';
    }
    safe[j] = '\0';
    if (j == 0) return 0;

    char filename[40];
    snprintf(filename, sizeof(filename), "%s.sav", safe);
    return write_save(filename);
}

int loadGameNamed(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;

    uint32_t magic;
    uint8_t  version;

    if (fread(&magic,   4, 1, f) != 1 || magic != SAVE_MAGIC)     goto fail;
    if (fread(&version, 1, 1, f) != 1 || version != SAVE_VERSION)  goto fail;

    if (fread(&player,        sizeof(PlayerData), 1, f) != 1) goto fail;
    if (fread(&inventory,     sizeof(Inventory),  1, f) != 1) goto fail;
    if (fread(&questSt,       sizeof(QuestState), 1, f) != 1) goto fail;
    if (fread(currentMapName, 64,                 1, f) != 1) goto fail;
    if (fread(&worldPlayerX,  sizeof(int),        1, f) != 1) goto fail;
    if (fread(&worldPlayerY,  sizeof(int),        1, f) != 1) goto fail;

    fclose(f);
    currentMapName[63] = '\0';
    int savedX = worldPlayerX;
    int savedY = worldPlayerY;
    worldLoadNamed(currentMapName);
    worldPlayerX = savedX;
    worldPlayerY = savedY;
    worldUpdateCamera();
    return 1;

fail:
    fclose(f);
    return 0;
}

int listSaves(char names[][64], int maxCount) {
    int count = 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("*.sav", &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (count >= maxCount) break;
        strncpy(names[count], fd.cFileName, 63);
        names[count][63] = '\0';
        count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return count;
}

int anySaveExists(void) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("*.sav", &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FindClose(h);
    return 1;
}
