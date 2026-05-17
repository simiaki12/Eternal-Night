#pragma once
#include <stdint.h>

typedef struct {
    int selected;
} TownState;

extern TownState townSt;

void startTown(void);
void handleTownInput(int key);
void renderTown(void);
