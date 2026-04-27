/*
 * music_editor — tracker-style ncurses editor for Tiny Hero .mus files
 * Build: gcc -std=c11 -Os -Wno-unused-result tools/music_editor.c -o build/music_editor -lncurses
 *
 * Keybindings (track view):
 *   a-g          enter note (advances cursor); # before = sharp
 *   0-8          change octave of current note
 *   - / r        enter rest (advances cursor)
 *   w h q i      set duration: whole / half / quarter / eighth  (i not e — e is a note)
 *   x            toggle dotted (WN->DH, HN->DH, QN->DQ, EN->DE)
 *   Ins / Del    insert rest / delete note at cursor
 *   arrows       navigate steps and voices
 *   PgUp/PgDn    previous / next section
 *   Tab          cycle voice
 *   Z            arrange mode
 *   P            params mode
 *   S O N T     save / open / new / title  (lowercase ok)
 *   B            BPM  (uppercase only — lowercase b is note B)
 *   Q            quit (uppercase only — lowercase q is quarter note)
 */

#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

/* ── Binary format constants (keep in sync with audio.c) ──────────────────
 *
 * File layout:
 *   [4]   magic "MUS0"
 *   [32]  title (null-padded)
 *   [1]   bpm  (40-255)
 *   [1]   arr_len (1-64)
 *   [64]  arr[]   — section indices in playback order
 *   [1]   sec_count (1-32)
 *   [4]   wave[4]     — 0=tri 1=sqr 2=saw 3=noise
 *   [4]   atk_ms[4]   — attack  1-255 ms
 *   [4]   dcy_10ms[4] — decay   1-255 × 10 ms
 *   [4]   sus_pct[4]  — sustain 0-100 %
 *   [4]   rel_ms[4]   — release 1-255 ms
 *   [4]   gate_pct[4] — gate    0-100 %
 *   for each section:
 *     for each voice (0-3):
 *       [1]   note_count
 *       [2×n] notes: pitch (MIDI, 255=rest), dur (DUR_*)
 */
#define MUS_MAGIC    "MUS0"
#define MAX_SECTIONS  32
#define MAX_NOTES    255
#define MAX_ARR       64
#define NUM_VOICES     4

#define DUR_WN  0   /* whole note       */
#define DUR_HN  1   /* half note        */
#define DUR_QN  2   /* quarter note     */
#define DUR_DH  3   /* dotted half      */
#define DUR_DQ  4   /* dotted quarter   */
#define DUR_EN  5   /* eighth note      */
#define DUR_DE  6   /* dotted eighth    */
#define DUR_COUNT 7

#define WAVE_TRI   0
#define WAVE_SQR   1
#define WAVE_SAW   2
#define WAVE_NOISE 3

#define PITCH_REST 255

/* ── Song data ────────────────────────────────────────────────────────── */

typedef struct { uint8_t pitch; uint8_t dur; } Note;
typedef struct { int count; Note notes[MAX_NOTES]; } Track;
typedef struct { Track tracks[NUM_VOICES]; } Section;

static char    g_title[32];
static uint8_t g_bpm;
static uint8_t g_arr[MAX_ARR];
static int     g_arr_len;
static int     g_sec_count;
static Section g_secs[MAX_SECTIONS];

static uint8_t g_wave[NUM_VOICES];
static uint8_t g_atk_ms[NUM_VOICES];
static uint8_t g_dcy_10ms[NUM_VOICES];
static uint8_t g_sus_pct[NUM_VOICES];
static uint8_t g_rel_ms[NUM_VOICES];
static uint8_t g_gate_pct[NUM_VOICES];

static char g_path[128];
static int  g_dirty;

/* ── Editor UI state ──────────────────────────────────────────────────── */

#define MODE_TRACK   0
#define MODE_ARRANGE 1
#define MODE_PARAMS  2
#define MODE_BROWSE  3

static int g_mode          = MODE_TRACK;
static int g_cur_sec       = 0;
static int g_cur_voice     = 0;
static int g_cur_step      = 0;
static int g_scroll        = 0;
static int g_arr_cur       = 0;
static int g_par_voice     = 0;
static int g_par_field     = 0;
static int g_browse_sel    = 0;
static int g_pending_sharp = 0;

/* ── File browser ─────────────────────────────────────────────────────── */

#define MAX_BROWSE 64
static char g_browse_files[MAX_BROWSE][280];
static int  g_browse_count = 0;

static void scan_music(void) {
    g_browse_count = 0;
    DIR *d = opendir("assets/music");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && g_browse_count < MAX_BROWSE) {
        int len = (int)strlen(e->d_name);
        if (len > 4 && strcmp(e->d_name + len - 4, ".mus") == 0) {
            snprintf(g_browse_files[g_browse_count++], 280,
                     "assets/music/%s", e->d_name);
        }
    }
    closedir(d);
    for (int i = 0; i < g_browse_count - 1; i++)
        for (int j = i + 1; j < g_browse_count; j++)
            if (strcmp(g_browse_files[i], g_browse_files[j]) > 0) {
                char t[280];
                memcpy(t,               g_browse_files[i], 280);
                memcpy(g_browse_files[i], g_browse_files[j], 280);
                memcpy(g_browse_files[j], t,               280);
            }
}

/* ── Reset / load / save ──────────────────────────────────────────────── */

static void reset_song(void) {
    memset(g_secs,  0, sizeof(g_secs));
    memset(g_arr,   0, sizeof(g_arr));
    memset(g_title, 0, sizeof(g_title));
    memcpy(g_title, "Untitled", 8);
    g_sec_count = 1;
    g_arr_len   = 1;
    g_bpm       = 110;
    g_cur_sec   = 0; g_cur_voice = 0;
    g_cur_step  = 0; g_scroll    = 0;
    g_dirty     = 0;

    static const uint8_t dw[4] = { WAVE_TRI, WAVE_SQR, WAVE_TRI, WAVE_NOISE };
    static const uint8_t da[4] = { 15,  80,  1,  1 };
    static const uint8_t dd[4] = { 50, 200, 15,  4 };
    static const uint8_t ds[4] = { 75,  55,  0,  0 };
    static const uint8_t dr[4] = { 50,  60,  1,  1 };
    static const uint8_t dg[4] = { 85,  88,100,100 };
    memcpy(g_wave,     dw, 4); memcpy(g_atk_ms,   da, 4);
    memcpy(g_dcy_10ms, dd, 4); memcpy(g_sus_pct,  ds, 4);
    memcpy(g_rel_ms,   dr, 4); memcpy(g_gate_pct, dg, 4);
}

static int load_mus(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char magic[4];
    if (fread(magic, 1, 4, f) < 4 || memcmp(magic, MUS_MAGIC, 4)) {
        fclose(f); return 0;
    }
    (void)fread(g_title,     1, 32, f);
    (void)fread(&g_bpm,      1,  1, f);
    uint8_t al; (void)fread(&al, 1, 1, f); g_arr_len = al;
    (void)fread(g_arr,       1, 64, f);
    uint8_t sc; (void)fread(&sc, 1, 1, f); g_sec_count = sc;
    (void)fread(g_wave,      1,  4, f); (void)fread(g_atk_ms,   1, 4, f);
    (void)fread(g_dcy_10ms,  1,  4, f); (void)fread(g_sus_pct,  1, 4, f);
    (void)fread(g_rel_ms,    1,  4, f); (void)fread(g_gate_pct, 1, 4, f);
    memset(g_secs, 0, sizeof(g_secs));
    for (int s = 0; s < g_sec_count; s++)
        for (int v = 0; v < NUM_VOICES; v++) {
            uint8_t nc; (void)fread(&nc, 1, 1, f);
            g_secs[s].tracks[v].count = nc;
            for (int n = 0; n < nc; n++) {
                (void)fread(&g_secs[s].tracks[v].notes[n].pitch, 1, 1, f);
                (void)fread(&g_secs[s].tracks[v].notes[n].dur,   1, 1, f);
            }
        }
    fclose(f);
    g_cur_sec = 0; g_cur_voice = 0; g_cur_step = 0; g_scroll = 0;
    g_dirty   = 0;
    return 1;
}

static void save_mus(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(MUS_MAGIC,  1, 4,  f);
    fwrite(g_title,    1, 32, f);
    fwrite(&g_bpm,     1,  1, f);
    uint8_t al = (uint8_t)g_arr_len; fwrite(&al, 1, 1, f);
    fwrite(g_arr,      1, 64, f);
    uint8_t sc = (uint8_t)g_sec_count; fwrite(&sc, 1, 1, f);
    fwrite(g_wave,     1,  4, f); fwrite(g_atk_ms,   1, 4, f);
    fwrite(g_dcy_10ms, 1,  4, f); fwrite(g_sus_pct,  1, 4, f);
    fwrite(g_rel_ms,   1,  4, f); fwrite(g_gate_pct, 1, 4, f);
    for (int s = 0; s < g_sec_count; s++)
        for (int v = 0; v < NUM_VOICES; v++) {
            uint8_t nc = (uint8_t)g_secs[s].tracks[v].count;
            fwrite(&nc, 1, 1, f);
            for (int n = 0; n < nc; n++) {
                fwrite(&g_secs[s].tracks[v].notes[n].pitch, 1, 1, f);
                fwrite(&g_secs[s].tracks[v].notes[n].dur,   1, 1, f);
            }
        }
    fclose(f);
    g_dirty = 0;
}

/* ── Note helpers ─────────────────────────────────────────────────────── */

static const char *dur_names[DUR_COUNT] = { "WN","HN","QN","DH","DQ","EN","DE" };
static const char *wave_names[4]        = { "tri","sqr","saw","nse" };
static const char *voice_names[4]       = { "MELODY","HARMONY","BASS","PERC" };

static const char *semitone_names[12] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

static void pitch_str(uint8_t p, char *out) {
    if (p == PITCH_REST) { strcpy(out, "---"); return; }
    int oct  = (int)(p / 12) - 1;
    int semi = p % 12;
    sprintf(out, "%s%d", semitone_names[semi], oct);
}

/* letter='c'..'g'/'a'/'b' (case-insensitive), sharp=0/1, octave=0..8 → MIDI */
static uint8_t make_pitch(char letter, int sharp, int octave) {
    int semi;
    switch (letter | 32) {
        case 'c': semi = 0; break; case 'd': semi = 2; break;
        case 'e': semi = 4; break; case 'f': semi = 5; break;
        case 'g': semi = 7; break; case 'a': semi = 9; break;
        case 'b': semi = 11; break; default: return PITCH_REST;
    }
    int midi = (octave + 1) * 12 + semi + sharp;
    if (midi < 0 || midi > 127) return PITCH_REST;
    return (uint8_t)midi;
}

/* ── Rendering ────────────────────────────────────────────────────────── */

/* col-pair layout: 1=dim  2=section-hdr  3=saved  4=unsaved  5=cursor */
#define CP_DIM     1
#define CP_HDR     2
#define CP_SAVED   3
#define CP_DIRTY   4

static void draw_header(int cols) {
    attron(A_BOLD);
    mvprintw(0, 0, "MUSIC EDITOR  %-32s  BPM:%-3d  Sec:%d/%d",
             g_path, (int)g_bpm, g_cur_sec, g_sec_count - 1);
    attroff(A_BOLD);
    if (g_dirty) {
        attron(COLOR_PAIR(CP_DIRTY) | A_BOLD);
        mvprintw(0, cols - 9, "[unsaved]");
        attroff(COLOR_PAIR(CP_DIRTY) | A_BOLD);
    } else {
        attron(COLOR_PAIR(CP_SAVED));
        mvprintw(0, cols - 7, "[saved]");
        attroff(COLOR_PAIR(CP_SAVED));
    }
    /* Arrangement summary */
    char arrbuf[80] = "Arr:";
    int  pos = 4;
    for (int i = 0; i < g_arr_len && pos < 72; i++)
        pos += snprintf(arrbuf + pos, sizeof(arrbuf) - (size_t)pos,
                        " %d", (int)g_arr[i]);
    mvprintw(1, 0, "%-79s", arrbuf);
    mvprintw(2, 0, "Title: %-24s  [T]=title  [B]=bpm  [Z]=arrange  [P]=params",
             g_title);
}

static void draw_track(int rows) {
    /* Column layout: step(5) + 4 × (separator(1) + note+dur(13)) */
    attron(A_BOLD);
    mvprintw(3, 0, "Step | %-13s| %-13s| %-13s| %-13s",
             voice_names[0], voice_names[1], voice_names[2], voice_names[3]);
    attroff(A_BOLD);
    mvhline(4, 0, '-', 5); mvprintw(4, 5, "+");
    for (int v = 0; v < NUM_VOICES; v++) mvprintw(4, 6 + v * 14, "%-13s+", "-------------");

    int vis = rows - 9;
    if (vis < 1) vis = 1;

    /* Find display max */
    int max_len = 1;
    for (int v = 0; v < NUM_VOICES; v++)
        if (g_secs[g_cur_sec].tracks[v].count > max_len)
            max_len = g_secs[g_cur_sec].tracks[v].count;

    /* Clamp cursor */
    if (g_cur_step < 0) g_cur_step = 0;
    if (g_cur_step > max_len) g_cur_step = max_len;
    if (g_cur_step < g_scroll)         g_scroll = g_cur_step;
    if (g_cur_step >= g_scroll + vis)  g_scroll = g_cur_step - vis + 1;
    if (g_scroll < 0) g_scroll = 0;

    for (int row = 0; row < vis; row++) {
        int step = g_scroll + row;
        int y    = 5 + row;
        int is_past = (step >= max_len);
        if (is_past) attron(COLOR_PAIR(CP_DIM));
        mvprintw(y, 0, "%4d |", step);
        if (is_past) attroff(COLOR_PAIR(CP_DIM));

        for (int v = 0; v < NUM_VOICES; v++) {
            Track *t = &g_secs[g_cur_sec].tracks[v];
            char   nb[6] = "---";
            char   db[4] = "  ";
            if (step < t->count) {
                pitch_str(t->notes[step].pitch, nb);
                snprintf(db, sizeof(db), "%s",
                         dur_names[t->notes[step].dur % DUR_COUNT]);
            }
            int sel = (step == g_cur_step && v == g_cur_voice);
            int attr = sel ? A_REVERSE : (step >= t->count ? COLOR_PAIR(CP_DIM) : 0);
            attron(attr);
            mvprintw(y, 6 + v * 14, " %-4s %-3s    ", nb, db);
            attroff(attr);
            mvprintw(y, 6 + v * 14 + 13, "|");
        }
    }

    /* Voice info bar */
    attron(A_BOLD);
    mvprintw(rows - 4, 0,
        "Voice:%-7s Wave:%-3s Atk:%3dms Dcy:%4dms Sus:%3d%% Rel:%3dms Gate:%3d%%",
        voice_names[g_cur_voice], wave_names[g_wave[g_cur_voice]],
        (int)g_atk_ms[g_cur_voice], (int)g_dcy_10ms[g_cur_voice] * 10,
        (int)g_sus_pct[g_cur_voice], (int)g_rel_ms[g_cur_voice],
        (int)g_gate_pct[g_cur_voice]);
    attroff(A_BOLD);
    mvprintw(rows - 3, 0,
        "[a-g]=note  [#]=sharp  [0-8]=oct  [-/r]=rest  [w/h/q/i]=dur  [x]=dot");
    mvprintw(rows - 2, 0,
        "[Ins]=insert  [Del]=delete  [Tab]=voice  [PgU/D]=section");
    mvprintw(rows - 1, 0,
        "[S]=save  [O]=open  [N]=new  [T]=title  [B]=BPM  [Q]=quit  (caps)");
}

static void draw_arrange(int rows) {
    attron(A_BOLD | COLOR_PAIR(CP_HDR));
    mvprintw(3, 0, "ARRANGEMENT  (playback order of sections)");
    attroff(A_BOLD | COLOR_PAIR(CP_HDR));
    mvprintw(4, 0, "Sections defined: %d    Arrangement length: %d",
             g_sec_count, g_arr_len);

    int vis    = rows - 10;
    int scroll = 0;
    if (g_arr_cur >= vis) scroll = g_arr_cur - vis + 1;

    for (int i = 0; i < g_arr_len && i < vis; i++) {
        int idx  = scroll + i;
        if (idx >= g_arr_len) break;
        int attr = (idx == g_arr_cur) ? A_REVERSE : 0;
        attron(attr);
        mvprintw(6 + i, 4, "  [%2d]  Section %d  ", idx, (int)g_arr[idx]);
        attroff(attr);
    }

    mvprintw(rows - 4, 0, "[0-9]=set section  [Ins]=add entry  [Del]=remove entry");
    mvprintw(rows - 3, 0, "[+]=add new section (max %d)  [-]=remove last (if unused)",
             MAX_SECTIONS);
    mvprintw(rows - 2, 0, "[Z/Esc]=back to track");
    mvprintw(rows - 1, 0, "[S]=save  [Q]=quit");
}

static void draw_params(int rows) {
    (void)rows;
    attron(A_BOLD | COLOR_PAIR(CP_HDR));
    mvprintw(3, 0, "VOICE PARAMETERS");
    attroff(A_BOLD | COLOR_PAIR(CP_HDR));

    /* Column headers */
    mvprintw(5, 0, "         %-5s %-5s %-9s %-5s %-5s %-6s",
             "Wave", "Atk", "Dcy(x10ms)", "Sus%", "Rel", "Gate%");

    static const char *fnames[6] = { "Wave","Atk","Dcy","Sus","Rel","Gate" };
    (void)fnames;

    for (int v = 0; v < NUM_VOICES; v++) {
        int vals[6] = {
            g_wave[v], g_atk_ms[v], g_dcy_10ms[v],
            g_sus_pct[v], g_rel_ms[v], g_gate_pct[v]
        };
        int row_attr = (v == g_par_voice) ? A_BOLD : 0;
        attron(row_attr);
        mvprintw(6 + v * 2, 0, "%c%-8s",
                 v == g_par_voice ? '>' : ' ', voice_names[v]);
        attroff(row_attr);

        int col_x[6] = { 9, 15, 21, 31, 37, 43 };
        for (int fi = 0; fi < 6; fi++) {
            int is_sel = (v == g_par_voice && fi == g_par_field);
            int attr   = is_sel ? A_REVERSE : 0;
            attron(attr);
            if (fi == 0)
                mvprintw(6 + v * 2, col_x[fi], "%-5s ", wave_names[vals[fi]]);
            else
                mvprintw(6 + v * 2, col_x[fi], "%-5d ", vals[fi]);
            attroff(attr);
        }
    }

    mvprintw(6 + NUM_VOICES * 2 + 1, 0,
        "[arrows]=navigate  [+/-]=change value  [W]=cycle wave type");
    mvprintw(6 + NUM_VOICES * 2 + 2, 0,
        "[P/Esc]=back to track  [S]=save");
}

static void draw_browse(int rows) {
    attron(A_BOLD | COLOR_PAIR(CP_HDR));
    mvprintw(3, 0, "OPEN FILE");
    attroff(A_BOLD | COLOR_PAIR(CP_HDR));
    mvprintw(4, 0, "[arrows]=select  [Enter]=open  [Esc]=cancel");

    int vis    = rows - 7;
    int scroll = 0;
    if (g_browse_sel >= vis) scroll = g_browse_sel - vis + 1;

    for (int i = 0; i < vis; i++) {
        int idx = scroll + i;
        if (idx >= g_browse_count) break;
        int attr = (idx == g_browse_sel) ? A_REVERSE : 0;
        attron(attr);
        mvprintw(6 + i, 2, "  %s  ", g_browse_files[idx]);
        attroff(attr);
    }
    if (g_browse_count == 0)
        mvprintw(6, 4, "(no .mus files in assets/music/)");
}

/* ── Input handlers ───────────────────────────────────────────────────── */

static void track_enter_note(char letter) {
    Track *t   = &g_secs[g_cur_sec].tracks[g_cur_voice];
    int    oct = 4;
    if (g_cur_step < t->count && t->notes[g_cur_step].pitch != PITCH_REST)
        oct = (int)(t->notes[g_cur_step].pitch / 12) - 1;
    uint8_t p = make_pitch(letter, g_pending_sharp, oct);
    g_pending_sharp = 0;
    if (p == PITCH_REST) return;
    /* Extend track with rests if needed */
    while (t->count < g_cur_step && t->count < MAX_NOTES) {
        t->notes[t->count].pitch = PITCH_REST;
        t->notes[t->count].dur   = DUR_QN;
        t->count++;
    }
    if (g_cur_step < MAX_NOTES) {
        if (g_cur_step == t->count) t->count++;
        t->notes[g_cur_step].pitch = p;
        g_cur_step++;
        g_dirty = 1;
    }
}

static void track_enter_rest(void) {
    Track *t = &g_secs[g_cur_sec].tracks[g_cur_voice];
    g_pending_sharp = 0;
    while (t->count < g_cur_step && t->count < MAX_NOTES) {
        t->notes[t->count].pitch = PITCH_REST;
        t->notes[t->count].dur   = DUR_QN;
        t->count++;
    }
    if (g_cur_step < MAX_NOTES) {
        if (g_cur_step == t->count) t->count++;
        t->notes[g_cur_step].pitch = PITCH_REST;
        g_cur_step++;
        g_dirty = 1;
    }
}

static void handle_track(int ch) {
    Track *t = &g_secs[g_cur_sec].tracks[g_cur_voice];

    switch (ch) {
        /* Navigation */
        case KEY_UP:
            if (g_cur_step > 0) g_cur_step--;
            g_pending_sharp = 0;
            break;
        case KEY_DOWN:
            g_cur_step++;
            g_pending_sharp = 0;
            break;
        case KEY_LEFT:
            if (g_cur_voice > 0) { g_cur_voice--; g_pending_sharp = 0; }
            break;
        case KEY_RIGHT:
            if (g_cur_voice < NUM_VOICES - 1) { g_cur_voice++; g_pending_sharp = 0; }
            break;
        case KEY_PPAGE:
            if (g_cur_sec > 0) { g_cur_sec--; g_cur_step = 0; g_scroll = 0; }
            break;
        case KEY_NPAGE:
            if (g_cur_sec < g_sec_count - 1) { g_cur_sec++; g_cur_step = 0; g_scroll = 0; }
            break;
        case '\t':
            g_cur_voice = (g_cur_voice + 1) % NUM_VOICES;
            g_pending_sharp = 0;
            break;

        case '#': g_pending_sharp ^= 1; break;

        /* Note letters */
        case 'a': case 'A': track_enter_note('a'); break;
        case 'b': case 'B': track_enter_note('b'); break;
        case 'c': case 'C': track_enter_note('c'); break;
        case 'd': case 'D': track_enter_note('d'); break;
        case 'e': case 'E': track_enter_note('e'); break;
        case 'f': case 'F': track_enter_note('f'); break;
        case 'g': case 'G': track_enter_note('g'); break;

        /* Octave (changes current note's octave, stays on step) */
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': {
            if (g_cur_step < t->count && t->notes[g_cur_step].pitch != PITCH_REST) {
                int semi = t->notes[g_cur_step].pitch % 12;
                int oct  = ch - '0';
                int midi = (oct + 1) * 12 + semi;
                if (midi >= 0 && midi <= 127) {
                    t->notes[g_cur_step].pitch = (uint8_t)midi;
                    g_dirty = 1;
                }
            }
            break;
        }

        /* Rest */
        case '-': case 'r': track_enter_rest(); break;

        /* Duration — changes current step, does not advance */
        case 'w': case 'W':
            if (g_cur_step < t->count) { t->notes[g_cur_step].dur = DUR_WN; g_dirty = 1; }
            break;
        case 'h':
            if (g_cur_step < t->count) { t->notes[g_cur_step].dur = DUR_HN; g_dirty = 1; }
            break;
        case 'q':
            if (g_cur_step < t->count) { t->notes[g_cur_step].dur = DUR_QN; g_dirty = 1; }
            break;
        case 'i': case 'I':  /* eighth — 'e' taken by note E */
            if (g_cur_step < t->count) { t->notes[g_cur_step].dur = DUR_EN; g_dirty = 1; }
            break;
        case 'x': case 'X': {  /* toggle dotted */
            if (g_cur_step < t->count) {
                static const uint8_t dotted[DUR_COUNT] = {
                    DUR_DH, DUR_DH, DUR_DQ, DUR_HN, DUR_QN, DUR_DE, DUR_EN
                };
                t->notes[g_cur_step].dur =
                    dotted[t->notes[g_cur_step].dur % DUR_COUNT];
                g_dirty = 1;
            }
            break;
        }

        /* Insert rest at cursor, push notes down */
        case KEY_IC:
            if (t->count < MAX_NOTES) {
                int ins = g_cur_step < t->count ? g_cur_step : t->count;
                memmove(&t->notes[ins + 1], &t->notes[ins],
                        (size_t)(t->count - ins) * sizeof(Note));
                t->notes[ins].pitch = PITCH_REST;
                t->notes[ins].dur   = DUR_QN;
                t->count++;
                g_dirty = 1;
            }
            break;

        /* Delete note at cursor, pull notes up */
        case KEY_DC:
            if (g_cur_step < t->count) {
                memmove(&t->notes[g_cur_step], &t->notes[g_cur_step + 1],
                        (size_t)(t->count - g_cur_step - 1) * sizeof(Note));
                t->count--;
                if (g_cur_step > t->count) g_cur_step = t->count;
                g_dirty = 1;
            }
            break;

        case 'z': case 'Z':   g_mode = MODE_ARRANGE; break;
        case 'p': case 'P':
            g_mode = MODE_PARAMS;
            g_par_voice = 0; g_par_field = 0;
            break;
    }
}

static void handle_arrange(int ch) {
    switch (ch) {
        case KEY_UP:   if (g_arr_cur > 0)             g_arr_cur--; break;
        case KEY_DOWN: if (g_arr_cur < g_arr_len - 1) g_arr_cur++; break;

        /* Set section index at cursor */
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9': {
            int s = ch - '0';
            if (s < g_sec_count) { g_arr[g_arr_cur] = (uint8_t)s; g_dirty = 1; }
            break;
        }

        case KEY_IC:  /* Insert new entry after cursor */
            if (g_arr_len < MAX_ARR) {
                memmove(&g_arr[g_arr_cur + 1], &g_arr[g_arr_cur],
                        (size_t)(g_arr_len - g_arr_cur));
                g_arr[g_arr_cur] = 0;
                g_arr_len++;
                g_dirty = 1;
            }
            break;
        case KEY_DC:  /* Delete entry at cursor */
            if (g_arr_len > 1) {
                memmove(&g_arr[g_arr_cur], &g_arr[g_arr_cur + 1],
                        (size_t)(g_arr_len - g_arr_cur - 1));
                g_arr_len--;
                if (g_arr_cur >= g_arr_len) g_arr_cur = g_arr_len - 1;
                g_dirty = 1;
            }
            break;

        case '+':  /* Add a new empty section */
            if (g_sec_count < MAX_SECTIONS) {
                memset(&g_secs[g_sec_count], 0, sizeof(Section));
                g_sec_count++;
                g_dirty = 1;
            }
            break;
        case '-': {  /* Remove last section if it's not in the arrangement */
            if (g_sec_count > 1) {
                int in_use = 0;
                for (int i = 0; i < g_arr_len; i++)
                    if (g_arr[i] == g_sec_count - 1) in_use = 1;
                if (!in_use) { g_sec_count--; g_dirty = 1; }
            }
            break;
        }

        case 'z': case 'Z': case 27:  g_mode = MODE_TRACK;  break;
    }
}

static uint8_t *par_field_ptr(int v, int fi) {
    switch (fi) {
        case 0: return &g_wave[v];
        case 1: return &g_atk_ms[v];
        case 2: return &g_dcy_10ms[v];
        case 3: return &g_sus_pct[v];
        case 4: return &g_rel_ms[v];
        default: return &g_gate_pct[v];
    }
}

static void handle_params(int ch) {
    static const int maxv[6] = { 3, 255, 255, 100, 255, 100 };
    static const int minv[6] = { 0,   1,   1,   0,   1,   0 };

    switch (ch) {
        case KEY_UP:    if (g_par_voice > 0)               g_par_voice--; break;
        case KEY_DOWN:  if (g_par_voice < NUM_VOICES - 1)  g_par_voice++; break;
        case KEY_LEFT:  if (g_par_field > 0)               g_par_field--; break;
        case KEY_RIGHT: if (g_par_field < 5)               g_par_field++; break;

        case '+': case '=': {
            uint8_t *vp = par_field_ptr(g_par_voice, g_par_field);
            if ((int)*vp < maxv[g_par_field]) { (*vp)++; g_dirty = 1; }
            break;
        }
        case '-': {
            uint8_t *vp = par_field_ptr(g_par_voice, g_par_field);
            if ((int)*vp > minv[g_par_field]) { (*vp)--; g_dirty = 1; }
            break;
        }
        case 'w': case 'W':
            g_wave[g_par_voice] = (g_wave[g_par_voice] + 1) % 4;
            g_dirty = 1;
            break;
        case 'p': case 'P': case 27:  g_mode = MODE_TRACK;  break;
    }
}

static void handle_browse(int ch) {
    switch (ch) {
        case KEY_UP:   if (g_browse_sel > 0)                  g_browse_sel--; break;
        case KEY_DOWN: if (g_browse_sel < g_browse_count - 1) g_browse_sel++; break;
        case '\n': case KEY_ENTER:
            if (g_browse_count > 0) {
                strncpy(g_path, g_browse_files[g_browse_sel], sizeof(g_path) - 1);
                load_mus(g_path);
                g_mode = MODE_TRACK;
            }
            break;
        case 27:  g_mode = MODE_TRACK;  break;
    }
}

/* ── Inline text prompt (call while ncurses is active) ────────────────── */

static void prompt(int y, const char *label, char *out, int maxlen) {
    echo(); curs_set(1);
    mvprintw(y, 0, "%s", label);
    clrtoeol(); refresh();
    getnstr(out, maxlen - 1);
    noecho(); curs_set(0);
}

/* ── Main ─────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    /* Ensure assets/music/ exists */
    {
        FILE *k = fopen("assets/music/.keep", "w");
        if (k) fclose(k);
    }

    reset_song();
    strncpy(g_path, "assets/music/untitled.mus", sizeof(g_path) - 1);

    if (argc >= 2) {
        strncpy(g_path, argv[1], sizeof(g_path) - 1);
        g_path[sizeof(g_path) - 1] = '\0';
        load_mus(g_path);
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    start_color();

    init_pair(CP_DIM,   COLOR_BLACK,  COLOR_BLACK);
    init_pair(CP_HDR,   COLOR_CYAN,   COLOR_BLACK);
    init_pair(CP_SAVED, COLOR_GREEN,  COLOR_BLACK);
    init_pair(CP_DIRTY, COLOR_RED,    COLOR_BLACK);

    int running = 1;
    while (running) {
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        clear();

        draw_header(cols);
        switch (g_mode) {
            case MODE_TRACK:   draw_track(rows);   break;
            case MODE_ARRANGE: draw_arrange(rows); break;
            case MODE_PARAMS:  draw_params(rows);  break;
            case MODE_BROWSE:  draw_browse(rows);  break;
        }
        refresh();

        int ch = getch();

        /* Global keys available in all modes */
        switch (ch) {
            case 's': case 'S':
                save_mus(g_path);
                goto next_frame;
            case 'o': case 'O':
                if (g_dirty) save_mus(g_path);
                scan_music();
                g_browse_sel = 0;
                g_mode = MODE_BROWSE;
                goto next_frame;
            case 'n': case 'N': {
                if (g_dirty) save_mus(g_path);
                char namebuf[64] = {0};
                prompt(rows - 1, "New file (assets/music/): ", namebuf, sizeof(namebuf));
                if (!namebuf[0]) goto next_frame;
                snprintf(g_path, sizeof(g_path), "assets/music/%s.mus", namebuf);
                reset_song();
                char titlebuf[32] = {0};
                prompt(rows - 1, "Title: ", titlebuf, sizeof(titlebuf));
                if (titlebuf[0]) memcpy(g_title, titlebuf, 32);
                char bpmbuf[8] = {0};
                prompt(rows - 1, "BPM (40-255): ", bpmbuf, sizeof(bpmbuf));
                int b = atoi(bpmbuf);
                if (b >= 40 && b <= 255) g_bpm = (uint8_t)b;
                g_dirty = 1;
                goto next_frame;
            }
            case 't': case 'T': {
                char buf[32] = {0};
                prompt(rows - 1, "Title: ", buf, sizeof(buf));
                if (buf[0]) { memcpy(g_title, buf, 32); g_dirty = 1; }
                goto next_frame;
            }
            case 'B': {  /* uppercase only — lowercase 'b' is note B in track mode */
                char buf[8] = {0};
                prompt(rows - 1, "BPM (40-255): ", buf, sizeof(buf));
                int b = atoi(buf);
                if (b >= 40 && b <= 255) { g_bpm = (uint8_t)b; g_dirty = 1; }
                goto next_frame;
            }
            case 'Q':  /* uppercase only — lowercase 'q' is quarter note in track mode */
                running = 0;
                goto next_frame;
        }

        switch (g_mode) {
            case MODE_TRACK:   handle_track(ch);   break;
            case MODE_ARRANGE: handle_arrange(ch); break;
            case MODE_PARAMS:  handle_params(ch);  break;
            case MODE_BROWSE:  handle_browse(ch);  break;
        }

        next_frame:;
    }

    endwin();

    if (g_dirty) {
        printf("Unsaved changes. Save? (y/n): ");
        fflush(stdout);
        int c = getchar();
        if (c == 'y' || c == 'Y') save_mus(g_path);
    }

    return 0;
}
