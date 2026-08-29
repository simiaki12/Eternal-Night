#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "actionhelp.h"
#include "encounter.h"
#include "actions.h"
#include "enc_graph.h"
#include "env_encounter.h"
#include "statuses.h"
#include "effects.h"
#include "gfx.h"

static int g_open = 0;

void actionHelpToggle(void) { g_open = !g_open; }
void actionHelpClose(void)  { g_open = 0; }
int  actionHelpIsOpen(void) { return g_open; }

/* Odds are shown as plusses rather than percentages — enough to compare two
   actions at a glance without turning the fight into a spreadsheet. */
static const char *band(int pct) {
    if (pct >= 99) return "+++++!";
    if (pct >= 75) return "++++";
    if (pct >= 50) return "+++";
    if (pct >= 25) return "++";
    if (pct >= 10) return "+";
    return "~";
}

static uint32_t bandColor(int pct) {
    if (pct >= 99) return rgb(255, 235, 120);
    if (pct >= 75) return rgb(120, 220, 110);
    if (pct >= 50) return rgb(160, 205, 110);
    if (pct >= 25) return rgb(200, 190, 100);
    if (pct >= 10) return rgb(190, 150, 80);
    return rgb(150, 110, 80);
}

/* What EFX_METER moves, in the active encounter's own words. */
static const char *meterNoun(void) {
    switch (encounter.encounterType) {
        case ENCOUNTER_SOCIAL:        return "Willingness";
        case ENCOUNTER_INVESTIGATION: return "Case";
        case ENCOUNTER_HUNT:          return "Panic";
        default:                      return "Meter";
    }
}

/* Render one Effect as a short player-facing line. Returns 0 if the effect
   is inert and should not take up a row. */
static int effectLabel(const Effect *e, char *buf, int cap) {
    if (!e || e->type == EFX_NONE || e->chance == 0) return 0;

    const StatusDef *sd;
    char body[44];

    switch (e->type) {
        case EFX_PROGRESS:
            snprintf(body, sizeof body, "Banks +%d progress", e->value);
            break;
        case EFX_PROGRESS_NEXT:
            snprintf(body, sizeof body, "Next action x%d.%d",
                     e->value / 10, e->value % 10);
            break;
        case EFX_HEAL_HP:
            snprintf(body, sizeof body, "Restores %d HP", e->value);
            break;
        case EFX_DAMAGE_HP:
            snprintf(body, sizeof body, "Costs %d HP", e->value);
            break;
        case EFX_EXTRA_ACTION:
            snprintf(body, sizeof body, "Act again this turn");
            break;
        case EFX_APPLY_STATUS:
            sd = statusGetDef(e->value);
            snprintf(body, sizeof body, "%s %.12s",
                     (sd && (sd->flags & STATUS_NEGATIVE)) ? "Inflicts" : "Grants",
                     sd ? sd->name : "?");
            break;
        case EFX_CLEAR_STATUS:
            if (e->value == 0xFF) {
                snprintf(body, sizeof body, "Cures all ailments");
            } else {
                sd = statusGetDef(e->value);
                snprintf(body, sizeof body, "Cures %.12s", sd ? sd->name : "?");
            }
            break;
        case EFX_METER:
            snprintf(body, sizeof body, "%s %+d", meterNoun(), (int)(int8_t)e->value);
            break;
        case EFX_KILL:
            if (e->value == 0xFF) snprintf(body, sizeof body, "Wipes the group");
            else                  snprintf(body, sizeof body, "Kills %d", e->value);
            break;
        default:
            return 0;
    }

    if (e->chance >= 100) snprintf(buf, cap, "%s", body);
    else                  snprintf(buf, cap, "%d%%: %s", e->chance, body);
    return 1;
}

/* Name of a state in whichever graph the encounter is running. typeIdx -1
   means environmental, whose states carry a description instead of a name. */
static const char *stateName(int typeIdx, uint8_t stateId) {
    if (stateId == 0xFF) return "Resolves it";
    if (typeIdx >= 0) {
        const StateDef *st = encGraphState(typeIdx, stateId);
        return st ? st->name : "?";
    }
    const EnvEncounterDef *def = envEncGetDef((uint8_t)encounter.envEncId);
    if (def && stateId < def->stateCount)
        return def->states[stateId].description;
    return "?";
}

/* ----------------------------------------------------------------------- */

#define HL_MAX 24

typedef struct {
    char     text[44];
    uint32_t col;
    uint8_t  scale;
    uint8_t  indent;
    uint8_t  gap;   /* extra pixels after the row */
    uint8_t  rule;  /* draw a divider instead of text */
} HelpLine;

static int pushLine(HelpLine *l, int n, const char *text, uint32_t col,
                    int scale, int indent, int gap) {
    if (n >= HL_MAX) return n;
    snprintf(l[n].text, sizeof l[n].text, "%s", text);
    l[n].col    = col;
    l[n].scale  = (uint8_t)scale;
    l[n].indent = (uint8_t)indent;
    l[n].gap    = (uint8_t)gap;
    l[n].rule   = 0;
    return n + 1;
}

static int pushRule(HelpLine *l, int n) {
    if (n >= HL_MAX) return n;
    l[n].text[0] = '\0';
    l[n].col     = rgb(48, 48, 70);
    l[n].scale   = 1;
    l[n].indent  = 0;
    l[n].gap     = 4;
    l[n].rule    = 1;
    return n + 1;
}

void renderActionHelp(void) {
    if (!g_open) return;
    if (encounter.selectedIndex < 0 ||
        encounter.selectedIndex >= encounter.actionCount) return;

    uint8_t          aid  = (uint8_t)encounter.actions[encounter.selectedIndex].type;
    const ActionDef *adef = getActionDef(aid);
    if (!adef) return;

    EdgePreview ev[ENC_PREVIEW_MAX];
    int     typeIdx = -1;
    uint8_t cur     = 0;
    int     n       = encPreviewCurrent(aid, ev, &typeIdx, &cur);

    HelpLine L[HL_MAX];
    int      nl = 0;
    char     buf[44];

    /* --- Heading ---------------------------------------------------- */
    nl = pushLine(L, nl, adef->name, rgb(255, 240, 120), 2, 0, 2);
    if (adef->desc[0])
        nl = pushLine(L, nl, adef->desc, rgb(140, 145, 180), 1, 0, 2);
    nl = pushRule(L, nl);

    /* --- Where you are ---------------------------------------------- */
    if (encounter.encounterType == ENCOUNTER_COMBAT &&
        encounter.targetIndex >= 0 && encounter.targetIndex < encounter.enemyCount) {
        snprintf(buf, sizeof buf, "%.14s is %.16s",
                 encounter.enemies[encounter.targetIndex].name,
                 stateName(typeIdx, cur));
    } else {
        snprintf(buf, sizeof buf, "Now: %.20s", stateName(typeIdx, cur));
    }
    nl = pushLine(L, nl, buf, rgb(130, 150, 200), 1, 0, 4);

    /* --- Where it can lead ------------------------------------------ */
    if (n == 0) {
        nl = pushLine(L, nl, "Moves nothing here.", rgb(120, 110, 130), 1, 0, 2);
    }
    for (int i = 0; i < n; i++) {
        const char *dest = stateName(typeIdx, ev[i].to);
        switch (ev[i].verdict) {
            case PV_LIVE:
                snprintf(buf, sizeof buf, "-> %-14.14s %s", dest, band(ev[i].pot));
                nl = pushLine(L, nl, buf, bandColor(ev[i].pot), 1, 0, 1);
                if (ev[i].banked > 0) {
                    snprintf(buf, sizeof buf, "   already %s", band(ev[i].banked));
                    nl = pushLine(L, nl, buf, rgb(95, 95, 120), 1, 0, 2);
                }
                break;
            case PV_SHADOWED:
                snprintf(buf, sizeof buf, "-> %-14.14s (not taken)", dest);
                nl = pushLine(L, nl, buf, rgb(95, 95, 120), 1, 0, 2);
                break;
            case PV_BLOCKED:
                snprintf(buf, sizeof buf, "-> %-14.14s can't go there", dest);
                nl = pushLine(L, nl, buf, rgb(150, 80, 80), 1, 0, 2);
                break;
            default: /* PV_NO_ROUTE */
                snprintf(buf, sizeof buf, "-> %-14.14s not from here", dest);
                nl = pushLine(L, nl, buf, rgb(110, 90, 90), 1, 0, 2);
                break;
        }
    }

    /* --- What else it does ------------------------------------------ */
    char fx[44];
    int  hasPlay = effectLabel(&adef->onPlay,   fx, sizeof fx);
    char wf[44];
    int  hasWhiff = effectLabel(&adef->fallback, wf, sizeof wf);

    if (hasPlay || hasWhiff) {
        nl = pushRule(L, nl);
        if (hasPlay)
            nl = pushLine(L, nl, fx, rgb(150, 200, 230), 1, 0, 2);
        if (hasWhiff) {
            snprintf(buf, sizeof buf, "On a whiff: %.28s", wf);
            nl = pushLine(L, nl, buf, rgb(140, 120, 150), 1, 0, 2);
        }
    }

    nl = pushRule(L, nl);
    nl = pushLine(L, nl, "H / ESC: close", rgb(70, 70, 95), 1, 0, 0);

    /* --- Measure, then draw ----------------------------------------- */
    const int PW  = 360;
    const int PAD = 14;

    int inner = 0;
    for (int i = 0; i < nl; i++)
        inner += (L[i].rule ? 1 : 8 * L[i].scale) + L[i].gap;

    int PH = inner + PAD * 2;
    int PX = gfxWidth  / 2 - PW / 2;
    int PY = gfxHeight / 2 - PH / 2;
    if (PY < 8) PY = 8;

    fillRect(PX,          PY,          PW, PH, rgb(10, 10, 22));
    fillRect(PX,          PY,          PW,  1, rgb(120, 110, 190));
    fillRect(PX,          PY + PH - 1, PW,  1, rgb(120, 110, 190));
    fillRect(PX,          PY,           1, PH, rgb(120, 110, 190));
    fillRect(PX + PW - 1, PY,           1, PH, rgb(120, 110, 190));

    int y = PY + PAD;
    for (int i = 0; i < nl; i++) {
        if (L[i].rule) {
            fillRect(PX + 8, y, PW - 16, 1, L[i].col);
            y += 1 + L[i].gap;
        } else {
            drawText(PX + PAD + L[i].indent, y, L[i].text, L[i].col, L[i].scale);
            y += 8 * L[i].scale + L[i].gap;
        }
    }
}
