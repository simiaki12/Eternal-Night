#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "shop.h"
#include "gfx.h"
#include "game.h"
#include "items.h"
#include "player.h"

ShopDef shopDefs[SHOP_DEF_MAX];
int     shopDefCount = 0;

static const ShopDef builtinShop0 = {
    "Black Market",
    {3}, /* Flaming Sword */
    1,
    {0}
};

int loadShops(PakData data) {
    if (!data.data || data.size < 1) return 0;
    uint8_t n = ((const uint8_t *)data.data)[0];
    if (n > SHOP_DEF_MAX) n = SHOP_DEF_MAX;
    uint32_t expected = 1 + (uint32_t)n * sizeof(ShopDef);
    if (data.size < expected) return 0;
    memcpy(shopDefs, (const uint8_t *)data.data + 1, (size_t)n * sizeof(ShopDef));
    shopDefCount = n;
    return n;
}

static const ShopDef *g_shop   = NULL;
static int            g_sel    = 0;
static GameState      g_returnTo;
static char           g_msg[48] = {0};
static DWORD          g_msgEnd  = 0;
#define MSG_MS 2000

void enterShop(uint8_t shopId, GameState returnTo) {
    g_returnTo = returnTo;
    g_sel      = 0;
    g_msg[0]   = '\0';
    if (shopDefCount > 0 && shopId < (uint8_t)shopDefCount)
        g_shop = &shopDefs[shopId];
    else
        g_shop = &builtinShop0;
    state = STATE_SHOP;
}

void handleShopInput(int key) {
    int count = g_shop ? (int)g_shop->count : 0;
    if (count == 0 && key != VK_ESCAPE) return;
    switch (key) {
        case VK_UP:
            g_sel = (g_sel + count - 1) % count;
            g_msg[0] = '\0';
            break;
        case VK_DOWN:
            g_sel = (g_sel + 1) % count;
            g_msg[0] = '\0';
            break;
        case VK_RETURN: {
            uint8_t id       = g_shop->stock[g_sel];
            const ItemDef *d = itemGetDef(id);
            if (!d) break;
            uint16_t price = d->price;
            if (player.gold < price) {
                snprintf(g_msg, sizeof(g_msg), "Not enough Solmarks! (need %d) ~", price);
            } else if (inventory.count >= 16) {
                snprintf(g_msg, sizeof(g_msg), "Inventory is full!");
            } else {
                player.gold -= price;
                addItem(id);
                snprintf(g_msg, sizeof(g_msg), "Bought %s!", d->name);
            }
            g_msgEnd = GetTickCount() + MSG_MS;
            break;
        }
        case VK_ESCAPE:
            state = g_returnTo;
            break;
    }
}

void renderShop(void) {
    const int x     = 60;
    const int lineH = 22;
    int y = 55;
    char buf[64];

    fillRect(40, 40, gfxWidth - 80, gfxHeight - 80, rgb(10, 20, 50));

    const char *title = (g_shop && g_shop->name[0]) ? g_shop->name : "SHOP";
    drawText(x, y, title, rgb(220, 220, 255), 2);
    y += lineH + 4;

    int count = g_shop ? (int)g_shop->count : 0;

    if (count == 0) {
        drawText(x, y, "  (nothing for sale)", rgb(120, 120, 120), 2);
        y += lineH;
    } else {
        for (int i = 0; i < count; i++) {
            uint8_t id       = g_shop->stock[i];
            const ItemDef *d = itemGetDef(id);
            int     sel      = (i == g_sel);
            uint32_t color   = sel ? rgb(255, 255, 100) : rgb(180, 180, 180);

            if (d)
                snprintf(buf, sizeof(buf), "%s%-14s %d ~",
                         sel ? "> " : "  ", d->name, d->price);
            else
                snprintf(buf, sizeof(buf), "%s???", sel ? "> " : "  ");

            drawText(x, y, buf, color, 2);
            y += lineH;
        }
    }

    /* Divider */
    fillRect(x, y + 6, gfxWidth - 120, 1, rgb(60, 60, 100));
    y += 16;

    /* Selected item details */
    if (count > 0) {
        uint8_t id       = g_shop->stock[g_sel];
        const ItemDef *d = itemGetDef(id);
        if (d) {
            drawText(x, y, itemDesc(id), rgb(160, 200, 255), 2);
            y += lineH + 2;

            #define STAT_ROW(label, stat, col) { \
                int cur = getStat(stat), pre = getStatPreview(stat, id); \
                if (pre != cur) snprintf(buf, sizeof(buf), label ": %d -> %d", cur, pre); \
                else            snprintf(buf, sizeof(buf), label ": %d", cur); \
                drawText(x, y, buf, col, 2); y += lineH; }

            if (d->attackBonus      || d->type == ITEM_WEAPON)  { STAT_ROW("ATK", STAT_ATK, rgb(220, 100, 100)) }
            if (d->defenseBonus     || d->type == ITEM_OUTFIT)  { STAT_ROW("DEF", STAT_DEF, rgb(100, 160, 220)) }
            if (d->hpBonus)                                      { STAT_ROW("MHP", STAT_MHP, rgb( 80, 200,  80)) }
            if (d->intelligenceBonus)                            { STAT_ROW("INT", STAT_INT, rgb(180, 100, 220)) }
            if (d->perceptionBonus)                              { STAT_ROW("PER", STAT_PER, rgb(220, 180, 100)) }
            if (d->staminaBonus)                                 { STAT_ROW("STA", STAT_STA, rgb(100, 220, 180)) }
            #undef STAT_ROW
        }
    }

    /* Feedback message */
    if (g_msg[0] && GetTickCount() < g_msgEnd)
        drawText(x, gfxHeight - 100, g_msg, rgb(255, 180, 60), 2);

    /* Gold */
    snprintf(buf, sizeof(buf), "Solmarks: %d ~", player.gold);
    drawText(x, gfxHeight - 80, buf, rgb(255, 215, 0), 2);

    drawText(x, gfxHeight - 62, "Enter=buy  ESC=close", rgb(80, 80, 80), 1);
}
