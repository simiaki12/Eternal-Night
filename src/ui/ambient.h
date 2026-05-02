#pragma once
#include <stdint.h>
#include "pak.h"

#define MAX_AMBIENT 128

#define AMBIENT_ALWAYS       0
#define AMBIENT_ONCE_SESSION 1
#define AMBIENT_ONCE_EVER    2

typedef struct {
    char    mapId[8];
    uint8_t leftX, rightX, topY, bottomY;
    uint8_t flags;
    uint8_t questIdx;    /* 0xFF = no quest condition */
    uint8_t questStatus; /* required quest status when questIdx != 0xFF */
    uint8_t _pad[1];
    char    text[64];
} AmbientEntry; /* 80 bytes */

extern AmbientEntry ambientDefs[MAX_AMBIENT];
extern int          ambientCount;

int  loadAmbient(PakData data);
void ambientCheckLocation(const char *mapName, uint8_t x, uint8_t y);
