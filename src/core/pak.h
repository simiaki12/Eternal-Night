#pragma once
#include <stdint.h>

typedef struct {
    char     name[64];
    uint32_t offset;
    uint32_t size;
} PakEntry;

typedef struct {
    uint8_t* data;
    uint32_t size;
} PakData;

int             pakOpen(const char* filename);
const PakEntry* pakFind(const char* name);
PakData         pakRead(const char* name);
void            pakClose(void);
