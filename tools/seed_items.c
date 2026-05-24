/* tools/seed_items.c — writes assets/items.dat
 * Run once: make seed_items
 * Edit content here and re-run to regenerate. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define ITEM_WEAPON       0
#define ITEM_OUTFIT       1
#define ITEM_CONSUMABLE   2
#define ITEM_UNDERGARMENT 3
#define ITEM_ACCESSORY    4
#define ITEM_RELIC        5

#define ITEM_FLAG_HEAL        0x01
#define ITEM_FLAG_BUFF_ATTACK 0x02

/* Action IDs — must match ActionId enum in actions.h */
#define ACTION_ATTACK   0
#define ACTION_STRONG   1
#define ACTION_HEAL     2
#define ACTION_DEFEND   3
#define NO_ACTION       0xFF

/* Must match ItemDef in items.h exactly — 64 bytes */
typedef struct {
    char     name[16];
    uint8_t  type;
    int8_t   attackBonus;
    int8_t   defenseBonus;
    int8_t   intelligenceBonus;
    int8_t   perceptionBonus;
    int8_t   staminaBonus;
    int8_t   hpBonus;
    uint8_t  flags;
    uint16_t price;
    char     description[24];
    uint8_t  actions[4];
    uint8_t  _pad[10];
} ItemDef;

/*            name              type            atk def int per sta  hp  flags              price  description                       actions */
static ItemDef items[] = {
    { "Iron Sword",    ITEM_WEAPON,      3,  1,  0,  1,  2,  0,  0,                 15, "A reliable iron blade.",  {ACTION_STRONG, NO_ACTION,     NO_ACTION, NO_ACTION} },
    { "Leather Armor", ITEM_OUTFIT,      0,  2,  0,  0,  0,  0,  0,                 12, "Light but protective.",   {ACTION_DEFEND, NO_ACTION,     NO_ACTION, NO_ACTION} },
    { "Health Potion", ITEM_CONSUMABLE,  0,  0,  0,  0,  0,  0,  ITEM_FLAG_HEAL,     8, "Restores 10 HP.",         {NO_ACTION,     NO_ACTION,     NO_ACTION, NO_ACTION} },
    { "Flaming Sword", ITEM_WEAPON,      6,  0,  2,  0,  0,  0,  0,                 40, "Burns with goblin fire.", {ACTION_STRONG, NO_ACTION,     NO_ACTION, NO_ACTION} },
};

int main(void) {
    /* Verify struct size at compile time */
    typedef char check_size[(sizeof(ItemDef) == 64) ? 1 : -1];
    (void)sizeof(check_size);

    FILE *f = fopen("assets/data/items.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot write assets/items.dat\n"); return 1; }

    int n = (int)(sizeof(items) / sizeof(items[0]));
    fwrite(&(uint8_t){(uint8_t)n}, 1, 1, f);
    fwrite(items, sizeof(ItemDef), n, f);
    fclose(f);

    printf("Written assets/items.dat  (%d items, %d bytes)\n",
        n, 1 + n * (int)sizeof(ItemDef));
    return 0;
}
