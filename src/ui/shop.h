#pragma once
#include <stdint.h>
#include "game.h"
#include "pak.h"

#define SHOP_DEF_MAX   32
#define SHOP_STOCK_MAX 16

typedef struct {
    char    name[24];
    uint8_t stock[SHOP_STOCK_MAX];
    uint8_t count;
    uint8_t _pad[7];
} ShopDef; /* 48 bytes */

extern ShopDef shopDefs[SHOP_DEF_MAX];
extern int     shopDefCount;

int  loadShops(PakData data);
void enterShop(uint8_t shopId, GameState returnTo);
void handleShopInput(int key);
void renderShop(void);
