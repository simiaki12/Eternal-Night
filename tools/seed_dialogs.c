/* tools/seed_dialogs.c — writes the initial assets/data/dialog.dat
 * Run once: make seed_dialogs
 * After that, use the dialog editor to modify content. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static void wByte(FILE *f, uint8_t b) { fwrite(&b, 1, 1, f); }
static void wStr(FILE *f, const char *s) {
    uint8_t len = (uint8_t)strlen(s);
    wByte(f, len);
    fwrite(s, 1, len, f);
}

#define CLOSE  0xFF  /* nextNode = -1 (close dialog) */
#define NO_DOM 0xFF  /* no domain requirement */

/* Domain constants — keep in sync with src/gameplay/domains.h */
#define DOM_COMBAT   0
#define DOM_TRICKERY 1
#define DOM_BLOOD    2
#define DOM_CHARM    3

/* Effect types */
#define FX_NONE 0
#define FX_SE   1  /* social encounter; arg = se_idx */

static void wOpt(FILE *f, uint8_t dom, uint8_t lvl, uint8_t next, const char *text) {
    wByte(f, dom); wByte(f, lvl); wByte(f, next);
    wByte(f, FX_NONE); wByte(f, 0);
    wStr(f, text);
}

static void wOptSE(FILE *f, uint8_t dom, uint8_t lvl, uint8_t next, uint8_t se_idx, const char *text) {
    wByte(f, dom); wByte(f, lvl); wByte(f, next);
    wByte(f, FX_SE); wByte(f, se_idx);
    wStr(f, text);
}

int main(void) {
    FILE *f = fopen("assets/data/dialog.dat", "wb");
    if (!f) { fprintf(stderr, "Cannot write assets/data/dialog.dat\n"); return 1; }

    wByte(f, 2); /* tree count */

    /* ---- Tree 0: Innkeeper ---- */
    wStr(f, "Innkeeper");
    wByte(f, 5); /* node count */

    /* Node 0 */
    wStr(f, "Welcome, traveler. What brings you here?");
    wByte(f, 3); /* options */
    wOpt(f, NO_DOM, 0, 1,     "Just passing through.");
    wOpt(f, NO_DOM, 0, 2,     "Tell me about this place.");
    wOpt(f, DOM_CHARM, 2, 3,  "I seek an audience with the elder.");

    /* Node 1 */
    wStr(f, "Safe travels then. The nights are dangerous out there.");
    wByte(f, 1);
    wOpt(f, NO_DOM, 0, CLOSE, "Farewell.");

    /* Node 2 */
    wStr(f, "Darkwood has seen better days. Since the sun vanished, the villagers live in fear.");
    wByte(f, 2);
    wOpt(f, NO_DOM, 0, CLOSE, "I'll be careful.");
    wOpt(f, NO_DOM, 0, 4,     "Who leads this village?");

    /* Node 3 */
    wStr(f, "A charmer! The elder's house is the large one by the well.");
    wByte(f, 1);
    wOpt(f, NO_DOM, 0, CLOSE, "My thanks.");

    /* Node 4 */
    wStr(f, "Elder Maren. A wise woman, though the bandit troubles weigh heavy on her.");
    wByte(f, 1);
    wOpt(f, NO_DOM, 0, CLOSE, "I'll pay her a visit.");

    /* ---- Tree 1: Village Elder ---- */
    wStr(f, "Elder Maren");
    wByte(f, 2); /* node count */

    /* Node 0 */
    wStr(f, "These are dark times. Bandits raid us nightly since the sun left the sky.");
    wByte(f, 3);
    wOpt(f, NO_DOM, 0,    1,     "Tell me about these bandits.");
    wOpt(f, NO_DOM, 0,    CLOSE, "I have no time for this.");
    wOpt(f, DOM_CHARM, 1, CLOSE, "We should be allies, not strangers.");

    /* Node 1: leads to social encounter (se_idx=0) or a direct dismissal */
    wStr(f, "Their leader, Crag, camps at the old mill. Drive him off, or convince him to leave.");
    wByte(f, 3);
    wOptSE(f, DOM_CHARM, 2, CLOSE, 0, "I'll talk him down. (Negotiate)");
    wOpt(f, NO_DOM, 0, CLOSE, "I'll deal with him directly.");
    wOpt(f, NO_DOM, 0, CLOSE, "I'll think about it.");

    fclose(f);
    printf("Written assets/data/dialog.dat\n");
    return 0;
}
