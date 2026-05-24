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
#define ACTION_ATTACK        0
#define ACTION_STRONG        1
#define ACTION_HEAL          2
#define ACTION_DEFEND        3
#define ACTION_DISARM        4
#define ACTION_BACKSTAB      5
#define ACTION_STUN          6
#define ACTION_CALM          7
#define ACTION_HIDE          8
#define ACTION_EXECUTE       9
#define ACTION_INTIMIDATE    10
#define ACTION_COUNTER       11
#define ACTION_WAR_CRY       12
#define ACTION_THREATEN      13
#define ACTION_AMBUSH        14
#define ACTION_VANISH        15
#define ACTION_POISON_BLADE  16
#define ACTION_SET_TRAP      17
#define ACTION_DECEIVE       18
#define ACTION_PICKPOCKET    19
#define ACTION_INSPECT       20
#define ACTION_TRACK         21
#define ACTION_BLOOD_DRAIN   22
#define ACTION_LETHAL_BITE   23
#define ACTION_BLOOD_HOWL    24
#define ACTION_BLOOD_SURGE   25
#define ACTION_BLOOD_SCENT   26
#define ACTION_DOMINATE      27
#define ACTION_MESMERIZE     28
#define ACTION_BRIBE         29
#define ACTION_SILVER_TONGUE 30
#define ACTION_INTERROGATE   31
#define ACTION_FEED          32
#define ACTION_BLEND_IN      33
#define ACTION_RALLY         34
#define ACTION_DEMAND        35
#define NO_ACTION            0xFF

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

/*             name               type               atk  def  int  per  sta   hp  flags                price  description                actions */
static ItemDef items[] = {
    /* Core gear (kept for reference / role swap) */
    { "Iron Sword",      ITEM_WEAPON,        3,   1,   0,   1,   2,   0,  0,                   15, "A reliable iron blade.",      {ACTION_STRONG,       NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Leather Armor",   ITEM_OUTFIT,        0,   2,   0,   0,   0,   0,  0,                   12, "Light but protective.",       {ACTION_DEFEND,       NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Health Potion",   ITEM_CONSUMABLE,    0,   0,   0,   0,   0,   0,  ITEM_FLAG_HEAL,       8, "Restores 10 HP.",             {NO_ACTION,           NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Flaming Sword",   ITEM_WEAPON,        6,   0,   2,   0,   0,   0,  0,                   40, "Burns with goblin fire.",     {ACTION_STRONG,       NO_ACTION, NO_ACTION, NO_ACTION} },

    /* Accessories */
    { "Enforcers Badge", ITEM_ACCESSORY,     0,   0,   1,   1,   0,   0,  0,                   30, "Commands respect.",           {ACTION_INTERROGATE,  NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Mourning Ring",   ITEM_ACCESSORY,     0,   0,   2,   0,   0,   0,  0,                   20, "Pre-blackout keepsake.",      {NO_ACTION,           NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Hunters Compass", ITEM_ACCESSORY,     0,   0,   0,   2,   0,   0,  0,                   25, "Finds the nearest foe.",      {ACTION_TRACK,        NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Iron Pendant",    ITEM_ACCESSORY,     0,   1,   0,   0,   1,   0,  0,                   18, "Dull heat in combat.",        {NO_ACTION,           NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Bloodwatch",      ITEM_ACCESSORY,     1,   0,   0,   0,   0,   0,  0,                   35, "Ticks near hostiles.",        {ACTION_BLOOD_SCENT,  NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Ghost Earring",   ITEM_ACCESSORY,     0,   0,   3,   0,   0,   0,  0,                   45, "Illegal arcana device.",      {ACTION_INSPECT,      NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Thieves Token",   ITEM_ACCESSORY,     0,   0,   0,   2,   0,   0,  0,                   15, "Opens locked dialogues.",     {ACTION_PICKPOCKET,   NO_ACTION, NO_ACTION, NO_ACTION} },

    /* Undergarments */
    { "Padded Shirt",    ITEM_UNDERGARMENT,  0,   1,   0,   0,   0,   0,  0,                    8, "Basic stab padding.",         {NO_ACTION,           NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Thermal Wraps",   ITEM_UNDERGARMENT,  0,   0,   0,   0,   1,   0,  0,                   10, "Cold-ward lining.",           {NO_ACTION,           NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Bloodweave Vest", ITEM_UNDERGARMENT,  0,   1,   0,   0,   0,   1,  0,                   28, "Rare treated cloth.",         {NO_ACTION,           NO_ACTION, NO_ACTION, NO_ACTION} },

    /* Relics */
    { "Lantern of Dusk", ITEM_RELIC,         0,   0,   2,   2,   0,   0,  0,                  100, "Pre-blackout artefact.",      {NO_ACTION,           NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Fractured Coin",  ITEM_RELIC,         3,   0,   0,   0,   0,   0,  0,                   80, "Last sunrise fragment.",      {ACTION_BLOOD_SURGE,  NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Coroners Key",    ITEM_RELIC,         0,   0,   0,   0,   0,   0,  0,                    0, "Opens one locked door.",      {NO_ACTION,           NO_ACTION, NO_ACTION, NO_ACTION} },

    /* Consumables */
    { "Health Tonic",    ITEM_CONSUMABLE,    0,   0,   0,   0,   0,   0,  ITEM_FLAG_HEAL,      12, "Restores 15 HP. Bitter.",     {NO_ACTION,           NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Adrenaline Shot", ITEM_CONSUMABLE,    0,   0,   0,   0,   0,   0,  ITEM_FLAG_BUFF_ATTACK,18, "Medic kit contraband.",      {NO_ACTION,           NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Smoke Pellet",    ITEM_CONSUMABLE,    0,   0,   0,   0,   0,   0,  0,                   10, "Escape one fight.",           {NO_ACTION,           NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Night Salve",     ITEM_CONSUMABLE,    0,   0,   0,   0,   0,   0,  ITEM_FLAG_HEAL,      14, "Heals. Stops bleeding.",      {NO_ACTION,           NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Focused Draught", ITEM_CONSUMABLE,    0,   0,   0,   0,   0,   0,  0,                   16, "Sharpens the mind.",          {NO_ACTION,           NO_ACTION, NO_ACTION, NO_ACTION} },

    /* Outfits — role / disguise */
    { "Formal Coat",     ITEM_OUTFIT,        0,  -1,   1,   0,   0,   0,  0,                   50, "Noble meeting attire.",       {ACTION_SILVER_TONGUE,NO_ACTION, NO_ACTION, NO_ACTION} },
    { "Vagrants Rags",   ITEM_OUTFIT,        0,   0,  -1,   2,   0,   0,  0,                    2, "Fits in the lower city.",     {ACTION_BLEND_IN,     NO_ACTION, NO_ACTION, NO_ACTION} },
};

int main(void) {
    typedef char check_size[(sizeof(ItemDef) == 64) ? 1 : -1];
    (void)sizeof(check_size);

    FILE *f = fopen("assets/data/items.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot write assets/data/items.dat\n"); return 1; }

    int n = (int)(sizeof(items) / sizeof(items[0]));
    fwrite(&(uint8_t){(uint8_t)n}, 1, 1, f);
    fwrite(items, sizeof(ItemDef), n, f);
    fclose(f);

    printf("Written assets/data/items.dat  (%d items, %d bytes)\n",
        n, 1 + n * (int)sizeof(ItemDef));
    return 0;
}
