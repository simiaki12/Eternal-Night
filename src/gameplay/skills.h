#pragma once
#include <stdint.h>
#include "actions.h"

/* Named skill indices — stored in PlayerData.skills[].
 * Add new skills here (up to SKILL_MAX) without breaking existing save data.
 * Also add a description in skillDesc() in skills.c. */
#define SKILL_BLADES     0
#define SKILL_SNEAK      1
#define SKILL_MAGIC      2
#define SKILL_DIPLOMACY  3
#define SKILL_SURVIVAL   4
#define SKILL_ARCHERY    5
#define SKILL_COUNT      6   /* number of currently defined skills */
#define SKILL_MAX       16   /* fixed array size in PlayerData — expansion headroom */

const char *skillName(int skill);
const char *skillDesc(int skill);
void        skillGetActions(int skill, uint8_t out[4]);
void        handleSkillsInput(int key);
void        renderSkills(void);
