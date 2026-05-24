/* tools/seed_shops.c — writes assets/data/shops.dat
 * Run once: make seed_shops
 * Edit content here and re-run to regenerate. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define SHOP_STOCK_MAX 16

typedef struct {
    char    name[24];
    uint8_t stock[SHOP_STOCK_MAX];
    uint8_t count;
    uint8_t _pad[7];
} ShopDef; /* 48 bytes */

typedef char check_size[(sizeof(ShopDef) == 48) ? 1 : -1];

/* Item IDs — must match order in assets/data/items.dat (see seed_items.c) */
#define ITEM_IRON_SWORD      0
#define ITEM_LEATHER_ARMOR   1
#define ITEM_HEALTH_POTION   2
#define ITEM_FLAMING_SWORD   3
#define ITEM_ENFORCERS_BADGE 4
#define ITEM_MOURNING_RING   5
#define ITEM_HUNTERS_COMPASS 6
#define ITEM_IRON_PENDANT    7
#define ITEM_BLOODWATCH      8
#define ITEM_GHOST_EARRING   9
#define ITEM_THIEVES_TOKEN   10
#define ITEM_PADDED_SHIRT    11
#define ITEM_THERMAL_WRAPS   12
#define ITEM_BLOODWEAVE_VEST 13
#define ITEM_LANTERN_OF_DUSK 14
#define ITEM_FRACTURED_COIN  15
#define ITEM_CORONERS_KEY    16
#define ITEM_HEALTH_TONIC    17
#define ITEM_ADRENALINE_SHOT 18
#define ITEM_SMOKE_PELLET    19
#define ITEM_NIGHT_SALVE     20
#define ITEM_FOCUSED_DRAUGHT 21
#define ITEM_FORMAL_COAT     22
#define ITEM_VAGRANTS_RAGS   23

static ShopDef shops[] = {
    /* Shop 0 — B-key debug shop / black market */
    { "Black Market", {ITEM_FLAMING_SWORD, ITEM_HEALTH_TONIC, ITEM_ADRENALINE_SHOT, ITEM_SMOKE_PELLET}, 4, {0} },
};

int main(void) {
    (void)sizeof(check_size);

    FILE *f = fopen("assets/data/shops.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot write assets/data/shops.dat\n"); return 1; }

    int n = (int)(sizeof(shops) / sizeof(shops[0]));
    fwrite(&(uint8_t){(uint8_t)n}, 1, 1, f);
    fwrite(shops, sizeof(ShopDef), n, f);
    fclose(f);

    printf("Written assets/data/shops.dat (%d shop(s), %d bytes)\n",
        n, 1 + n * (int)sizeof(ShopDef));
    return 0;
}
