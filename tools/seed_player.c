/* tools/seed_player.c — writes assets/player.dat
 * Run once: make seed_player
 * Edit starting stats here and re-run to regenerate. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define SKILL_MAX   16
#define EQUIP_SLOTS  8
#define ITEM_UNEQUIPPED 0xFF

typedef struct {
    uint16_t maxHp;
    uint8_t  attack;
    uint8_t  defense;
    uint8_t  equipped[EQUIP_SLOTS];
    uint8_t  skills[SKILL_MAX];
    uint8_t  level;
    uint16_t xp;
    uint8_t  skillPoints;
    uint16_t hp;
    uint8_t  intelligence;
    uint8_t  perception;
    uint8_t  stamina;
    uint16_t gold;
} PlayerData;

int main(void) {
    PlayerData p = {0};
    p.maxHp       = 20;
    p.attack      =  5;
    p.defense     =  5;
    p.intelligence=  3;
    p.perception  =  3;
    p.stamina     =  3;
    p.level       =  1;
    p.xp          =  0;
    p.skillPoints =  0;
    p.hp          = p.maxHp;
    p.gold        =  0;
    memset(p.equipped, ITEM_UNEQUIPPED, EQUIP_SLOTS);
    memset(p.skills,   0,               SKILL_MAX);

    FILE *f = fopen("assets/data/player.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot write assets/player.dat\n"); return 1; }
    fwrite(&p, sizeof(PlayerData), 1, f);
    fclose(f);

    printf("Written assets/player.dat  (%d bytes)\n", (int)sizeof(PlayerData));
    return 0;
}
