/* audio.c — multi-voice PCM synthesis via waveOut.
   Voices: triangle / square(NES duty) / saw / noise / sine / Karplus-Strong.
   ADSR envelope per voice.  Songs described by SongDef structs. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <string.h>
#include "audio.h"

/* --- Audio format --- */
#define SR       22050
#define BUFSZ    4096
#define NBUFS    2
#define KS_BUFMAX 170   /* ceil(SR/C3_Hz)+1 */

/* --- Built-in world theme (110 BPM) --- */
#define QN 12027   /* quarter note at 110 BPM */
#define HN (QN*2)
#define WN (QN*4)
#define DH (QN*3)

#define C3f 130.81f
#define D3f 146.83f
#define E3f 164.81f
#define F3f 174.61f
#define G3f 196.00f
#define A3f 220.00f
#define C4f 261.63f
#define D4f 293.66f
#define E4f 329.63f
#define F4f 349.23f
#define G4f 392.00f
#define A4f 440.00f
#define C5f 523.25f

static const Note s_mel[] = {
    {C4f,QN,1.0f},{E4f,QN,1.0f},{G4f,QN,1.0f},{A4f,QN,1.0f},
    {G4f,DH,1.0f},{E4f,QN,1.0f},
    {D4f,QN,1.0f},{E4f,QN,1.0f},{G4f,QN,1.0f},{E4f,QN,1.0f},
    {C4f,WN,1.0f},
    {A4f,QN,1.0f},{G4f,QN,1.0f},{E4f,QN,1.0f},{G4f,QN,1.0f},
    {C5f,QN,1.0f},{A4f,QN,1.0f},{G4f,QN,1.0f},{E4f,QN,1.0f},
    {F4f,QN,1.0f},{G4f,QN,1.0f},{A4f,QN,1.0f},{G4f,QN,1.0f},
    {E4f,HN,1.0f},{C4f,HN,1.0f},
};
static const Note s_har[] = {
    {E4f,WN,1.0f},{G4f,WN,1.0f},{E4f,HN,1.0f},{C4f,HN,1.0f},{E4f,WN,1.0f},
    {C4f,WN,1.0f},{D4f,WN,1.0f},{C4f,WN,1.0f},{G4f,WN,1.0f},
};
static const Note s_bas[] = {
    {C3f,HN,1.0f},{C3f,HN,1.0f},{G3f,HN,1.0f},{G3f,HN,1.0f},{A3f,HN,1.0f},{E3f,HN,1.0f},{C3f,WN,1.0f},
    {F3f,HN,1.0f},{G3f,HN,1.0f},{F3f,HN,1.0f},{G3f,HN,1.0f},{F3f,HN,1.0f},{G3f,HN,1.0f},{C3f,WN,1.0f},
};
static const Note s_drm[] = {
    {0.0f,QN,1.0f},{1.0f,QN,1.0f},{0.0f,QN,1.0f},{1.0f,QN,1.0f},
};
#define MEL_N (int)(sizeof(s_mel)/sizeof(s_mel[0]))
#define HAR_N (int)(sizeof(s_har)/sizeof(s_har[0]))
#define BAS_N (int)(sizeof(s_bas)/sizeof(s_bas[0]))
#define DRM_N (int)(sizeof(s_drm)/sizeof(s_drm[0]))

static const SongDef s_world_song = {
    4, {
        { s_mel, MEL_N, 4, 1.0f },   /* sine   — melody  */
        { s_har, HAR_N, 1, 1.0f },   /* square — harmony */
        { s_bas, BAS_N, 5, 1.0f },   /* KS     — bass    */
        { s_drm, DRM_N, 3, 1.0f },   /* noise  — drums   */
    }
};

/* --- ADSR envelope --- */
typedef enum { ENV_ATK, ENV_DCY, ENV_SUS, ENV_REL, ENV_OFF } EnvPhase;

/* --- Voice --- */
typedef struct {
    int      ni, np;
    float    ph;
    float    env;
    EnvPhase ep;
    float    atk, dcy, sus, rel, gate_frac;
    int      wave;
    float    duty;                /* square duty cycle [0.1, 0.9]; 0.5 = classic */
    float    lpf_state;
    float    ks_buf[KS_BUFMAX];
    int      ks_len, ks_pos;
} Voice;

/* One active voice bound to a TrackDef */
typedef struct {
    Voice    v;
    const Note *seq;
    int      n;
    float    gain;
} ActiveVoice;

/* --- Globals --- */
static ActiveVoice     s_av[SONG_MAX_TRACKS];
static int             s_nvoices = 0;
static const SongDef  *s_cur_song = NULL;

static volatile int    s_vol = 8;
static HWAVEOUT        s_wo     = NULL;
static HANDLE          s_thread = NULL;
static HANDLE          s_evt    = NULL;
static volatile LONG   s_stop   = 1;
static int16_t         s_pcm[NBUFS][BUFSZ];
static WAVEHDR         s_hdr[NBUFS];

/* --- Noise PRNG --- */
static uint32_t s_nsr = 0xCAFEBABEu;
static float noise_next(void) {
    s_nsr ^= s_nsr << 13;
    s_nsr ^= s_nsr >> 17;
    s_nsr ^= s_nsr << 5;
    return (int32_t)s_nsr / 2147483648.0f;
}

/* --- Per-wave default ADSR --- */
static void voice_defaults(Voice *v, int wave) {
    v->duty = 0.5f;
    switch (wave) {
        case 0:  /* triangle — NES: hard gate, no envelope at all */
            v->atk = 1.0f; v->dcy = 1.0f; v->sus = 1.0f;
            v->rel = 1.0f; v->gate_frac = 0.995f; break;
        case 1:  /* square — NES pulse: fast attack, clean */
            v->atk = 1.0f/(SR*0.003f); v->dcy = 1.0f; v->sus = 1.0f;
            v->rel = 0.005f; v->gate_frac = 0.92f; break;
        case 2:  /* saw — piano approximation: sharp attack, fast decay */
            v->atk = 1.0f; v->dcy = 1.0f/(SR*0.25f); v->sus = 0.25f;
            v->rel = 0.25f/(SR*0.15f); v->gate_frac = 0.9f; break;
        case 3:  /* noise — NES: instant attack, short decay */
            v->atk = 1.0f; v->dcy = 1.0f/(SR*0.06f); v->sus = 0.0f;
            v->rel = 0.02f; v->gate_frac = 1.0f; break;
        case 4:  /* sine — flute/pad */
            v->atk = 1.0f/(SR*0.015f); v->dcy = 1.0f/(SR*0.5f); v->sus = 0.75f;
            v->rel = 0.75f/(SR*0.05f); v->gate_frac = 0.85f; break;
        case 5:  /* Karplus-Strong — plucked */
            v->atk = 1.0f; v->dcy = 1.0f; v->sus = 1.0f;
            v->rel = 0.002f; v->gate_frac = 0.85f; break;
        default:
            v->atk = 1.0f/(SR*0.01f); v->dcy = 1.0f; v->sus = 1.0f;
            v->rel = 0.005f; v->gate_frac = 0.9f; break;
    }
}

static void voice_reset(Voice *v, int wave) {
    memset(v, 0, sizeof(*v));
    v->wave = wave;
    voice_defaults(v, wave);
    v->ep = ENV_ATK;
    if (wave == 3) v->ks_len = 0x7FFF;  /* seed LFSR — must be non-zero */
}

/* Per-wave mix weights (sum kept close to 1.0 by soft-clip) */
static const float s_wave_vol[6] = {
    0.35f,  /* 0 triangle */
    0.28f,  /* 1 square   */
    0.22f,  /* 2 saw      */
    0.10f,  /* 3 noise    */
    0.35f,  /* 4 sine     */
    0.30f,  /* 5 KS       */
};

static float voice_tick(Voice *v, const Note *seq, int n) {
    const Note *nt = &seq[v->ni];

    /* KS: seed delay line on new note */
    if (v->wave == 5 && v->np == 0 && nt->f > 0.0f) {
        v->ks_len = (int)((float)SR / nt->f + 0.5f);
        if (v->ks_len < 2)       v->ks_len = 2;
        if (v->ks_len > KS_BUFMAX) v->ks_len = KS_BUFMAX;
        for (int j = 0; j < v->ks_len; j++) v->ks_buf[j] = noise_next();
        v->ks_pos = 0;
    }

    /* Gate-off at gate_frac → begin release */
    if (v->np == (int)(nt->d * v->gate_frac) && v->ep != ENV_REL && v->ep != ENV_OFF)
        v->ep = ENV_REL;

    /* ADSR */
    switch (v->ep) {
        case ENV_ATK:
            v->env += v->atk;
            if (v->env >= 1.0f) { v->env = 1.0f; v->ep = ENV_DCY; } break;
        case ENV_DCY:
            v->env -= v->dcy;
            if (v->env <= v->sus) { v->env = v->sus; v->ep = ENV_SUS; } break;
        case ENV_SUS: break;
        case ENV_REL:
            v->env -= v->rel;
            if (v->env <= 0.0f) { v->env = 0.0f; v->ep = ENV_OFF; } break;
        case ENV_OFF: break;
    }

    float s = 0.0f;
    if (v->ep != ENV_OFF && nt->f > 0.0f) {
        if (v->wave == 3) {
            /* NES 15-bit LFSR: feedback = bit0 XOR bit1, clocked at note pitch.
               Reuses ks_len (LFSR state) and ph (phase accumulator) — both
               unused for wave=3 in all other branches. */
            float rate = nt->f > 0.0f ? nt->f : 220.0f;
            v->ph += rate / (float)SR;
            while (v->ph >= 1.0f) {
                v->ph -= 1.0f;
                int fb = (v->ks_len & 1) ^ ((v->ks_len >> 1) & 1);
                v->ks_len = (fb << 14) | ((v->ks_len >> 1) & 0x3FFF);
            }
            s = (v->ks_len & 1) ? 1.0f : -1.0f;
        } else if (v->wave == 5) {
            float a = v->ks_buf[v->ks_pos];
            float b = v->ks_buf[(v->ks_pos + 1) % v->ks_len];
            s = (a + b) * 0.4985f;
            v->ks_buf[v->ks_pos] = s;
            v->ks_pos = (v->ks_pos + 1) % v->ks_len;
        } else {
            v->ph += nt->f / (float)SR;
            if (v->ph >= 1.0f) v->ph -= 1.0f;
            switch (v->wave) {
                case 0: {
                    float p = v->ph - 0.5f;
                    s = 1.0f - 4.0f*(p<0?-p:p);
                    /* 4-bit NES quantization: 32 steps per cycle */
                    s = (float)((int)(s * 16.0f + 16.5f) - 16) / 16.0f;
                    break;
                }
                case 1:
                    /* hard-switching square — NES pulse character, no LPF */
                    s = v->ph < v->duty ? 1.0f : -1.0f;
                    break;
                case 2: s = 2.0f * v->ph - 1.0f; break;
                case 4: {
                    /* Bhaskara I sine approximation — no math.h */
                    float x = v->ph < 0.5f ? v->ph * 6.28318f : (v->ph - 0.5f) * 6.28318f;
                    float q = x * (3.14159f - x);
                    s = 16.0f * q / (49.348f - 4.0f * q);
                    if (v->ph >= 0.5f) s = -s;
                    break;
                }
            }
        }
        s *= v->env * (nt->v > 0.0f ? nt->v : 1.0f);
    }

    /* Advance note */
    if (++v->np >= nt->d) {
        v->np  = 0;
        v->ni  = (v->ni + 1) % n;
        v->ph  = 0.0f;
        v->env = 0.0f;
        v->ep  = ENV_ATK;
    }

    return s;
}

/* --- Volume --- */
void audioSetVolume(int v) { s_vol = v < 0 ? 0 : v > 10 ? 10 : v; }
int  audioGetVolume(void)  { return s_vol; }

/* --- Sample fill --- */
static void fill(int16_t *buf, int n) {
    float vol = s_vol / 10.0f;
    int nv = s_nvoices;
    for (int i = 0; i < n; i++) {
        float v = 0.0f;
        for (int t = 0; t < nv; t++) {
            ActiveVoice *av = &s_av[t];
            int wave = av->v.wave < 6 ? av->v.wave : 1;
            float g = av->gain > 0.0f ? av->gain : 1.0f;
            v += voice_tick(&av->v, av->seq, av->n) * s_wave_vol[wave] * g;
        }
        /* cubic soft clip */
        if      (v >  1.0f) v =  1.0f;
        else if (v < -1.0f) v = -1.0f;
        else                v *= 1.5f - 0.5f*v*v;
        buf[i] = (int16_t)(v * 26000.0f * vol);
    }
}

/* --- waveOut thread --- */
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

/* --- Public API --- */

void audioPlaySong(const SongDef *song) {
    if (s_cur_song == song && s_wo) return;
    audioStop();
    if (!song) return;

    s_cur_song = song;
    s_nvoices  = song->ntracks < SONG_MAX_TRACKS ? song->ntracks : SONG_MAX_TRACKS;

    for (int t = 0; t < s_nvoices; t++) {
        const TrackDef *td = &song->tracks[t];
        voice_reset(&s_av[t].v, td->wave);
        s_av[t].seq  = td->seq;
        s_av[t].n    = td->n;
        s_av[t].gain = td->gain > 0.0f ? td->gain : 1.0f;
    }

    WAVEFORMATEX fmt = {
        WAVE_FORMAT_PCM, 1, SR, SR*2, 2, 16, 0
    };

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

void audioPlayWorld(void) {
    audioPlaySong(&s_world_song);
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
    s_cur_song = NULL;
}

void audioCleanup(void) { audioStop(); }
