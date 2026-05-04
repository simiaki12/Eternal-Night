# TinyHero Audio System — Development Roadmap

## Current State (as of 2026-05-04)

### Fidelity estimate: ~68%

The NES VST instruments (dominant in all 8 songs) sound reasonably accurate.
FL Keys / piano channels now use Karplus-Strong, but 3x Osc and Purity remain rough approximations.
Simple song-level reverb exists; bitcrush, Serum FX, EQ, and compression are still absent.

---

## Files Changed

| File | Status | What was done |
|---|---|---|
| `tools/flp2c.py` | New | Converts assets/music/*.flp → src/music/*.c. Reads arrangement clips, builds per-rack-channel timelines, assigns polyphonic notes to lanes, emits Note arrays + SongDef structs. Reads note velocity, fine pitch (cents), and channel volume. |
| `src/core/audio.h` | Modified | Added `Note.v` (velocity), `TrackDef.gain` (per-channel volume), `TrackDef.duty` (square duty cycle), `SongDef.reverb` (song-level wet amount), bumped `SONG_MAX_TRACKS` 8→24. |
| `src/core/audio.c` | Modified | Generalized to N voices via `ActiveVoice s_av[SONG_MAX_TRACKS]`. Added NES-accurate waveforms: 4-bit quantized triangle, hard-switching square with per-track duty, 15-bit LFSR noise. Per-wave ADSR presets. Velocity and gain applied in synthesis. `audioPlaySong()` API. Enlarged Karplus-Strong delay buffer for low piano notes. Added lightweight song-level reverb. |
| `src/music/songs.h` | New | Extern declarations for all 8 SongDef symbols. |
| `src/music/*.c` | Generated | 8 files, one per FLP. Run `make flp2c` or `python3 tools/flp2c.py` to regenerate. |
| `tools/verify_music.py` | New | Verifies FLP arrangement length, generated C loop length, voice count, playlist clipping counts, and mapped MP3 reference duration. |
| `src/core/main.c` | Modified | State-based song selection: each game state (menu, world, town, dungeon, combat, death) plays its corresponding song via `audioPlaySong()`. |
| `Makefile` | Modified | Added `-I src/music` to CFLAGS, added `flp2c:` and `verify_music:` targets. |
| `.vscode/c_cpp_properties.json` | Modified | Added `${workspaceFolder}/src/music` to includePath. |

---

## Converter Accuracy Pass

Before tuning synthesis fidelity, the converter must reproduce the same musical timeline as the FL Studio arrangement and reference MP3 exports. The first audit found that `Shining Star - Eternal Night ost.flp` arranged for ~108 seconds, matching `Target Detected (Combat theme) - original soundtrack.mp3`, but the generated C previously looped at ~156 seconds because notes outside clipped playlist items were still emitted.

### Completed

- **Playlist clip boundaries** — done 2026-05-04. `tools/flp2c.py` now ignores notes that begin outside the visible playlist clip, shortens notes that cross the clip end, and pads/trims each generated voice to the arrangement end. Regenerated `src/music/*.c`; generated loop lengths now match FLP arrangement lengths.
- **Converter verification harness** — done 2026-05-04. `make verify_music` now checks each FLP against the generated C loop duration and expected voice count, reports playlist clipping counts, and prints mapped MP3 reference durations where available.
- **Karplus-Strong for piano/keys** — done 2026-05-04. `tools/flp2c.py` now maps `piano`, `keys`, and `grand` channels to wave 5. `src/core/audio.c` increased `KS_BUFMAX` from 170 to 1024 so low FL Keys notes remain in tune. Regenerated `src/music/*.c`; FL Keys lanes now use `wave=5`.
- **Square duty cycle variety** — done 2026-05-04. `TrackDef` now carries a per-track `duty` field, `audioPlaySong()` copies it into each square voice, and `tools/flp2c.py` emits common NES pulse widths from channel-name heuristics (`#3` -> 25%, `#4` -> 12.5%, plain -> 50%). Regenerated `src/music/*.c`; square lanes now include duty metadata.
- **Simple reverb** — done 2026-05-04. `SongDef` now carries a song-level `reverb` amount, `audio.c` applies a lightweight two-comb/allpass reverb in the mixer, and `tools/flp2c.py` emits conservative per-song wet amounts. Regenerated `src/music/*.c`; cave/menu/ending songs now have more space while combat stays mostly dry.

### Recommended Next Checks

- Add optional WAV rendering for generated songs so we can compare against the MP3 references by ear and with waveform/spectrogram tools.
- Add a small manual override table for per-song/per-channel settings that PyFLP does not expose reliably: waveform, square duty, ADSR preset, gain trim, reverb send, bitcrush/sample flags, and loop metadata.

---

## What We Have Now

- **Waveforms**: triangle (4-bit NES quantized), square (hard-switching), sawtooth, noise (15-bit LFSR), sine (Bhaskara I approx), Karplus-Strong for piano/key channels
- **Polyphony**: full — simultaneous notes split into separate lanes, every note captured
- **ADSR**: per-wave presets tuned for NES character
- **Velocity**: per-note amplitude from `note.velocity` (0–127 → 0.0–1.0)
- **Fine pitch**: per-note cents offset from `note.pitch` applied to frequency
- **Per-channel volume**: FL Studio channel volume normalized and stored as `TrackDef.gain`
- **Square duty**: per-track duty cycle stored as `TrackDef.duty` and applied to square voices
- **Reverb**: song-level wet amount stored as `SongDef.reverb` and applied in the mixer
- **Playlist clipping**: notes outside visible playlist clips are skipped, notes crossing clip ends are shortened
- **Voice sync**: all lanes are padded/trimmed to the FLP arrangement end so they loop together

---

## Instrument Fidelity Breakdown

| Instrument | Songs | Current wave | Fidelity |
|---|---|---|---|
| NES VST Square | All 8 | square with per-track duty | ~85% |
| NES VST Triangle | Most | 4-bit quantized triangle | ~85% |
| NES VST Noise | Several | 15-bit LFSR | ~65% |
| FL Keys (piano) | 4 songs | Karplus-Strong | ~70% |
| Grand Piano | 2 songs | Karplus-Strong when channel name is detected | ~70% |
| 3x Osc | eternal_town | sawtooth (one osc only) | ~30% |
| Purity (wavetable) | eternal_cave | triangle fallback | ~25% |

Effects: simple song-level reverb exists; kHs Bitcrush, Serum FX, FL mixer EQ, and compression are still absent.

---

## Roadmap to 95% Fidelity

### 0. Converter verification harness — completed 2026-05-04
- **What**: Add a repeatable validation command that checks each FLP against its generated C duration and the matching MP3 export. This should catch future arrangement/loop regressions before we start subjective audio tuning.
- **Files**: `tools/flp2c.py` or a small companion script under `tools/`.
- **Impact**: Keeps generated music structurally aligned with the references.

### 1. Karplus-Strong for piano — completed 2026-05-04
- **What**: Wave=5 (KS) already exists in audio.c but `_guess_wave()` in flp2c.py never picks it for piano channels — it assigns wave=2 (sawtooth). Change `_guess_wave` to return 5 for 'piano', 'keys', 'grand'.
- **Files**: `tools/flp2c.py`, `src/core/audio.c`. Re-run converter.
- **Impact**: FL Keys and Grand Piano go from ~40% to ~70%+.

### 2. Duty cycle variety for NES square — completed 2026-05-04
- **What**: NES APU has 4 duty cycles: 12.5%, 25%, 50%, 75%. Each sounds distinct. The `Voice.duty` field already exists in audio.c. Add a `duty` field to `TrackDef` and encode it. The converter can't read the NES VST plugin parameter directly from PyFLP, but we can assign a default per-channel heuristic or make it manually tunable per song.
- **Files**: `src/core/audio.h` (add duty to TrackDef), `src/core/audio.c` (copy duty from TrackDef to Voice in audioPlaySong), `tools/flp2c.py` (emit duty in TrackDef, default 0.5).
- **Impact**: Square wave goes from ~75% to ~85%.

### 3. Simple reverb — completed 2026-05-04
- **What**: A pair of comb filters + allpass (~20 lines). Apply as a post-process on the mixed buffer in `fill()`. Add a per-song reverb wet/dry amount. Spatial depth for eternal_cave and ambient songs improves massively.
- **Files**: `src/core/audio.h`, `src/core/audio.c`, `tools/flp2c.py`, `src/music/*.c`.
- **Impact**: Ambient songs (eternal_cave, hopes_and_dreams) go from flat/dry to spacious. ~5–8% overall fidelity improvement.

### 4. Note slides / portamento
- **What**: FL Studio slide notes are marked in PyFLP. Detect them in `_build_lanes` and emit a slide duration. In the engine, linearly interpolate `Voice.ph` frequency between the previous note's end freq and the new note's target freq over N samples.
- **Files**: `tools/flp2c.py` (detect slide flag, emit slide info), `src/core/audio.h` (add slide field to Note or encode in f), `src/core/audio.c` (frequency interpolation in voice_tick).
- **Impact**: Glide-heavy channels sound dramatically more accurate. Moderate overall impact since not all songs use slides.

### 5. Bitcrusher effect for marked channels
- **What**: eternal_town and eternal_gdr use kHs Bitcrush. Add a per-track `crushed` flag to `TrackDef`. In the engine, apply 4-bit quantization to that voice's output: `s = floorf(s * 8.0f) / 8.0f`. The converter detects "bitcrush" in channel names or plugin names and sets the flag.
- **Files**: `src/core/audio.h` (add crushed to TrackDef), `src/core/audio.c` (quantize in fill loop), `tools/flp2c.py` (detect and emit flag).
- **Impact**: eternal_town and eternal_gdr sound significantly more accurate.

---

## Roadmap to 100% Fidelity

These are expensive or require fundamentally new subsystems:

### 6. Sample rate 22050 → 44100 Hz
- **What**: Change `#define SR 22050` in audio.c. Change WAVEFORMATEX. Re-run flp2c.py (all durations recalculate automatically). `KS_BUFMAX` should double from 1024 to 2048 to preserve the current low-note Karplus-Strong range.
- **Cost**: Double buffer memory, slightly more CPU. All generated .c files double in note duration values.
- **Impact**: High-frequency notes stop aliasing. ~5% fidelity improvement.

### 7. polyBLEP anti-aliasing
- **What**: Add band-limited step correction to square and sawtooth transitions. Eliminates the metallic aliasing on upper-register melodies. Pure math, no lookup tables.
- **Files**: `src/core/audio.c` (modify wave=1 and wave=2 synthesis branches).
- **Impact**: Upper-register NES notes sound cleaner. ~3% fidelity improvement.

### 8. Additive synthesis for 3x Osc
- **What**: Add a new wave type (e.g., wave=6) that sums three detuned oscillators. FL Studio's 3x Osc in eternal_town has configurable ratios and detune — approximate with three square waves at 1x, 1x+detune, 2x freq.
- **Files**: `src/core/audio.c` (new wave branch), `tools/flp2c.py` (assign wave=6 to 3x Osc channels).
- **Impact**: eternal_town 3x Osc goes from ~30% to ~60%.

### 9. Full effects chain
- **What**: Reverb, EQ, compression, delay, Serum FX. These are each significant subsystems. Reverb (item 3 above) is the most feasible. Full EQ and compression approach audio engine territory.
- **Impact**: The remaining gap between ~90% and 100%.

### 10. NES noise period table
- **What**: Real NES noise has 16 fixed periods (not arbitrary pitch). Map note pitches to the nearest NES noise period. Currently we drive the LFSR at arbitrary Hz from the note frequency.
- **Files**: `src/core/audio.c` (add period lookup table for wave=3).
- **Impact**: Percussion sounds more authentically NES. ~2–3% improvement.

---

## Quick Reference: How to Regenerate Music

```
python3 tools/flp2c.py          # convert all assets/music/*.flp
python3 tools/flp2c.py path.flp # convert one file
make flp2c                       # same as first line via Makefile
make verify_music                # validate FLP -> generated C timing
```

Output goes to `src/music/`. Always rebuild after regenerating.

## Song → Game State Mapping (in main.c)

| Game state | Song |
|---|---|
| STATE_MAIN_MENU | hopes_and_dreams_eternal_night_ost |
| STATE_WORLD | eternal_test |
| STATE_TOWN | eternal_town |
| STATE_DUNGEON | eternal_cave |
| STATE_COMBAT | shining_star_eternal_night_ost |
| STATE_DEATH | over |
| (other states) | audioStop() |
