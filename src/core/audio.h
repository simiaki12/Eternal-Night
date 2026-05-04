#pragma once
#include <stdint.h>

/* --- Note: one event in a voice sequence --- */
/* f > 0 → pitch in Hz.  f == 0 → rest.  d → duration in samples.
   v → velocity/amplitude 0.0-1.0; 0.0 treated as 1.0 (full). */
typedef struct { float f; int d; float v; } Note;

/* --- Track: one synthesizer voice within a song --- */
/* wave: 0=triangle  1=square  2=saw  3=noise  4=sine  5=Karplus-Strong */
typedef struct {
    const Note *seq;
    int         n;
    int         wave;
    float       gain;   /* per-channel volume [0.0, 2.0]; 0.0 treated as 1.0 */
    float       duty;   /* square duty [0.125, 0.75]; 0.0 treated as 0.5 */
} TrackDef;

/* --- Song: up to 24 simultaneous voice tracks --- */
#define SONG_MAX_TRACKS 24
typedef struct {
    int      ntracks;
    TrackDef tracks[SONG_MAX_TRACKS];
    float    reverb;   /* song-level wet amount [0.0, 0.5] */
} SongDef;

/* Play a song (stops whatever is currently playing, then loops). */
void audioPlaySong(const SongDef *song);

/* Legacy: play the built-in procedural world theme. */
void audioPlayWorld(void);

/* Stop any currently playing music. */
void audioStop(void);

/* Volume: 0 (silent) to 10 (full). Default 8. */
void audioSetVolume(int vol);
int  audioGetVolume(void);

/* Release resources on shutdown. */
void audioCleanup(void);
