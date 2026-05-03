/* audio.c — procedural PCM synthesis via waveOut.
   Four-voice synth: melody (triangle) + harmony (square pad) + bass (plucked) + percussion (noise).
   No math.h, no external libs. ADSR envelope per voice. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <string.h>
#include "audio.h"

/* --- Audio format --- */
#define SR       22050   /* sample rate */
#define BUFSZ    4096    /* samples per waveOut buffer (~185ms each) */
#define NBUFS    2       /* double-buffer */
#define KS_BUFMAX 170    /* ceil(SR/C3f)+1 — longest Karplus-Strong delay line */

/* --- Note durations at 110 BPM --- */
/* Quarter note = SR*60/110 = 12027 samples */
#define QN 12027
#define HN (QN*2)
#define WN (QN*4)
#define DH (QN*3)   /* dotted half */

typedef struct { float f; int d; } Note;  /* frequency Hz, duration samples */

/* Frequencies */
#define C3f  130.81f
#define D3f  146.83f
#define E3f  164.81f
#define F3f  174.61f
#define G3f  196.00f
#define A3f  220.00f
#define C4f  261.63f
#define D4f  293.66f
#define E4f  329.63f
#define F4f  349.23f
#define G4f  392.00f
#define A4f  440.00f
#define C5f  523.25f

/* 8-bar melody — triangle, sustained */
static const Note s_mel[] = {
    {C4f,QN},{E4f,QN},{G4f,QN},{A4f,QN},   /* bar 1: ascending lift    */
    {G4f,DH},{E4f,QN},                       /* bar 2: dotted-half peak  */
    {D4f,QN},{E4f,QN},{G4f,QN},{E4f,QN},   /* bar 3: bounce            */
    {C4f,WN},                                /* bar 4: resolve           */
    {A4f,QN},{G4f,QN},{E4f,QN},{G4f,QN},   /* bar 5: variation         */
    {C5f,QN},{A4f,QN},{G4f,QN},{E4f,QN},   /* bar 6: descend from high */
    {F4f,QN},{G4f,QN},{A4f,QN},{G4f,QN},   /* bar 7: build-back        */
    {E4f,HN},{C4f,HN},                       /* bar 8: final resolve     */
};
#define MEL_N (int)(sizeof(s_mel)/sizeof(s_mel[0]))

/* 8-bar harmony pad — square, whole notes outlining chords */
static const Note s_har[] = {
    {E4f,WN},               /* bar 1: C  — third  */
    {G4f,WN},               /* bar 2: G  — root   */
    {E4f,HN},{C4f,HN},     /* bar 3: Am — third, root */
    {E4f,WN},               /* bar 4: C  — third  */
    {C4f,WN},               /* bar 5: F  — fifth  */
    {D4f,WN},               /* bar 6: G  — fifth  */
    {C4f,WN},               /* bar 7: F  — fifth  */
    {G4f,WN},               /* bar 8: C  — fifth  */
};
#define HAR_N (int)(sizeof(s_har)/sizeof(s_har[0]))

/* 8-bar bass line — triangle, plucked */
static const Note s_bas[] = {
    {C3f,HN},{C3f,HN},
    {G3f,HN},{G3f,HN},
    {A3f,HN},{E3f,HN},
    {C3f,WN},
    {F3f,HN},{G3f,HN},
    {F3f,HN},{G3f,HN},
    {F3f,HN},{G3f,HN},
    {C3f,WN},
};
#define BAS_N (int)(sizeof(s_bas)/sizeof(s_bas[0]))

/* 1-bar drum loop — noise, off-beats only (f>0 = hit, f=0 = rest) */
static const Note s_drm[] = {
    {0.0f,QN},{1.0f,QN},{0.0f,QN},{1.0f,QN},
};
#define DRM_N (int)(sizeof(s_drm)/sizeof(s_drm[0]))

/* --- ADSR envelope state --- */
typedef enum { ENV_ATK, ENV_DCY, ENV_SUS, ENV_REL, ENV_OFF } EnvPhase;

/* --- Voice --- */
typedef struct {
    /* sequence position */
    int      ni, np;
    float    ph;
    /* envelope state */
    float    env;
    EnvPhase ep;
    /* envelope params (set once at voice init) */
    float    atk;        /* per-sample rise during attack  */
    float    dcy;        /* per-sample fall during decay   */
    float    sus;        /* sustain level [0,1]            */
    float    rel;        /* per-sample fall during release */
    float    gate_frac;  /* fraction of note to hold gate  */
    /* timbre: 0=triangle  1=square(lpf)  2=saw  3=noise  4=sine  5=karplus-strong */
    int      wave;
    float    lpf_state;          /* one-pole IIR state for square wave */
    float    ks_buf[KS_BUFMAX];  /* Karplus-Strong delay line */
    int      ks_len, ks_pos;     /* KS buffer length and read/write head */
} Voice;

static Voice s_mv, s_hv, s_bv, s_pv;

/* Xorshift noise — no stdlib rand(), deterministic and cheap */
static uint32_t s_nsr = 0xCAFEBABEu;
static float noise_next(void) {
    s_nsr ^= s_nsr << 13;
    s_nsr ^= s_nsr >> 17;
    s_nsr ^= s_nsr << 5;
    return (int32_t)s_nsr / 2147483648.0f;
}

static void voice_init(Voice *v, int wave,
                       float atk, float dcy, float sus, float rel,
                       float gate_frac) {
    memset(v, 0, sizeof(*v));
    v->wave      = wave;
    v->atk       = atk;
    v->dcy       = dcy;
    v->sus       = sus;
    v->rel       = rel;
    v->gate_frac = gate_frac;
    v->ep        = ENV_ATK;
}

static float voice_tick(Voice *v, const Note *seq, int n) {
    const Note *nt = &seq[v->ni];

    /* KS: seed delay line with noise at the start of each new note */
    if (v->wave == 5 && v->np == 0 && nt->f > 0.0f) {
        v->ks_len = (int)((float)SR / nt->f + 0.5f);
        if (v->ks_len < 2) v->ks_len = 2;
        if (v->ks_len > KS_BUFMAX) v->ks_len = KS_BUFMAX;
        for (int j = 0; j < v->ks_len; j++) v->ks_buf[j] = noise_next();
        v->ks_pos = 0;
    }

    /* Gate off at gate_frac of note duration → begin release */
    if (v->np == (int)(nt->d * v->gate_frac) && v->ep != ENV_REL && v->ep != ENV_OFF)
        v->ep = ENV_REL;

    /* ADSR state machine */
    switch (v->ep) {
        case ENV_ATK:
            v->env += v->atk;
            if (v->env >= 1.0f) { v->env = 1.0f; v->ep = ENV_DCY; }
            break;
        case ENV_DCY:
            v->env -= v->dcy;
            if (v->env <= v->sus) { v->env = v->sus; v->ep = ENV_SUS; }
            break;
        case ENV_SUS: break;
        case ENV_REL:
            v->env -= v->rel;
            if (v->env <= 0.0f) { v->env = 0.0f; v->ep = ENV_OFF; }
            break;
        case ENV_OFF: break;
    }

    /* Oscillator */
    float s = 0.0f;
    if (v->ep != ENV_OFF && nt->f > 0.0f) {
        if (v->wave == 3) {
            s = noise_next();
        } else if (v->wave == 5) {
            /* Karplus-Strong: averaging feedback — natural plucked decay */
            float a = v->ks_buf[v->ks_pos];
            float b = v->ks_buf[(v->ks_pos + 1) % v->ks_len];
            s = (a + b) * 0.4985f;   /* 0.4985 < 0.5: slightly lossy for stable decay */
            v->ks_buf[v->ks_pos] = s;
            v->ks_pos = (v->ks_pos + 1) % v->ks_len;
        } else {
            v->ph += nt->f / (float)SR;
            if (v->ph >= 1.0f) v->ph -= 1.0f;
            switch (v->wave) {
                case 0: { float p = v->ph - 0.5f; s = 1.0f - 4.0f*(p<0.0f?-p:p); break; }
                case 1:
                    /* square with one-pole LPF (a=0.75, fc≈1020 Hz) — removes harshness */
                    s = v->ph < 0.5f ? 1.0f : -1.0f;
                    v->lpf_state = v->lpf_state * 0.75f + s * 0.25f;
                    s = v->lpf_state;
                    break;
                case 2: s = 2.0f * v->ph - 1.0f; break;
                case 4: {
                    /* Bhaskara I sine approximation — max error 0.17%, no math.h */
                    float x = v->ph < 0.5f ? v->ph * 6.28318f : (v->ph - 0.5f) * 6.28318f;
                    float q = x * (3.14159f - x);
                    s = 16.0f * q / (49.348f - 4.0f * q);
                    if (v->ph >= 0.5f) s = -s;
                    break;
                }
            }
        }
        s *= v->env;
    }

    /* Advance note — fresh attack on next note */
    if (++v->np >= nt->d) {
        v->np  = 0;
        v->ni  = (v->ni + 1) % n;
        v->ph  = 0.0f;
        v->env = 0.0f;
        v->ep  = ENV_ATK;
    }

    return s;
}

/* --- Volume (0–10) --- */
static volatile int s_vol = 8;

void audioSetVolume(int v) { s_vol = v < 0 ? 0 : v > 10 ? 10 : v; }
int  audioGetVolume(void)  { return s_vol; }

/* --- waveOut streaming --- */
static HWAVEOUT       s_wo     = NULL;
static HANDLE         s_thread = NULL;
static HANDLE         s_evt    = NULL;
static volatile LONG  s_stop   = 1;
static int16_t        s_pcm[NBUFS][BUFSZ];
static WAVEHDR        s_hdr[NBUFS];

static void fill(int16_t *buf, int n) {
    float vol = s_vol / 10.0f;
    for (int i = 0; i < n; i++) {
        float v = voice_tick(&s_mv, s_mel, MEL_N) * 0.40f
                + voice_tick(&s_hv, s_har, HAR_N) * 0.12f
                + voice_tick(&s_bv, s_bas, BAS_N) * 0.25f
                + voice_tick(&s_pv, s_drm, DRM_N) * 0.07f;
        /* cubic soft clip: f(x)=1.5x-0.5x³ — smooth saturation into ±1 */
        if      (v >  1.0f) v =  1.0f;
        else if (v < -1.0f) v = -1.0f;
        else                v *= 1.5f - 0.5f*v*v;
        buf[i] = (int16_t)(v * 26000.0f * vol);
    }
}

static DWORD WINAPI audio_thread(LPVOID arg) {
    (void)arg;
    while (!s_stop) {
        WaitForSingleObject(s_evt, 200);
        for (int i = 0; i < NBUFS; i++) {
            if (!s_stop && (s_hdr[i].dwFlags & WHDR_DONE)) {
                waveOutUnprepareHeader(s_wo, &s_hdr[i], sizeof(WAVEHDR));
                fill(s_pcm[i], BUFSZ);
                waveOutPrepareHeader(s_wo, &s_hdr[i], sizeof(WAVEHDR));
                waveOutWrite(s_wo, &s_hdr[i], sizeof(WAVEHDR));
            }
        }
    }
    return 0;
}

void audioPlayWorld(void) {
    if (s_wo) return;

    /* melody: sine, flute-like — slow attack, high sustain */
    voice_init(&s_mv, 4,
        1.0f/(SR*0.015f),       /* 15ms attack  */
        1.0f/(SR*0.5f),         /* 500ms decay  */
        0.75f,                  /* sustain       */
        0.75f/(SR*0.05f),       /* 50ms release */
        0.85f);

    /* harmony: square, soft pad — slow attack blends in */
    voice_init(&s_hv, 1,
        1.0f/(SR*0.08f),        /* 80ms attack  */
        1.0f/(SR*2.0f),         /* 2s decay      */
        0.55f,
        0.55f/(SR*0.06f),       /* 60ms release */
        0.88f);

    /* bass: Karplus-Strong — natural plucked string decay */
    voice_init(&s_bv, 5,
        1.0f,                   /* instant attack */
        1.0f,                   /* instant through DCY (sus=1 so no decay) */
        1.0f,                   /* sustain=1; KS feedback handles natural decay */
        0.002f,                 /* release */
        0.85f);                 /* gate closes at 85% for clean note separation */

    /* percussion: noise, hi-hat feel — instant attack, fast decay */
    voice_init(&s_pv, 3,
        1.0f,                   /* instant attack */
        1.0f/(SR*0.04f),        /* 40ms decay */
        0.0f,
        0.01f,
        1.0f);

    WAVEFORMATEX fmt;
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = 1;
    fmt.nSamplesPerSec  = SR;
    fmt.nAvgBytesPerSec = SR * 2;
    fmt.nBlockAlign     = 2;
    fmt.wBitsPerSample  = 16;
    fmt.cbSize          = 0;

    s_evt = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!s_evt) return;

    if (waveOutOpen(&s_wo, WAVE_MAPPER, &fmt,
                    (DWORD_PTR)s_evt, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(s_evt); s_evt = NULL; return;
    }

    InterlockedExchange(&s_stop, 0);

    for (int i = 0; i < NBUFS; i++) {
        memset(&s_hdr[i], 0, sizeof(WAVEHDR));
        s_hdr[i].lpData         = (LPSTR)s_pcm[i];
        s_hdr[i].dwBufferLength = BUFSZ * 2;
        fill(s_pcm[i], BUFSZ);
        waveOutPrepareHeader(s_wo, &s_hdr[i], sizeof(WAVEHDR));
        waveOutWrite(s_wo, &s_hdr[i], sizeof(WAVEHDR));
    }

    s_thread = CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
}

void audioStop(void) {
    if (!s_wo) return;
    InterlockedExchange(&s_stop, 1);
    if (s_evt) SetEvent(s_evt);
    if (s_thread) {
        WaitForSingleObject(s_thread, 3000);
        CloseHandle(s_thread);
        s_thread = NULL;
    }
    waveOutReset(s_wo);
    for (int i = 0; i < NBUFS; i++)
        waveOutUnprepareHeader(s_wo, &s_hdr[i], sizeof(WAVEHDR));
    waveOutClose(s_wo);
    s_wo = NULL;
    if (s_evt) { CloseHandle(s_evt); s_evt = NULL; }
}

void audioCleanup(void) { audioStop(); }
