#pragma once

/* -----------------------------------------------------------------------
 * Action detail overlay (H during an encounter) — reads the live preview
 * from encPreviewCurrent() and spells out where the selected action can
 * take the encounter and what else it does. Purely a view: it never
 * touches encounter state.
 * ----------------------------------------------------------------------- */

void actionHelpToggle(void);
void actionHelpClose(void);
int  actionHelpIsOpen(void);
void renderActionHelp(void); /* no-op when closed; draw last, it overlays */
