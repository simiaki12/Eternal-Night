/*
 * music_editor_gui.c — Tiny Hero music editor, Win32 GUI
 * Iteration 2: piano roll with note placement, 4 voices, horizontal scroll.
 *
 * Controls:
 *   Left-click grid   — place note at pitch/time; drag right to set length
 *   Right-click grid  — erase note
 *   Left-click piano  — preview pitch
 *   Mouse wheel       — scroll timeline (1 beat per notch)
 *   Left / Right      — scroll timeline (1 beat)
 *   1 / 2 / 3 / 4    — select voice (Melody / Harmony / Bass / Perc)
 *
 * Build: make music_editor_gui
 *   Ctrl+S — save as .mus
 *   Space  — play / stop
 *   [ / ]  — BPM -5 / +5
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Logical canvas ───────────────────────────────────────────────────── */
#define LOGICAL_W  640
#define LOGICAL_H  480

/* ── Piano roll layout ────────────────────────────────────────────────── */
#define PIANO_W   50      /* keyboard strip on the left                    */
#define STATUS_H  12      /* status bar at the bottom                      */
#define NOTE_LO   48      /* lowest MIDI note shown: C3                    */
#define NOTE_HI   84      /* one past highest: C6                          */
#define N_ROWS   (NOTE_HI - NOTE_LO)             /* 36 semitones           */
#define GRID_H   (LOGICAL_H - STATUS_H)          /* 468 px                 */
#define ROW_H    (GRID_H / N_ROWS)               /* 13 px per row          */

/* ── Time grid ────────────────────────────────────────────────────────── */
#define SUBDIVS   4       /* 16th notes per quarter-note beat               */
#define SUBDIV_W  8       /* pixels per 16th note                           */
#define BEAT_W   (SUBDIV_W * SUBDIVS)            /* 32 px per beat         */

/* ── Note data ────────────────────────────────────────────────────────── */
#define MAX_PLACED 1024
#define NUM_VOICES    4

typedef struct { int midi, tick, len, voice; } Note;

static Note g_notes[MAX_PLACED];
static int  g_note_count  = 0;
static int  g_cur_voice   = 0;
static int  g_scroll_tick = 0;   /* leftmost visible tick */
static int  g_bpm         = 120;

/* placement drag state */
static int  g_placing    = 0;
static int  g_place_idx  = -1;
static int  g_place_base = 0;

/* ── Frequencies for MIDI 48-83 (C3 to B5), no math.h ───────────────── */
static const float g_freq[N_ROWS] = {
    130.81f, 138.59f, 146.83f, 155.56f, 164.81f, 174.61f,  /* C3  – F3  */
    185.00f, 196.00f, 207.65f, 220.00f, 233.08f, 246.94f,  /* F#3 – B3  */
    261.63f, 277.18f, 293.66f, 311.13f, 329.63f, 349.23f,  /* C4  – F4  */
    369.99f, 392.00f, 415.30f, 440.00f, 466.16f, 493.88f,  /* F#4 – B4  */
    523.25f, 554.37f, 587.33f, 622.25f, 659.25f, 698.46f,  /* C5  – F5  */
    739.99f, 783.99f, 830.61f, 880.00f, 932.33f, 987.77f,  /* F#5 – B5  */
};

/* ── Voice colours (0xRRGGBB) ─────────────────────────────────────────── */
static const uint32_t g_vcol[NUM_VOICES] = {
    0x3B64D2u,   /* steel blue  — melody    */
    0x2DA05Au,   /* teal green  — harmony   */
    0xC88228u,   /* amber       — bass      */
    0xB4378Cu,   /* magenta     — perc      */
};
static const uint32_t g_vcol_hi[NUM_VOICES] = {
    0x628CFFu,
    0x55C882u,
    0xF0AA50u,
    0xDC5FB4u,
};

/* ── GDI software backbuffer ──────────────────────────────────────────── */
static HWND      g_hwnd;
static HDC       g_dc;
static HBITMAP   g_bmp;
static uint32_t *g_pix;
static int       g_winW = LOGICAL_W;
static int       g_winH = LOGICAL_H;

/* ── Audio ────────────────────────────────────────────────────────────── */
#define SR          22050
#define NOTE_SAMPS  (SR * 3 / 4)   /* 750 ms */

static HWAVEOUT  g_wo           = NULL;
static WAVEHDR   g_hdr          = {0};
static int16_t   g_buf[NOTE_SAMPS];
static int16_t  *g_play_buf     = NULL;
static int       g_playing_full = 0;
static int       g_play_total   = 0;   /* samples in g_play_buf */

/* ── Selection highlight ──────────────────────────────────────────────── */
static int g_sel = -1;   /* MIDI note of highlighted row; -1 = none */

/* ── Key helpers ──────────────────────────────────────────────────────── */
static int is_black_key(int midi) {
    int s = midi % 12;
    return s==1||s==3||s==6||s==8||s==10;
}

static const char *semitone_name(int midi) {
    static const char *n[12] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };
    return n[midi % 12];
}

/* ── Pixel ops ────────────────────────────────────────────────────────── */
static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r<<16)|((uint32_t)g<<8)|b;
}

static void fill_rect(int x, int y, int w, int h, uint32_t c) {
    int x1=x<0?0:x,            y1=y<0?0:y;
    int x2=x+w>LOGICAL_W?LOGICAL_W:x+w;
    int y2=y+h>LOGICAL_H?LOGICAL_H:y+h;
    for (int py=y1; py<y2; py++)
        for (int px=x1; px<x2; px++)
            g_pix[py*LOGICAL_W+px] = c;
}

/* ── Time ↔ pixel coordinate conversion ──────────────────────────────── */
static int tick_to_px(int tick) { return PIANO_W + (tick - g_scroll_tick) * SUBDIV_W; }
static int px_to_tick(int lx)   { int gx=lx-PIANO_W; return gx<0 ? -1 : g_scroll_tick+gx/SUBDIV_W; }

/* ── Note ops ─────────────────────────────────────────────────────────── */
static int find_note(int midi, int tick) {
    for (int i=0; i<g_note_count; i++)
        if (g_notes[i].midi==midi && tick>=g_notes[i].tick && tick<g_notes[i].tick+g_notes[i].len)
            return i;
    return -1;
}

static void remove_note(int idx) { g_notes[idx] = g_notes[--g_note_count]; }

/* ── Screen ↔ logical canvas coordinate conversion ───────────────────── */
static void screen_to_logical(int sx, int sy, int *lx, int *ly) {
    int dstW, dstH;
    if (g_winW*LOGICAL_H > g_winH*LOGICAL_W) {
        dstH=g_winH; dstW=LOGICAL_W*g_winH/LOGICAL_H;
    } else {
        dstW=g_winW; dstH=LOGICAL_H*g_winW/LOGICAL_W;
    }
    int ox=(g_winW-dstW)/2, oy=(g_winH-dstH)/2;
    *lx = (sx-ox)*LOGICAL_W / (dstW>0?dstW:1);
    *ly = (sy-oy)*LOGICAL_H / (dstH>0?dstH:1);
}

static int logical_to_midi(int lx, int ly) {
    if (lx<0||lx>=LOGICAL_W)   return -1;
    if (ly<0||ly>=N_ROWS*ROW_H) return -1;
    return NOTE_HI-1 - ly/ROW_H;
}

/* ── Synthesis ────────────────────────────────────────────────────────── */
/*  0=Melody: triangle   1=Harmony: 25% pulse   2=Bass: sawtooth   3=Perc: noise */
static float synth_wave(int voice, float ph, uint32_t *seed) {
    switch (voice & 3) {
    case 0: return ph < 0.5f ? 4.0f*ph - 1.0f : 3.0f - 4.0f*ph;
    case 1: return ph < 0.5f ? 1.0f : -1.0f;
    case 2: return 2.0f*ph - 1.0f;
    default:
        *seed = *seed * 1664525u + 1013904223u;
        return (float)(int32_t)*seed / 2147483648.0f;
    }
}

/* Fill adsr params and vol for a voice; ns may be clamped for perc */
static void voice_params(int voice, int *ns,
                          float *vol, int *atk, int *dcy, float *sus, int *rel) {
    switch (voice & 3) {
    case 0: *vol=0.28f; *atk=SR/100; *dcy=SR/20;  *sus=0.65f; *rel=(*ns>SR/4?SR/5:*ns/4); break;
    case 1: *vol=0.18f; *atk=SR/30;  *dcy=SR/10;  *sus=0.70f; *rel=(*ns>SR/3?SR/4:*ns/3); break;
    case 2: *vol=0.24f; *atk=SR/400; *dcy=SR/25;  *sus=0.80f; *rel=(*ns>SR/5?SR/8:*ns/4); break;
    default:*vol=0.22f; *atk=SR/500; *dcy=SR/8;   *sus=0.00f; *rel=1; *ns=SR/8; break;
    }
}

/* ── Audio ────────────────────────────────────────────────────────────── */
static void audio_stop(void) {
    if (!g_wo) return;
    waveOutReset(g_wo);
    waveOutUnprepareHeader(g_wo, &g_hdr, sizeof(WAVEHDR));
    waveOutClose(g_wo);
    g_wo = NULL;
    free(g_play_buf); g_play_buf = NULL;
    g_playing_full = 0;
    KillTimer(g_hwnd, 1);
}

static void play_all(void) {
    audio_stop();
    if (g_note_count == 0) return;

    int max_tick = 0;
    for (int i = 0; i < g_note_count; i++) {
        int end = g_notes[i].tick + g_notes[i].len;
        if (end > max_tick) max_tick = end;
    }

    int spt   = (SR * 60) / (g_bpm * SUBDIVS);
    int total = max_tick * spt + SR;

    g_play_buf = calloc(total, sizeof(int16_t));
    if (!g_play_buf) return;

    for (int ni = 0; ni < g_note_count; ni++) {
        Note *n = &g_notes[ni];
        if (n->midi < NOTE_LO || n->midi >= NOTE_HI) continue;

        float freq  = g_freq[n->midi - NOTE_LO];
        int   ns    = n->len * spt;
        int   start = n->tick * spt;
        int   v     = n->voice & 3;

        float vol; int atk, dcy, rel; float sus;
        voice_params(v, &ns, &vol, &atk, &dcy, &sus, &rel);
        int rs = ns - rel;
        if (rs < atk + dcy) rs = atk + dcy;

        uint32_t seed = (uint32_t)(ni * 2654435761u + 1);
        float ph = 0.0f, ph_inc = freq / (float)SR, lp = 0.0f;
        for (int i = 0; i < ns && start + i < total; i++) {
            float amp;
            if      (i < atk)       amp = (float)i / atk;
            else if (i < atk + dcy) amp = 1.0f - (1.0f - sus) * (float)(i - atk) / dcy;
            else if (i < rs)        amp = sus;
            else                    amp = sus * (1.0f - (float)(i - rs) / rel);
            if (amp < 0.0f) amp = 0.0f;

            ph += ph_inc; if (ph >= 1.0f) ph -= 1.0f;
            float s = synth_wave(v, ph, &seed);
            if (v == 1) { lp = lp * 0.40f + s * 0.60f; s = lp; }  /* soften square  */
            if (v == 2) { lp = lp * 0.55f + s * 0.45f; s = lp; }  /* warm the saw   */

            int32_t mix = g_play_buf[start + i] + (int32_t)(s * amp * vol * 32767.0f);
            if (mix >  32767) mix =  32767;
            if (mix < -32768) mix = -32768;
            g_play_buf[start + i] = (int16_t)mix;
        }
    }

    WAVEFORMATEX fmt = {WAVE_FORMAT_PCM, 1, SR, SR*2, 2, 16, 0};
    if (waveOutOpen(&g_wo, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        free(g_play_buf); g_play_buf = NULL; return;
    }
    memset(&g_hdr, 0, sizeof(g_hdr));
    g_hdr.lpData         = (LPSTR)g_play_buf;
    g_hdr.dwBufferLength = total * sizeof(int16_t);
    waveOutPrepareHeader(g_wo, &g_hdr, sizeof(WAVEHDR));
    waveOutWrite(g_wo, &g_hdr, sizeof(WAVEHDR));
    g_play_total   = total;
    g_playing_full = 1;
    SetTimer(g_hwnd, 1, 40, NULL);   /* ~25fps redraw */
}

static void audio_play(float freq, int voice) {
    audio_stop();
    int   ns  = NOTE_SAMPS;
    float vol; int atk, dcy, rel; float sus;
    voice_params(voice, &ns, &vol, &atk, &dcy, &sus, &rel);
    int rel_start = ns - rel;
    if (rel_start < atk + dcy) rel_start = atk + dcy;
    uint32_t seed = 12345;
    float ph = 0.0f, ph_inc = freq / (float)SR, lp = 0.0f;
    for (int i = 0; i < ns; i++) {
        float amp;
        if      (i < atk)         amp = (float)i / atk;
        else if (i < atk + dcy)   amp = 1.0f - (1.0f-sus) * (float)(i-atk) / dcy;
        else if (i < rel_start)   amp = sus;
        else                      amp = sus * (1.0f - (float)(i-rel_start) / rel);
        if (amp < 0.0f) amp = 0.0f;
        ph += ph_inc; if (ph >= 1.0f) ph -= 1.0f;
        float s = synth_wave(voice, ph, &seed);
        if (voice == 1) { lp = lp * 0.40f + s * 0.60f; s = lp; }
        if (voice == 2) { lp = lp * 0.55f + s * 0.45f; s = lp; }
        g_buf[i] = (int16_t)(s * amp * 22000.0f);
    }
    WAVEFORMATEX fmt = {WAVE_FORMAT_PCM, 1, SR, SR*2, 2, 16, 0};
    if (waveOutOpen(&g_wo, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return;
    memset(&g_hdr, 0, sizeof(g_hdr));
    g_hdr.lpData = (LPSTR)g_buf; g_hdr.dwBufferLength = ns * 2;
    waveOutPrepareHeader(g_wo, &g_hdr, sizeof(WAVEHDR));
    waveOutWrite(g_wo, &g_hdr, sizeof(WAVEHDR));
}

/* ── .mus export ─────────────────────────────────────────────────────── */
#define MUS_MAGIC "MUS0"

/* Duration table: ticks (16th notes) → DUR_* value */
static const int k_dur_ticks[7] = { 16, 12, 8, 6, 4, 3, 2 };
static const int k_dur_val[7]   = {  0,  3, 1, 4, 2, 6, 5 }; /* WN DH HN DQ QN DE EN */

static int nearest_dur(int ticks) {
    int best = 6, bestd = 9999;
    for (int i = 0; i < 7; i++) {
        int d = k_dur_ticks[i] - ticks; if (d < 0) d = -d;
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

static void save_mus(const char *path) {
    typedef struct { uint8_t pitch, dur; } MNote;
    static MNote out[NUM_VOICES][255];
    int          out_n[NUM_VOICES];

    for (int v = 0; v < NUM_VOICES; v++) {
        out_n[v] = 0;

        /* collect + sort notes for this voice by tick */
        Note sorted[MAX_PLACED]; int cnt = 0;
        for (int i = 0; i < g_note_count; i++)
            if (g_notes[i].voice == v) sorted[cnt++] = g_notes[i];
        for (int i = 0; i < cnt-1; i++)
            for (int j = i+1; j < cnt; j++)
                if (sorted[j].tick < sorted[i].tick)
                    { Note t=sorted[i]; sorted[i]=sorted[j]; sorted[j]=t; }

        int cur = 0;
        for (int i = 0; i < cnt && out_n[v] < 250; i++) {
            /* fill gap with rests (greedy largest-fit) */
            int gap = sorted[i].tick - cur;
            while (gap > 0 && out_n[v] < 254) {
                int di = 6;
                for (int d = 0; d < 7; d++) if (k_dur_ticks[d] <= gap) { di = d; break; }
                out[v][out_n[v]++] = (MNote){ 255, (uint8_t)k_dur_val[di] };
                gap -= k_dur_ticks[di];
            }
            int di = nearest_dur(sorted[i].len);
            out[v][out_n[v]++] = (MNote){ (uint8_t)sorted[i].midi, (uint8_t)k_dur_val[di] };
            cur = sorted[i].tick + k_dur_ticks[di];
        }
    }

    FILE *f = fopen(path, "wb");
    if (!f) { MessageBoxA(g_hwnd, "Could not write file.", "Save failed", MB_ICONERROR); return; }

    /* title = filename without extension */
    char title[32] = {0};
    const char *base = path;
    for (const char *p = path; *p; p++) if (*p=='/'||*p=='\\') base = p+1;
    strncpy(title, base, 31);
    char *dot = strchr(title, '.'); if (dot) *dot = 0;

    uint8_t bpm    = (uint8_t)g_bpm;
    uint8_t arr[64]= {0};              /* single section 0 */
    uint8_t al=1, sc=1;

    uint8_t wave[4]     = { 0, 1, 2, 3 };
    uint8_t atk_ms[4]   = { 10, 33,  3,  2 };
    uint8_t dcy_10ms[4] = {  5, 10,  4, 13 };
    uint8_t sus_pct[4]  = { 65, 70, 80,  0 };
    uint8_t rel_ms[4]   = {200,250,125, 10 };
    uint8_t gate_pct[4] = { 90, 90, 90, 60 };

    fwrite(MUS_MAGIC,  1,  4, f);
    fwrite(title,      1, 32, f);
    fwrite(&bpm,       1,  1, f);
    fwrite(&al,        1,  1, f);
    fwrite(arr,        1, 64, f);
    fwrite(&sc,        1,  1, f);
    fwrite(wave,       1,  4, f);
    fwrite(atk_ms,     1,  4, f);
    fwrite(dcy_10ms,   1,  4, f);
    fwrite(sus_pct,    1,  4, f);
    fwrite(rel_ms,     1,  4, f);
    fwrite(gate_pct,   1,  4, f);

    for (int v = 0; v < NUM_VOICES; v++) {
        uint8_t nc = (uint8_t)out_n[v];
        fwrite(&nc, 1, 1, f);
        for (int i = 0; i < out_n[v]; i++) {
            fwrite(&out[v][i].pitch, 1, 1, f);
            fwrite(&out[v][i].dur,   1, 1, f);
        }
    }
    fclose(f);
}

static char g_save_path[260] = "";

static void open_mus(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { MessageBoxA(g_hwnd, "Could not open file.", "Open failed", MB_ICONERROR); return; }

    char magic[4]; fread(magic, 1, 4, f);
    if (memcmp(magic, MUS_MAGIC, 4) != 0) {
        fclose(f);
        MessageBoxA(g_hwnd, "Not a valid .mus file.", "Open failed", MB_ICONERROR);
        return;
    }

    char    title[32]; fread(title, 1, 32, f);
    uint8_t bpm, arr_len; fread(&bpm, 1, 1, f); fread(&arr_len, 1, 1, f);
    uint8_t arr[64];       fread(arr,   1, 64, f);
    uint8_t sec_count;     fread(&sec_count, 1, 1, f);
    uint8_t skip[24];      fread(skip, 1, 24, f);   /* voice params */

    typedef struct { uint8_t pitch, dur; } MNote;
    static MNote secs[32][NUM_VOICES][255];
    static int   sec_nc[32][NUM_VOICES];

    for (int s = 0; s < sec_count && s < 32; s++)
        for (int v = 0; v < NUM_VOICES; v++) {
            uint8_t nc; fread(&nc, 1, 1, f);
            sec_nc[s][v] = nc;
            for (int i = 0; i < nc; i++) {
                fread(&secs[s][v][i].pitch, 1, 1, f);
                fread(&secs[s][v][i].dur,   1, 1, f);
            }
        }
    fclose(f);

    /* DUR_WN=0 DUR_HN=1 DUR_QN=2 DUR_DH=3 DUR_DQ=4 DUR_EN=5 DUR_DE=6 */
    static const int dur_ticks[7] = { 16, 8, 4, 12, 6, 2, 3 };

    g_note_count  = 0;
    g_bpm         = bpm;
    g_scroll_tick = 0;

    for (int v = 0; v < NUM_VOICES; v++) {
        int tick = 0;
        for (int si = 0; si < arr_len; si++) {
            int s = arr[si];
            if (s >= sec_count) continue;
            for (int i = 0; i < sec_nc[s][v] && g_note_count < MAX_PLACED; i++) {
                uint8_t pitch = secs[s][v][i].pitch;
                uint8_t dur   = secs[s][v][i].dur;
                int     len   = (dur < 7) ? dur_ticks[dur] : 4;
                if (pitch != 255 && pitch >= NOTE_LO && pitch < NOTE_HI)
                    g_notes[g_note_count++] = (Note){ pitch, tick, len, v };
                tick += len;
            }
        }
    }

    strncpy(g_save_path, path, sizeof(g_save_path) - 1);
}

static void do_open(void) {
    OPENFILENAMEA ofn = {0};
    char path[260] = "";
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = g_hwnd;
    ofn.lpstrFilter     = "Music Files\0*.mus\0All Files\0*.*\0";
    ofn.lpstrFile       = path;
    ofn.nMaxFile        = sizeof(path);
    ofn.lpstrDefExt     = "mus";
    ofn.lpstrInitialDir = "assets/music";
    ofn.Flags           = OFN_FILEMUSTEXIST;
    if (GetOpenFileNameA(&ofn))
        open_mus(path);
}

static void do_save(void) {
    if (g_save_path[0]) { save_mus(g_save_path); return; }

    OPENFILENAMEA ofn = {0};
    char path[260] = "untitled.mus";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = "Music Files\0*.mus\0All Files\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = sizeof(path);
    ofn.lpstrDefExt = "mus";
    ofn.lpstrInitialDir = "assets/music";
    ofn.Flags       = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameA(&ofn)) {
        strncpy(g_save_path, path, sizeof(g_save_path)-1);
        save_mus(g_save_path);
    }
}

/* ── Rendering ────────────────────────────────────────────────────────── */
static HFONT g_font = NULL;

static void draw(void) {
    fill_rect(0,0,LOGICAL_W,LOGICAL_H, rgb(20,20,28));

    /* Pass 1: row backgrounds (colour varies by pitch type and selection) */
    for (int row=0; row<N_ROWS; row++) {
        int midi = NOTE_HI-1-row;
        int y    = row*ROW_H;
        uint32_t bg;
        if      (midi==g_sel)    bg = rgb(30,50,88);
        else if (is_black_key(midi)) bg = rgb(22,22,32);
        else                     bg = rgb(38,38,50);
        fill_rect(PIANO_W, y, LOGICAL_W-PIANO_W, ROW_H, bg);
    }

    /* Pass 2: beat and bar lines */
    {
        int first = g_scroll_tick;
        int last  = g_scroll_tick + (LOGICAL_W-PIANO_W)/SUBDIV_W + 1;
        for (int t=first; t<=last; t++) {
            int px = tick_to_px(t);
            if (px<=PIANO_W || px>=LOGICAL_W) continue;
            uint32_t lc;
            if      (t%(SUBDIVS*4)==0) lc = rgb(55,55,82);   /* bar line      */
            else if (t%SUBDIVS==0)     lc = rgb(33,33,52);   /* beat line     */
            else if (t%2==0)           lc = rgb(25,25,38);   /* 8th-note line */
            else continue;
            fill_rect(px, 0, 1, N_ROWS*ROW_H, lc);
        }
    }

    /* Pass 3: note bars */
    for (int ni=0; ni<g_note_count; ni++) {
        Note *n = &g_notes[ni];
        if (n->midi<NOTE_LO||n->midi>=NOTE_HI) continue;
        int row = NOTE_HI-1-n->midi;
        int y   = row*ROW_H;
        int nx  = tick_to_px(n->tick);
        int nw  = n->len*SUBDIV_W - 1;
        if (nx>=LOGICAL_W || nx+nw<PIANO_W) continue;
        uint32_t nc   = g_vcol   [n->voice & 3];
        uint32_t nc_hi= g_vcol_hi[n->voice & 3];
        fill_rect(nx,   y+1,  nw,     ROW_H-2, nc);     /* body            */
        fill_rect(nx,   y+1,  nw,     1,        nc_hi);  /* top edge        */
        fill_rect(nx,   y+1,  1,      ROW_H-2,  nc_hi);  /* left edge       */
    }

    /* Playhead */
    if (g_playing_full && g_wo) {
        MMTIME mmt = {0}; mmt.wType = TIME_SAMPLES;
        waveOutGetPosition(g_wo, &mmt, sizeof(mmt));
        int spt = (SR * 60) / (g_bpm * SUBDIVS);
        int cur_tick = (int)mmt.u.sample / (spt > 0 ? spt : 1);
        /* auto-scroll: keep playhead in the right 3/4 of the view */
        int vis = (LOGICAL_W - PIANO_W) / SUBDIV_W;
        if (cur_tick >= g_scroll_tick + vis - SUBDIVS)
            g_scroll_tick = cur_tick - vis / 4;
        if (g_scroll_tick < 0) g_scroll_tick = 0;
        int px = tick_to_px(cur_tick);
        if (px >= PIANO_W && px < LOGICAL_W)
            fill_rect(px, 0, 2, N_ROWS * ROW_H, rgb(220, 40, 40));
    }

    /* Pass 4: row separators and octave-C markers */
    for (int row=0; row<N_ROWS; row++) {
        int midi = NOTE_HI-1-row;
        int y    = row*ROW_H;
        fill_rect(PIANO_W, y+ROW_H-1, LOGICAL_W-PIANO_W, 1, rgb(14,14,22));
        if (midi%12==0)
            fill_rect(PIANO_W, y, LOGICAL_W-PIANO_W, 1, rgb(62,62,90));
    }

    /* Pass 5: piano key strip */
    for (int row=0; row<N_ROWS; row++) {
        int midi = NOTE_HI-1-row;
        int y    = row*ROW_H;
        uint32_t kc;
        if      (midi==g_sel)        kc = rgb(75,130,220);
        else if (is_black_key(midi)) kc = rgb(16,16,16);
        else                         kc = rgb(205,205,210);
        fill_rect(2, y+1, PIANO_W-6, ROW_H-2, kc);
    }
    fill_rect(PIANO_W-2, 0, 2, N_ROWS*ROW_H, rgb(0,0,0));

    /* Status bar */
    {
        int sy = N_ROWS*ROW_H;
        fill_rect(0, sy, LOGICAL_W, STATUS_H, rgb(12,12,18));
        fill_rect(0, sy, LOGICAL_W, 1,        rgb(45,45,65));
    }

    /* ── GDI text ─────────────────────────────────────────────────────── */
    HFONT old = (HFONT)SelectObject(g_dc, g_font);
    SetBkMode(g_dc, TRANSPARENT);

    /* Bar numbers at top of each bar line */
    {
        int bar_ticks = SUBDIVS*4;
        int first_bar = g_scroll_tick / bar_ticks;
        int last_bar  = (g_scroll_tick + (LOGICAL_W-PIANO_W)/SUBDIV_W) / bar_ticks + 1;
        for (int b=first_bar; b<=last_bar; b++) {
            int px = tick_to_px(b*bar_ticks);
            if (px<PIANO_W || px>=LOGICAL_W-10) continue;
            char buf[8]; sprintf(buf, "%d", b+1);
            SetTextColor(g_dc, RGB(68,68,98));
            TextOutA(g_dc, px+2, 1, buf, (int)strlen(buf));
        }
    }

    /* Octave labels on piano strip */
    for (int row=0; row<N_ROWS; row++) {
        int midi = NOTE_HI-1-row;
        if (midi%12==0) {
            char buf[6]; sprintf(buf, "C%d", midi/12-1);
            SetTextColor(g_dc, RGB(88,88,105));
            TextOutA(g_dc, 4, row*ROW_H+1, buf, (int)strlen(buf));
        }
    }

    /* Status bar text */
    {
        static const char *vnames[4] = {"Melody","Harmony","Bass","Perc"};
        int sy = N_ROWS*ROW_H;
        char buf[160];
        if (g_sel>=NOTE_LO && g_sel<NOTE_HI) {
            sprintf(buf, "  [%d] %-7s  \xb7  %s%d  %.1f Hz  \xb7  BPM:%d  \xb7  %d notes  \xb7  SPACE=%s  [/]=bpm",
                    g_cur_voice+1, vnames[g_cur_voice],
                    semitone_name(g_sel), g_sel/12-1,
                    g_freq[g_sel-NOTE_LO], g_bpm,
                    g_note_count, g_wo ? "stop" : "play");
        } else {
            sprintf(buf, "  [%d] %-7s  \xb7  BPM:%d  \xb7  %d notes  \xb7  SPACE=%s  [/]=bpm  1-4=voice",
                    g_cur_voice+1, vnames[g_cur_voice], g_bpm, g_note_count,
                    g_wo ? "stop" : "play");
        }
        SetTextColor(g_dc, RGB(120,165,240));
        TextOutA(g_dc, 0, sy+1, buf, (int)strlen(buf));
    }

    SelectObject(g_dc, old);
}

/* ── Present (letterbox to window size) ──────────────────────────────── */
static void present(void) {
    HDC hdc = GetDC(g_hwnd);
    int dstW, dstH;
    if (g_winW*LOGICAL_H > g_winH*LOGICAL_W) {
        dstH=g_winH; dstW=LOGICAL_W*g_winH/LOGICAL_H;
    } else {
        dstW=g_winW; dstH=LOGICAL_H*g_winW/LOGICAL_W;
    }
    int ox=(g_winW-dstW)/2, oy=(g_winH-dstH)/2;
    HBRUSH blk = GetStockObject(BLACK_BRUSH);
    if (ox>0) {
        RECT r={0,0,ox,g_winH};             FillRect(hdc,&r,blk);
        r=(RECT){ox+dstW,0,g_winW,g_winH};  FillRect(hdc,&r,blk);
    }
    if (oy>0) {
        RECT r={0,0,g_winW,oy};             FillRect(hdc,&r,blk);
        r=(RECT){0,oy+dstH,g_winW,g_winH};  FillRect(hdc,&r,blk);
    }
    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchBlt(hdc,ox,oy,dstW,dstH, g_dc,0,0,LOGICAL_W,LOGICAL_H, SRCCOPY);
    ReleaseDC(g_hwnd, hdc);
}

/* ── Win32 message loop ───────────────────────────────────────────────── */
static int g_running = 1;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        g_winW=(int)LOWORD(lp); g_winH=(int)HIWORD(lp);
        return 0;

    case WM_LBUTTONDOWN: {
        SetCapture(hwnd);
        int lx, ly;
        screen_to_logical((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), &lx, &ly);
        int midi = logical_to_midi(lx, ly);
        int tick = px_to_tick(lx);
        g_sel = midi;
        if (midi>=NOTE_LO && midi<NOTE_HI)
            audio_play(g_freq[midi-NOTE_LO], g_cur_voice);
        /* Place note only when clicking in the grid area (not the piano strip) */
        if (lx>=PIANO_W && midi>=NOTE_LO && midi<NOTE_HI && tick>=0) {
            if (find_note(midi,tick)<0 && g_note_count<MAX_PLACED) {
                int i = g_note_count++;
                g_notes[i]   = (Note){ midi, tick, SUBDIVS, g_cur_voice };
                g_placing    = 1;
                g_place_idx  = i;
                g_place_base = tick;
            }
        }
        draw(); present();
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (g_placing && g_place_idx>=0 && (wp & MK_LBUTTON)) {
            int lx, ly;
            screen_to_logical((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), &lx, &ly);
            int tick = px_to_tick(lx);
            if (tick>=0) {
                int len = tick - g_place_base + 1;
                if (len<1) len=1;
                g_notes[g_place_idx].len = len;
                draw(); present();
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
        ReleaseCapture();
        g_placing=0; g_place_idx=-1;
        return 0;

    case WM_RBUTTONDOWN: {
        int lx, ly;
        screen_to_logical((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), &lx, &ly);
        int midi = logical_to_midi(lx, ly);
        int tick = px_to_tick(lx);
        if (lx>=PIANO_W && midi>=NOTE_LO && midi<NOTE_HI && tick>=0) {
            int idx = find_note(midi, tick);
            if (idx>=0) { remove_note(idx); draw(); present(); }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        g_scroll_tick -= (delta/WHEEL_DELTA) * SUBDIVS;
        if (g_scroll_tick<0) g_scroll_tick=0;
        draw(); present();
        return 0;
    }

    case WM_KEYDOWN:
        switch (wp) {
        case VK_LEFT:  g_scroll_tick -= SUBDIVS; break;
        case VK_RIGHT: g_scroll_tick += SUBDIVS; break;
        case '1': g_cur_voice=0; break;
        case '2': g_cur_voice=1; break;
        case '3': g_cur_voice=2; break;
        case '4': g_cur_voice=3; break;
        case VK_SPACE: if (g_wo) audio_stop(); else play_all(); break;
        case 'S': if (GetKeyState(VK_CONTROL) & 0x8000) do_save(); break;
        case 'O': if (GetKeyState(VK_CONTROL) & 0x8000) { audio_stop(); do_open(); } break;
        case 0xDB: if (g_bpm > 40)  g_bpm -= 5; break;  /* [ */
        case 0xDD: if (g_bpm < 240) g_bpm += 5; break;  /* ] */
        }
        if (g_scroll_tick<0) g_scroll_tick=0;
        draw(); present();
        return 0;

    case WM_TIMER:
        if (g_playing_full && g_wo && (g_hdr.dwFlags & WHDR_DONE))
            audio_stop();
        draw(); present();
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(hwnd,&ps); EndPaint(hwnd,&ps);
        present();
        return 0;
    }

    case WM_DESTROY:
        g_running=0; PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    (void)hPrev; (void)cmd;

    /* Create backbuffer DIBSection */
    HDC screen = GetDC(NULL);
    g_dc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = LOGICAL_W;
    bmi.bmiHeader.biHeight      = -LOGICAL_H;   /* top-down */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    g_bmp = CreateDIBSection(g_dc, &bmi, DIB_RGB_COLORS, (void**)&g_pix, NULL, 0);
    SelectObject(g_dc, g_bmp);

    /* Font for labels */
    g_font = CreateFontA(10,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,
                         ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                         DRAFT_QUALITY,FIXED_PITCH|FF_MODERN,"Courier New");

    /* Register and create window */
    WNDCLASSA wc     = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = "TinyHeroMusicEd";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc);

    RECT wr = {0, 0, LOGICAL_W, LOGICAL_H};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowA(
        "TinyHeroMusicEd", "Tiny Hero \x97 Music Editor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right-wr.left, wr.bottom-wr.top,
        NULL, NULL, hInst, NULL);

    draw();
    ShowWindow(g_hwnd, show);

    MSG msg;
    while (g_running && GetMessage(&msg,NULL,0,0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    audio_stop();
    DeleteObject(g_font);
    DeleteObject(g_bmp);
    DeleteDC(g_dc);
    return 0;
}
