/* audio.c — multi-voice PCM synthesis via waveOut.
   Voices: triangle / square(NES duty) / saw / noise / sine / Karplus-Strong.
   ADSR envelope per voice.  Songs described by SongDef structs. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "audio.h"
#include "pak.h"

/* --- Audio format --- */
#define SR       22050
#define BUFSZ    4096
#define NBUFS    2
#define KS_BUFMAX 1024  /* supports Karplus-Strong notes down to ~22 Hz at 22050 Hz */
#define REV_COMB1 1557
#define REV_COMB2 1801
#define REV_AP     521

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
        { s_mel, MEL_N, 4, 1.0f, 0.5f, 0 },   /* sine   — melody  */
        { s_har, HAR_N, 1, 1.0f, 0.5f, 0 },   /* square — harmony */
        { s_bas, BAS_N, 5, 1.0f, 0.5f, 0 },   /* KS     — bass    */
        { s_drm, DRM_N, 3, 1.0f, 0.5f, 0 },   /* noise  — drums   */
    },
    0.12f
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
    float    prev_f;              /* last note's pitch for slide interpolation */
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
    int      crushed; /* 1 = 4-bit quantize this voice's output */
} ActiveVoice;

/* --- Globals --- */
static ActiveVoice     s_av[SONG_MAX_TRACKS];
static int             s_nvoices = 0;
static const SongDef  *s_cur_song = NULL;

/* --- Runtime-loaded song state --- */
static SongDef *s_loaded_song = NULL;
static char     s_loaded_name[64];

static volatile int    s_vol = 0;
static HWAVEOUT        s_wo     = NULL;
static HANDLE          s_thread = NULL;
static HANDLE          s_evt    = NULL;
static volatile LONG   s_stop   = 1;
static int16_t         s_pcm[NBUFS][BUFSZ];
static WAVEHDR         s_hdr[NBUFS];
static float           s_reverb = 0.0f;
static float           s_revComb1[REV_COMB1];
static float           s_revComb2[REV_COMB2];
static float           s_revAp[REV_AP];
static int             s_revPos1 = 0, s_revPos2 = 0, s_revApPos = 0;

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

static float clamp_duty(float duty) {
    if (duty <= 0.0f) return 0.5f;
    if (duty < 0.125f) return 0.125f;
    if (duty > 0.75f) return 0.75f;
    return duty;
}

static float clamp_reverb(float wet) {
    if (wet < 0.0f) return 0.0f;
    if (wet > 0.5f) return 0.5f;
    return wet;
}

static void reverb_reset(void) {
    memset(s_revComb1, 0, sizeof(s_revComb1));
    memset(s_revComb2, 0, sizeof(s_revComb2));
    memset(s_revAp, 0, sizeof(s_revAp));
    s_revPos1 = s_revPos2 = s_revApPos = 0;
}

static float reverb_tick(float in) {
    float d1 = s_revComb1[s_revPos1];
    float d2 = s_revComb2[s_revPos2];
    s_revComb1[s_revPos1] = in * 0.35f + d1 * 0.74f;
    s_revComb2[s_revPos2] = in * 0.35f + d2 * 0.70f;
    if (++s_revPos1 >= REV_COMB1) s_revPos1 = 0;
    if (++s_revPos2 >= REV_COMB2) s_revPos2 = 0;

    float wet = (d1 + d2) * 0.5f;
    float ap = s_revAp[s_revApPos];
    float out = ap - wet;
    s_revAp[s_revApPos] = wet + ap * 0.45f;
    if (++s_revApPos >= REV_AP) s_revApPos = 0;
    return out;
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

    /* Slide: interpolate frequency from previous note's pitch to |nt->f| */
    float eff_f;
    if (nt->f < 0.0f) {
        float t = (float)(v->np + 1) / (float)(nt->d > 0 ? nt->d : 1);
        if (t > 1.0f) t = 1.0f;
        eff_f = v->prev_f + (-nt->f - v->prev_f) * t;
    } else {
        eff_f = nt->f;
    }

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
    if (v->ep != ENV_OFF && eff_f > 0.0f) {
        if (v->wave == 3) {
            /* NES 15-bit LFSR: feedback = bit0 XOR bit1, clocked at note pitch.
               Reuses ks_len (LFSR state) and ph (phase accumulator) — both
               unused for wave=3 in all other branches. */
            v->ph += eff_f / (float)SR;
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
            v->ph += eff_f / (float)SR;
            if (v->ph >= 1.0f) v->ph -= 1.0f;
            switch (v->wave) {
                case 0: {
                    float p = v->ph - 0.5f;
                    s = 1.0f - 4.0f*(p<0?-p:p);
                    /* 4-bit NES quantization: 32 steps per cycle */
                    s = (float)((int)(s * 16.0f + 16.5f) - 16) / 16.0f;
                    /* smooth quantization steps — heavier LPF on low notes where stepping is audible */
                    { float a = eff_f < 300.0f ? 0.12f : 0.35f;
                      v->lpf_state += a * (s - v->lpf_state);
                      s = v->lpf_state; }
                    break;
                }
                case 1:
                    s = v->ph < v->duty ? 1.0f : -1.0f;
                    /* low bass notes: LPF rounds harsh square into warm hum */
                    if (eff_f < 200.0f) {
                        v->lpf_state += 0.10f * (s - v->lpf_state);
                        s = v->lpf_state;
                    }
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
        if (nt->f != 0.0f) v->prev_f = nt->f < 0.0f ? -nt->f : nt->f;
        v->np = 0;
        v->ni = (v->ni + 1) % n;
        {
            const Note *nxt = &seq[v->ni];
            if (nxt->f >= 0.0f) {   /* not a slide — reset phase and envelope */
                v->ph  = 0.0f;
                v->env = 0.0f;
                v->ep  = ENV_ATK;
            }
        }
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
            float vs = voice_tick(&av->v, av->seq, av->n) * s_wave_vol[wave] * g;
            if (av->crushed) vs = (float)((int)(vs * 8.0f)) / 8.0f;
            v += vs;
        }
        if (s_reverb > 0.0f) {
            float wet = reverb_tick(v);
            v = v * (1.0f - s_reverb) + wet * s_reverb;
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

/* --- MUSX v8 binary decoder --- */

static uint32_t musx_varint(const uint8_t *d, int *p) {
    uint32_t val = 0; int shift = 0;
    uint8_t b;
    do { b = d[(*p)++]; val |= (uint32_t)(b & 0x7F) << shift; shift += 7; } while (b & 0x80);
    return val;
}

static int musx_bits(int n) { return n<=1?0:n==2?1:n<=4?2:n<=16?4:8; }

typedef struct { const uint8_t *d; int p, a, av; } MBR;

static int mbr_read(MBR *br, int w) {
    if (!w) return 0;
    while (br->av < w) { br->a |= (int)br->d[br->p++] << br->av; br->av += 8; }
    int val = br->a & ((1 << w) - 1);
    br->a >>= w; br->av -= w;
    return val;
}

static SongDef *musx_decode(const uint8_t *data, int size) {
    if (size < 5) return NULL;
    if (data[0]!='M'||data[1]!='U'||data[2]!='S'||data[3]!='X') return NULL;
    if (data[4] != 8) return NULL;

    int p = 5;
    float reverb = data[p++] / 255.0f;
    int nlen = data[p++];
    p += nlen; /* skip name */

    int ntracks = (int)musx_varint(data, &p);
    if (ntracks <= 0 || ntracks > SONG_MAX_TRACKS) return NULL;

    /* Pass 1: count total notes and record per-track start offsets */
    int track_start[SONG_MAX_TRACKS];
    int total_notes = 0;

    int p2 = p;
    for (int t = 0; t < ntracks; t++) {
        track_start[t] = p2;
        p2 += 5; /* wave(1) + flags(1) + gain_q(1) + duty_q(1) + detune_q(1) */
        int cnt = (int)musx_varint(data, &p2);
        total_notes += cnt;
        if (cnt == 0) continue;
        int nf = data[p2++]; p2 += nf * 4;       /* freq palette */
        int nd = data[p2++];
        for (int i = 0; i < nd; i++) musx_varint(data, &p2); /* dur palette varints */
        int nv = data[p2++]; p2 += nv;            /* vel palette */
        int bpn = musx_bits(nf) + musx_bits(nd) + musx_bits(nv);
        p2 += (bpn * cnt + 7) / 8;
    }

    /* Allocate: one block for SongDef + Note pool */
    SongDef *song = (SongDef *)malloc(sizeof(SongDef) + (size_t)total_notes * sizeof(Note));
    if (!song) return NULL;
    Note *pool = (Note *)(song + 1);
    int pool_off = 0;

    song->ntracks = ntracks;
    song->reverb  = reverb < 0.0f ? 0.0f : reverb > 0.5f ? 0.5f : reverb;

    /* Pass 2: decode each track */
    for (int t = 0; t < ntracks; t++) {
        int pp = track_start[t];
        int wave    =  data[pp++];
        int flags   =  data[pp++];
        float gain  =  data[pp++] / 255.0f;
        float duty  =  data[pp++] / 255.0f;
        pp++; /* detune_q — not in TrackDef */
        (void)flags; /* pad bit not in TrackDef */

        int cnt = (int)musx_varint(data, &pp);

        song->tracks[t].wave    = wave;
        song->tracks[t].gain    = gain > 0.0f ? gain : 1.0f;
        song->tracks[t].duty    = duty;
        song->tracks[t].crushed = (flags >> 1) & 1;
        song->tracks[t].n       = cnt;
        song->tracks[t].seq     = &pool[pool_off];

        if (cnt == 0) continue;

        int nf = data[pp++];
        uint32_t freq_pal[256];
        for (int i = 0; i < nf; i++) {
            freq_pal[i] = (uint32_t)data[pp] | ((uint32_t)data[pp+1]<<8) |
                          ((uint32_t)data[pp+2]<<16) | ((uint32_t)data[pp+3]<<24);
            pp += 4;
        }

        int nd = data[pp++];
        uint32_t dur_pal[256];
        for (int i = 0; i < nd; i++) dur_pal[i] = musx_varint(data, &pp);

        int nv = data[pp++];
        uint8_t vel_pal[256];
        for (int i = 0; i < nv; i++) vel_pal[i] = data[pp++];

        int fw = musx_bits(nf), dw = musx_bits(nd), vw = musx_bits(nv);
        MBR br; br.d = data; br.p = pp; br.a = 0; br.av = 0;

        for (int i = 0; i < cnt; i++) {
            int fi = mbr_read(&br, fw);
            int di = mbr_read(&br, dw);
            int vi = mbr_read(&br, vw);
            pool[pool_off + i].f = nf > 0 ? (float)freq_pal[fi] / 100.0f : 0.0f;
            pool[pool_off + i].d = nd > 0 ? (int)dur_pal[di] : 0;
            pool[pool_off + i].v = nv > 0 ? (float)vel_pal[vi] / 255.0f : 1.0f;
        }
        pool_off += cnt;
    }

    return song;
}

void audioPlayMusic(const char *name) {
    if (s_wo && s_loaded_name[0] && strcmp(name, s_loaded_name) == 0) return;
    audioStop();
    free(s_loaded_song); s_loaded_song = NULL; s_loaded_name[0] = 0;
    PakData pd = pakRead(name);
    if (!pd.data) return;
    s_loaded_song = musx_decode(pd.data, (int)pd.size);
    free(pd.data);
    if (!s_loaded_song) return;
    strncpy(s_loaded_name, name, 63); s_loaded_name[63] = 0;
    audioPlaySong(s_loaded_song);
}

/* --- Public API --- */

void audioPlaySong(const SongDef *song) {
    if (s_cur_song == song && s_wo) return;
    audioStop();
    if (!song) return;

    s_cur_song = song;
    s_reverb   = clamp_reverb(song->reverb);
    reverb_reset();
    s_nvoices  = song->ntracks < SONG_MAX_TRACKS ? song->ntracks : SONG_MAX_TRACKS;

    for (int t = 0; t < s_nvoices; t++) {
        const TrackDef *td = &song->tracks[t];
        voice_reset(&s_av[t].v, td->wave);
        s_av[t].seq  = td->seq;
        s_av[t].n    = td->n;
        s_av[t].gain    = td->gain > 0.0f ? td->gain : 1.0f;
        s_av[t].v.duty  = clamp_duty(td->duty);
        s_av[t].crushed = td->crushed;
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

void audioCleanup(void) {
    audioStop();
    free(s_loaded_song); s_loaded_song = NULL; s_loaded_name[0] = 0;
}
