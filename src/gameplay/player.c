#include "player.h"
#include "items.h"
#include "game.h"
#include <stdint.h>
#include <string.h>

PlayerData player;

void playerInit(void) {
    memset(&player, 0, sizeof(player));
    memset(player.equipped, ITEM_UNEQUIPPED, EQUIP_SLOTS);
    player.maxHp        = 100;
    player.attack       =  5;
    player.defense      =  5;
    player.stamina      =  3;
    player.intelligence =  3;
    player.perception   =  3;
    player.charisma     =  3;
    player.agility      =  3;
    player.hp           = player.maxHp;
    player.focusedDomain = DOMAIN_NONE;
}

void enterDeath(void) {
    player.hp = player.maxHp;
    state     = STATE_DEATH;
}
