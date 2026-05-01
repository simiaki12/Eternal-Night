#include "player.h"
#include "items.h"
#include "game.h"
#include <stdint.h>
#include <string.h>

PlayerData player;

void playerInit(void) {
    memset(&player, 0, sizeof(player));
    memset(player.equipped, ITEM_UNEQUIPPED, EQUIP_SLOTS);
    player.maxHp       = 20;
    player.attack      =  5;
    player.defense     =  5;
    player.intelligence=  3;
    player.perception  =  3;
    player.stamina     =  3;
    player.level       =  1;
    player.hp          = player.maxHp;
}

int xpToNext(void) {
    return 20 + player.level * 10;
}

int awardXp(int amount) {
    int xp = (int)player.xp + amount;
    int gained = 0;
    while (xp >= xpToNext()) {
        xp -= xpToNext();
        player.level++;
        player.maxHp += 5;
        if (player.attack < 255) player.attack  += 1;
        if (player.defense< 255) player.defense += 1;
        if (player.skillPoints < 255) player.skillPoints++;
        player.hp = player.maxHp;
        gained++;
    }
    player.xp = (uint16_t)xp;
    return gained;
}

void enterDeath(void) {
    player.xp = 0;        /* lose XP progress within current level — level is kept */
    player.hp  = player.maxHp; /* healed by the town healer */
    state     = STATE_DEATH;
}
