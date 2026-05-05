#!/usr/bin/env python3
"""FLP -> C converter for TinyHero audio.c.

Usage:
    python3 tools/flp2c.py                  # convert all assets/music/*.flp
    python3 tools/flp2c.py path/to/song.flp # convert one file

Outputs one .c file per FLP into src/music/, each containing Note arrays and
a SongDef struct that audio.c can play with audioPlaySong().
"""

import sys, os, re, glob, math
import pyflp
from pyflp.arrangement import PatternPLItem

SR = 22050
SONG_MAX_TRACKS = 24   # must match audio.h

_NOTE_NAMES = ("C","C#","D","D#","E","F","F#","G","G#","A","A#","B")

def _key_to_raw(key_str):
    """Parse PyFLP key string (e.g. 'C4', 'F#5') to raw note index 0-131.
    PyFLP convention: C0=0, A5=69 (=440 Hz)."""
    if len(key_str) >= 3 and key_str[1] == '#':
        note, octave = key_str[:2], int(key_str[2:])
    else:
        note, octave = key_str[0], int(key_str[1:])
    return _NOTE_NAMES.index(note) + octave * 12

def _raw_to_freq(raw, cents=0.0):
    """Raw note index + fine pitch in cents to Hz. A5(69)=440 Hz."""
    return 440.0 * (2.0 ** ((raw - 69.0 + cents / 100.0) / 12.0))

def _ch_gain(ch):
    """Normalize PyFLP channel volume to a [0.0, 2.0] gain scalar."""
    try:
        v = float(getattr(ch, 'volume', 100) or 100)
        if v <= 0:    return 0.0
        if v <= 2.0:  return v           # already normalized
        if v <= 200:  return v / 100.0   # 0-100 or 0-200 percent scale
        return v / 10000.0               # FL internal 0-12800 scale
    except Exception:
        return 1.0

def _c_name(s):
    s = s.lower()
    s = re.sub(r'[^a-z0-9]+', '_', s)
    return s.strip('_')

def _guess_wave(ch_name, notes):
    """Heuristic: map channel name / note stats to wave type.
    0=triangle  1=square  3=noise  4=sine  5=Karplus-Strong"""
    n = (ch_name or '').lower()
    if 'noise' in n:                   return 3
    if 'triangle' in n:                return 0
    if 'square' in n or 'pulse' in n:  return 1
    if 'piano' in n or 'keys' in n or 'grand' in n: return 5
    if 'bass' in n:                    return 0
    if not notes:                      return 1

    raws    = [_key_to_raw(note.key) for note in notes]
    avg_len = sum(note.length for note in notes) / len(notes)
    p_range = (max(raws) - min(raws)) if len(raws) > 1 else 0

    # Short notes with narrow pitch range → percussive → noise
    if avg_len < 60 and p_range <= 5:  return 3
    # Fast melodic runs present (< 48 ticks ≈ 16th note at 96 PPQ) → square
    if sum(1 for note in notes if note.length < 48) >= 3:  return 1
    # Very long notes only → sustained pad → triangle
    if avg_len > 200:                  return 0
    # Default: square wave (NES pulse / melodic)
    return 1

def _guess_duty(ch_name, wave):
    """Heuristic square duty. NES VST channel params are not exposed reliably."""
    if wave != 1:
        return 0.5

    n = (ch_name or '').lower()
    if '#4' in n:
        return 0.125
    if '#3' in n:
        return 0.25
    if '#2' in n:
        return 0.75
    return 0.5

def _guess_crushed(ch_name, ch):
    """Detect kHs Bitcrush or similar bit-reduction on this channel."""
    n = (ch_name or '').lower()
    if 'bitcrush' in n or 'crush' in n or '8bit' in n or 'lofi' in n or 'lo-fi' in n:
        return 1
    try:
        plugin = getattr(ch, 'plugin', None)
        pname = (getattr(plugin, 'name', None) or '').lower()
        if 'bitcrush' in pname or 'crush' in pname:
            return 1
    except Exception:
        pass
    return 0

def _guess_reverb(song_name):
    """Song-level reverb amount. Kept conservative to preserve tiny synth clarity."""
    n = (song_name or '').lower()
    if 'cave' in n:
        return 0.24
    if 'hopes_and_dreams' in n or 'test_ending' in n:
        return 0.18
    if 'over' in n:
        return 0.16
    if 'town' in n:
        return 0.10
    if 'gdr' in n or 'test' in n:
        return 0.08
    if 'shining_star' in n:
        return 0.04
    return 0.08

def _fit_timeline(timeline, target_samples):
    """Pad or trim a timeline so it loops exactly at the playlist end."""
    total = sum(d for _, d, _ in timeline)
    if total < target_samples:
        timeline.append((0.0, target_samples - total, 1.0))
    elif total > target_samples:
        over = total - target_samples
        while timeline and over > 0:
            freq, dur, vel = timeline[-1]
            if dur > over:
                timeline[-1] = (freq, dur - over, vel)
                over = 0
            else:
                over -= dur
                timeline.pop()
    return timeline

def _build_lanes(all_clips, rack_ch, ppq, bpm, song_end_tick):
    """Return list of monophonic timelines (one per concurrent voice lane).
    Each timeline is a list of (freq_hz, dur_samples, velocity_0_1).
    Gaps become rests (freq=0, vel=1). Simultaneous notes spread across lanes."""
    spb = SR / (bpm / 60.0 * ppq)   # samples per tick
    target_samples = max(1, round(song_end_tick * spb))

    events = []  # (abs_tick, raw_note, length_ticks, cents, velocity, is_slide)
    clipped = 0
    skipped = 0
    for clip in all_clips:
        for note in clip.pattern.notes:
            if note.rack_channel != rack_ch:
                continue

            # Playlist clips can expose only part of a pattern. Ignore notes
            # outside the visible clip and shorten notes that cross the clip end.
            note_start = note.position
            note_end   = note.position + note.length
            clip_end   = clip.length
            if note_start >= clip_end:
                skipped += 1
                continue
            if note_end > clip_end:
                note_end = clip_end
                clipped += 1
            length = note_end - note_start
            if length <= 0:
                skipped += 1
                continue

            abs_pos  = clip.position + note_start
            cents    = float(getattr(note, 'pitch', 0) or 0)
            vel      = int(getattr(note, 'velocity', 100) or 100)
            is_slide = bool(getattr(note, 'slide', False))
            events.append((abs_pos, _key_to_raw(note.key), length, cents, vel, is_slide))

    if not events:
        return [], clipped, skipped

    # Sort by position; within same position highest pitch first (most audible lane 0)
    events.sort(key=lambda e: (e[0], -e[1]))

    # Assign each event to the earliest free lane (greedy voice allocation)
    lane_notes = []   # list of list of (pos, raw, length, cents, vel)
    lane_ends  = []   # tick at which each lane becomes free

    for pos, raw, length, cents, vel, is_slide in events:
        assigned = None
        for i, end in enumerate(lane_ends):
            if pos >= end:
                assigned = i
                break
        if assigned is None:
            assigned = len(lane_notes)
            lane_notes.append([])
            lane_ends.append(0)
        lane_notes[assigned].append((pos, raw, length, cents, vel, is_slide))
        lane_ends[assigned] = pos + length

    # Build a flat (freq, dur_samples, vel_norm) timeline for each lane
    timelines = []
    for lane in lane_notes:
        out = []
        prev_end = 0
        for pos, raw, length, cents, vel, is_slide in lane:
            if pos > prev_end:
                out.append((0.0, max(1, round((pos - prev_end) * spb)), 1.0))
            vel_norm = max(0.0, min(1.0, vel / 127.0))
            freq = _raw_to_freq(raw, cents)
            if is_slide:
                freq = -freq
            out.append((freq, max(1, round(length * spb)), vel_norm))
            prev_end = pos + length
        timelines.append(_fit_timeline(out, target_samples))

    return timelines, clipped, skipped

def convert(flp_path, out_path):
    print(f'  {os.path.basename(flp_path)}')
    p = pyflp.parse(flp_path)
    name = _c_name(os.path.splitext(os.path.basename(flp_path))[0])
    reverb = _guess_reverb(name)
    channels = list(p.channels)

    # Collect all pattern clips from the arrangement
    arr = list(p.arrangements)[0]
    all_clips = []
    for track in arr.tracks:
        for item in track:
            if isinstance(item, PatternPLItem):
                all_clips.append(item)
    song_end_tick = max((clip.position + clip.length for clip in all_clips), default=0)

    # Discover rack channels used
    rack_channels = sorted({
        note.rack_channel
        for clip in all_clips
        for note in clip.pattern.notes
    })

    voices = []  # (rc, lane_idx, ch_name, wave, gain, duty, crushed, timeline)
    total_clipped = 0
    total_skipped = 0
    for rc in rack_channels:
        ch = channels[rc] if rc < len(channels) else None
        ch_name = getattr(ch, 'name', None)
        ch_notes = [
            note
            for clip in all_clips
            for note in clip.pattern.notes
            if note.rack_channel == rc
        ]
        wave    = _guess_wave(ch_name, ch_notes)
        gain    = _ch_gain(ch) if ch is not None else 1.0
        duty    = _guess_duty(ch_name, wave)
        crushed = _guess_crushed(ch_name, ch)
        timelines, clipped, skipped = _build_lanes(all_clips, rc, p.ppq, p.tempo, song_end_tick)
        total_clipped += clipped
        total_skipped += skipped
        for lane_idx, timeline in enumerate(timelines):
            if timeline:
                voices.append((rc, lane_idx, ch_name or f'ch{rc}', wave, gain, duty, crushed, timeline))

    if not voices:
        print(f'    (no notes found, skipping)')
        return

    if len(voices) > SONG_MAX_TRACKS:
        print(f'    WARNING: {len(voices)} lanes exceeds SONG_MAX_TRACKS={SONG_MAX_TRACKS}, truncating')
        voices = voices[:SONG_MAX_TRACKS]

    # Voice sync: pad any rounding drift so all lanes loop at the same point.
    if len(voices) > 1:
        totals = [sum(d for _, d, _ in tl) for _, _, _, _, _, _, _, tl in voices]
        max_dur = max(totals)
        voices = [
            (rc, li, nm, wv, gn, du, cr, tl + [(0.0, max_dur - total, 1.0)] if total < max_dur else tl)
            for (rc, li, nm, wv, gn, du, cr, tl), total in zip(voices, totals)
        ]

    lines = [
        f'/* {os.path.basename(flp_path)} — auto-generated by tools/flp2c.py',
        f'   Tempo: {p.tempo} BPM  PPQ: {p.ppq}  SR: {SR} */',
        '#include "../core/audio.h"',
        '',
    ]

    arr_names = []
    for rc, lane_idx, ch_name, wave, gain, duty, crushed, timeline in voices:
        arr = f'_{name}_c{rc}_v{lane_idx}'
        arr_names.append((arr, wave, gain, duty, crushed, len(timeline)))
        crush_tag = ' crushed' if crushed else ''
        lines.append(f'/* ch{rc} v{lane_idx} "{ch_name}" wave={wave} gain={gain:.2f} duty={duty:.3f}{crush_tag} ({len(timeline)} notes) */')
        lines.append(f'static const Note {arr}[] = {{')
        for freq, dur, vel in timeline:
            if freq == 0.0:
                lines.append(f'    {{0.000f,{dur},1.0f}},')
            else:
                lines.append(f'    {{{freq:.3f}f,{dur},{vel:.4f}f}},')
        lines.append('};')
        lines.append('')

    lines.append(f'const SongDef song_{name} = {{')
    lines.append(f'    {len(arr_names)},')
    lines.append('    {')
    for arr, wave, gain, duty, crushed, n in arr_names:
        lines.append(f'        {{ {arr}, {n}, {wave}, {gain:.4f}f, {duty:.4f}f, {crushed} }},')
    lines.append('    },')
    lines.append(f'    {reverb:.4f}f')
    lines.append('};')

    with open(out_path, 'w') as f:
        f.write('\n'.join(lines) + '\n')

    total_notes = sum(len(v[7]) for v in voices)
    print(f'    -> {out_path}  ({len(voices)} voices, {total_notes} note entries)')
    if total_clipped or total_skipped:
        print(f'       playlist clipping: shortened {total_clipped}, skipped {total_skipped}')

def main():
    music_dir = 'assets/music'
    out_dir   = 'src/music'
    os.makedirs(out_dir, exist_ok=True)

    if len(sys.argv) > 1:
        paths = sys.argv[1:]
    else:
        paths = sorted(glob.glob(os.path.join(music_dir, '*.flp')))

    if not paths:
        print('No FLP files found.')
        sys.exit(1)

    print(f'Converting {len(paths)} FLP file(s) -> {out_dir}/')
    for path in paths:
        name = _c_name(os.path.splitext(os.path.basename(path))[0])
        convert(path, os.path.join(out_dir, f'{name}.c'))

if __name__ == '__main__':
    main()
