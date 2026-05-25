#include "items.h"
#include "actions.h"
#include "player.h"
#include "quests.h"
#include <stdio.h>
#include <string.h>

ItemDef   itemDefs[64];
int       itemDefCount = 0;

Inventory inventory = {
    {0, 1, 2},
    3,
    0
};

static const ItemDef builtinDefs[] = {
    { "Iron Sword",    ITEM_WEAPON,      3, 0, 0, 0, 0, 0, 0,              15, "A reliable iron blade.",  {ACTION_DEFEND, 0xFF, 0xFF, 0xFF}, {0} },
    { "Leather Armor", ITEM_OUTFIT,      0, 2, 0, 0, 0, 0, 0,              12, "Light but protective.",   {0xFF, 0xFF, 0xFF, 0xFF},          {0} },
    { "Health Potion", ITEM_CONSUMABLE,  0, 0, 0, 0, 0, 0, ITEM_FLAG_HEAL,  8, "Restores 10 HP.",         {0xFF, 0xFF, 0xFF, 0xFF},          {0} },
};
#define BUILTIN_COUNT (int)(sizeof(builtinDefs)/sizeof(builtinDefs[0]))

/* Format: [1 count][N × sizeof(ItemDef)] */
int loadItems(PakData data) {
    if (!data.data || data.size < 1) return 0;
    uint8_t n = ((const uint8_t *)data.data)[0];
    if (n > 64) n = 64;
    uint32_t expected = 1 + (uint32_t)n * sizeof(ItemDef);
    if (data.size < expected) return 0;
    memcpy(itemDefs, (const uint8_t *)data.data + 1, (size_t)n * sizeof(ItemDef));
    itemDefCount = n;
    return n;
}

const ItemDef *itemGetDef(uint8_t id) {
    if (itemDefCount > 0 && id < (uint8_t)itemDefCount) return &itemDefs[id];
    if ((int)id < BUILTIN_COUNT) return &builtinDefs[id];
    return NULL;
}

const char *itemName(uint8_t id) {
    const ItemDef *d = itemGetDef(id);
    return d ? d->name : "???";
}

const char *itemDesc(uint8_t id) {
    const ItemDef *d = itemGetDef(id);
    if (!d || !d->description[0]) return "";
    return d->description;
}

int itemSlot(const ItemDef *d) {
    if (!d) return -1;
    switch (d->type) {
        case ITEM_WEAPON:       return SLOT_WEAPON;
        case ITEM_OUTFIT:       return SLOT_OUTFIT;
        case ITEM_UNDERGARMENT: return SLOT_UNDER;
        case ITEM_ACCESSORY:    return SLOT_ACCESSORY_1; /* dual-slot: see useOrEquipItem */
        case ITEM_RELIC:        return SLOT_RELIC;
        default:                return -1;
    }
}

int isEquipped(uint8_t id) {
    for (int i = 0; i < EQUIP_SLOTS; i++)
        if (player.equipped[i] == id) return 1;
    return 0;
}

int playerHasItem(uint8_t id) {
    if (isEquipped(id)) return 1;
    for (int i = 0; i < inventory.count; i++)
        if (inventory.items[i] == id) return 1;
    return 0;
}

static int bonusOf(const ItemDef *d, StatType stat) {
    if (!d) return 0;
    switch (stat) {
        case STAT_ATK: return d->attackBonus;
        case STAT_DEF: return d->defenseBonus;
        case STAT_MHP: return d->hpBonus;
        case STAT_INT: return d->intelligenceBonus;
        case STAT_PER: return d->perceptionBonus;
        case STAT_STA: return d->staminaBonus;
        default:       return 0;
    }
}

static int baseStat(StatType stat) {
    switch (stat) {
        case STAT_ATK: return player.attack;
        case STAT_DEF: return player.defense;
        case STAT_MHP: return player.maxHp;
        case STAT_INT: return player.intelligence;
        case STAT_PER: return player.perception;
        case STAT_STA: return player.stamina;
        default:       return 0;
    }
}

int getStat(StatType stat) {
    int val = baseStat(stat);
    for (int i = 0; i < EQUIP_SLOTS; i++)
        if (player.equipped[i] != ITEM_UNEQUIPPED)
            val += bonusOf(itemGetDef(player.equipped[i]), stat);
    return val;
}

int getStatPreview(StatType stat, uint8_t itemId) {
    const ItemDef *newItem = itemGetDef(itemId);
    if (!newItem) return getStat(stat);
    if (newItem->type == ITEM_CONSUMABLE) return getStat(stat);
    if (isEquipped(itemId)) return getStat(stat); /* already on — no change */

    /* For accessories, find which slot would be displaced (if any) */
    int replaceSlot;
    if (newItem->type == ITEM_ACCESSORY) {
        if      (player.equipped[SLOT_ACCESSORY_1] == ITEM_UNEQUIPPED) replaceSlot = -1;
        else if (player.equipped[SLOT_ACCESSORY_2] == ITEM_UNEQUIPPED) replaceSlot = -1;
        else                                                             replaceSlot = SLOT_ACCESSORY_2;
    } else {
        replaceSlot = itemSlot(newItem);
    }

    int result = baseStat(stat);
    for (int i = 0; i < EQUIP_SLOTS; i++) {
        if (i == replaceSlot) continue;
        if (player.equipped[i] != ITEM_UNEQUIPPED)
            result += bonusOf(itemGetDef(player.equipped[i]), stat);
    }
    result += bonusOf(newItem, stat);
    return result;
}

int getAttack(void)      { return getStat(STAT_ATK); }
int getDefense(void)     { return getStat(STAT_DEF); }
int getMaxHp(void)       { return getStat(STAT_MHP); }
int getIntelligence(void){ return getStat(STAT_INT); }
int getPerception(void)  { return getStat(STAT_PER); }
int getStamina(void)     { return getStat(STAT_STA); }

int addItem(uint8_t id) {
    if (inventory.count >= 16) return 0;
    inventory.items[inventory.count++] = id;
    questOnItemGained(id);
    return 1;
}

void removeItem(int index) {
    int i;
    for (i = index; i < inventory.count - 1; i++)
        inventory.items[i] = inventory.items[i + 1];
    inventory.count--;
    if (inventory.selected >= (int8_t)inventory.count)
        inventory.selected = inventory.count - 1;
}

static void purgeItemActions(uint8_t itemId) {
    const ItemDef *d = itemGetDef(itemId);
    if (!d) return;
    for (int k = 0; k < 4; k++)
        if (d->actions[k] != 0xFF)
            playerRemoveActionFromQueues(d->actions[k]);
}

void useOrEquipItem(int index) {
    uint8_t id = inventory.items[index];
    const ItemDef *d = itemGetDef(id);
    if (!d) return;

    if (d->type == ITEM_ACCESSORY) {
        /* Dual-slot: unequip from whichever slot holds it, or fill an empty slot, or replace slot 1 */
        if (player.equipped[SLOT_ACCESSORY_1] == id) { purgeItemActions(id); player.equipped[SLOT_ACCESSORY_1] = ITEM_UNEQUIPPED; return; }
        if (player.equipped[SLOT_ACCESSORY_2] == id) { purgeItemActions(id); player.equipped[SLOT_ACCESSORY_2] = ITEM_UNEQUIPPED; return; }
        if (player.equipped[SLOT_ACCESSORY_1] == ITEM_UNEQUIPPED) { player.equipped[SLOT_ACCESSORY_1] = id; return; }
        if (player.equipped[SLOT_ACCESSORY_2] == ITEM_UNEQUIPPED) { player.equipped[SLOT_ACCESSORY_2] = id; return; }
        /* Both slots full — old slot 2 is pushed out */
        purgeItemActions(player.equipped[SLOT_ACCESSORY_2]);
        player.equipped[SLOT_ACCESSORY_2] = player.equipped[SLOT_ACCESSORY_1];
        player.equipped[SLOT_ACCESSORY_1] = id;
        return;
    }

    int slot = itemSlot(d);
    if (slot >= 0) {
        /* toggle: unequip if already in slot, otherwise equip (replacing whatever was there) */
        uint8_t prev = player.equipped[slot];
        if (prev != ITEM_UNEQUIPPED) purgeItemActions(prev);
        player.equipped[slot] = (prev == id) ? ITEM_UNEQUIPPED : id;
    } else if (d->type == ITEM_CONSUMABLE) {
        if (d->flags & ITEM_FLAG_HEAL) {
            int newHp = (int)player.hp + 10;
            int cap   = getMaxHp();
            player.hp = (uint16_t)(newHp > cap ? cap : newHp);
        }
        removeItem(index);
    }
}
