#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "skills.h"
#include "actions.h"
#include "game.h"
#include "player.h"
#include "gfx.h"

static int selectedSkill = 0;

static const uint8_t skillActions[SKILL_MAX][4] = {
    /* SKILL_BLADES    */ { ACTION_STRONG, ACTION_DISARM, ACTION_STUN, ACTION_EXECUTE },
    /* SKILL_SNEAK     */ { ACTION_BACKSTAB, ACTION_HIDE, 0xFF, 0xFF },
    /* SKILL_MAGIC     */ { 0xFF, 0xFF, 0xFF, 0xFF },
    /* SKILL_DIPLOMACY */ { ACTION_CALM, 0xFF, 0xFF, 0xFF },
    /* SKILL_SURVIVAL  */ { ACTION_HEAL, 0xFF, 0xFF, 0xFF },
    /* SKILL_ARCHERY   */ { 0xFF, 0xFF, 0xFF, 0xFF },
};

void skillGetActions(int skill, uint8_t out[4]) {
    if (skill < 0 || skill >= SKILL_MAX) { memset(out, 0xFF, 4); return; }
    memcpy(out, skillActions[skill], 4);
}

const char *skillName(int skill) {
    switch (skill) {
        case SKILL_BLADES:    return "Nycthemeral";
        case SKILL_SNEAK:     return "Shadowalk";
        case SKILL_MAGIC:     return "Enchantment";
        case SKILL_DIPLOMACY: return "The Voice";
        case SKILL_SURVIVAL:  return "Sanguinomancy";
        case SKILL_ARCHERY:   return "Valentine Blades";
        default:              return "???";
    }
}

const char *skillDesc(int skill) {
    switch (skill) {
        case SKILL_BLADES:
            return "Mastery of edged weapons. Unlocks Strong Attack (lv2),\n"
                   "Disarm (lv3), Stun (lv2) and Execute (lv4) in combat.";
        case SKILL_SNEAK:
            return "Moving unseen. Unlocks Backstab on the opening turn (lv2)\n"
                   "and Hide to avoid counter-attacks (lv3).";
        case SKILL_MAGIC:
            return "Control over arcane forces. Unlocks spells\n"
                   "and magical abilities as your knowledge grows.";
        case SKILL_DIPLOMACY:
            return "The art of persuasion. Unlocks Calm in combat (lv2)\n"
                   "and new dialog paths with nobles and merchants.";
        case SKILL_SURVIVAL:
            return "Endurance in the wild. Unlocks Heal in combat when\n"
                   "wounded (lv1) and improves its potency at higher levels.";
        case SKILL_ARCHERY:
            return "Precision with ranged weapons. Unlocks ranged\n"
                   "combat actions and ambush opportunities.";
        default:
            return "";
    }
}

/* Splits desc at '\n' and draws two lines at (x, y) with given color and scale. */
static void drawDescLines(int x, int y, const char *desc, uint32_t color, int scale) {
    const char *nl = strchr(desc, '\n');
    if (!nl) {
        drawText(x, y, desc, color, scale);
        return;
    }
    char line[64];
    int len = (int)(nl - desc);
    if (len >= 64) len = 63;
    memcpy(line, desc, len);
    line[len] = '\0';
    drawText(x, y,           line,  color, scale);
    drawText(x, y + 8*scale, nl+1,  color, scale);
}

void handleSkillsInput(int key) {
    switch (key) {
        case VK_UP:
            selectedSkill--;
            if (selectedSkill < 0) selectedSkill = SKILL_COUNT - 1;
            break;
        case VK_DOWN:
            selectedSkill = (selectedSkill + 1) % SKILL_COUNT;
            break;
        case VK_RETURN:
            if (player.skillPoints > 0 && player.skills[selectedSkill] < 10) {
                player.skills[selectedSkill]++;
                player.skillPoints--;
            }
            break;
        case VK_BACK:
            if (player.skills[selectedSkill] > 0) {
                player.skills[selectedSkill]--;
                player.skillPoints++;
            }
            break;
        case VK_ESCAPE:
        case 'P':
            state = STATE_WORLD;
            break;
    }
}

void renderSkills(void) {
    const int lineH   = 22;
    const int listX   = 60;
    const int listY   = 75;
    const int descBoxY = gfxHeight - 120;

    fillRect(40, 40, gfxWidth - 80, gfxHeight - 80, rgb(10, 30, 10));

    char buf[32];
    drawText(listX, 50, "SKILLS", rgb(180, 220, 180), 2);
    snprintf(buf, sizeof(buf), " Available points: %d", player.skillPoints);
    uint32_t ptColor = player.skillPoints > 0 ? rgb(255, 220, 80) : rgb(80, 100, 80);
    drawText(listX + 100, 50, buf, ptColor, 2);

    for (int i = 0; i < SKILL_COUNT; i++) {
        int sel = (i == selectedSkill);
        uint8_t val = player.skills[i];

        uint32_t color;
        if (sel)       color = rgb(255, 255, 100);
        else if (val)  color = rgb(160, 220, 160);
        else           color = rgb(80,  100,  80);

        snprintf(buf, sizeof(buf), "%s%-12s %2d",
            sel ? "> " : "  ", skillName(i), val);
        drawText(listX, listY + i * lineH, buf, color, 2);
    }

    /* Description box */
    fillRect(40, descBoxY - 4, gfxWidth - 80, 2, rgb(30, 60, 30));
    drawDescLines(listX, descBoxY + 4,
                  skillDesc(selectedSkill),
                  rgb(140, 190, 140), 1);

    /* Actions granted by this skill */
    {
        uint8_t acts[4];
        skillGetActions(selectedSkill, acts);
        int ax = listX + 320;
        int ay = descBoxY + 4;
        int hasAny = 0;
        for (int j = 0; j < 4; j++) {
            if (acts[j] == 0xFF) continue;
            const ActionDef *adef = getActionDef(acts[j]);
            if (!adef) continue;
            if (!hasAny) { drawText(ax, ay, "Grants:", rgb(200, 180, 80), 1); ay += 12; hasAny = 1; }
            drawText(ax, ay, adef->name, rgb(220, 200, 100), 1);
            ay += 12;
        }
    }

    drawText(listX, gfxHeight - 50, "P / ESC  close      Enter=spend  Bksp=refund", rgb(50, 70, 50), 1);
}
