#!/usr/bin/env python3
"""Verify TinyHero FLP -> C music conversion timing.

Checks each assets/music/*.flp against the generated src/music/*.c file:
  - FL Studio arrangement duration
  - generated C loop duration
  - expected/generated voice count
  - playlist clipping counts
  - optional MP3 reference duration when a known export exists
"""

import glob
import os
import re
import subprocess
import sys

import pyflp
from pyflp.arrangement import PatternPLItem

import flp2c

MUSIC_DIR = "assets/music"
OUT_DIR = "src/music"
DURATION_EPS = 0.03
MP3_EPS = 2.0

REF_MP3 = {
    "hopes_and_dreams_eternal_night_ost": "Hopes and Dreams - Eternal Night ost.mp3",
    "shining_star_eternal_night_ost": "Target Detected (Combat theme) - original soundtrack.mp3",
    "eternal_gdr": "Gilles is Here ! - original soundtrack.mp3",
    "eternal_test_ending": "Daring to Hope again (End credits theme) - original soundtra.mp3",
    "over": "The star has fallen (Game Over) - original sountrack.mp3",
}


def _format_seconds(value):
    return f"{value:7.2f}s" if value is not None else "      -"


def _mp3_duration(path):
    try:
        result = subprocess.run(
            [
                "ffprobe",
                "-v", "error",
                "-show_entries", "format=duration",
                "-of", "default=nw=1:nk=1",
                path,
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return float(result.stdout.strip())
    except (FileNotFoundError, subprocess.SubprocessError, ValueError):
        return None


def _arrangement_data(flp_path):
    project = pyflp.parse(flp_path)
    arrangement = list(project.arrangements)[0]

    clips = []
    for track in arrangement.tracks:
        for item in track:
            if isinstance(item, PatternPLItem):
                clips.append(item)

    end_tick = max((clip.position + clip.length for clip in clips), default=0)
    seconds = end_tick * (60.0 / (project.tempo * project.ppq))

    rack_channels = sorted({
        note.rack_channel
        for clip in clips
        for note in clip.pattern.notes
    })

    expected_voices = 0
    clipped = 0
    skipped = 0
    for rack_ch in rack_channels:
        timelines, ch_clipped, ch_skipped = flp2c._build_lanes(
            clips, rack_ch, project.ppq, project.tempo, end_tick
        )
        expected_voices += sum(1 for timeline in timelines if timeline)
        clipped += ch_clipped
        skipped += ch_skipped

    return {
        "seconds": seconds,
        "voices": min(expected_voices, flp2c.SONG_MAX_TRACKS),
        "clipped": clipped,
        "skipped": skipped,
    }


def _generated_data(c_path):
    if not os.path.exists(c_path):
        return None

    text = open(c_path, encoding="utf-8").read()
    match = re.search(r"const SongDef\s+\w+\s*=\s*\{\s*(\d+)\s*,", text, re.S)
    tracks = int(match.group(1)) if match else None

    max_samples = 0
    for array in re.finditer(r"static const Note\s+\w+\[\]\s*=\s*\{(.*?)\};", text, re.S):
        samples = 0
        for duration in re.findall(r"\{[^,]+,\s*(\d+)\s*,", array.group(1)):
            samples += int(duration)
        max_samples = max(max_samples, samples)

    return {
        "seconds": max_samples / flp2c.SR,
        "tracks": tracks,
    }


def main():
    flp_paths = sorted(glob.glob(os.path.join(MUSIC_DIR, "*.flp")))
    if not flp_paths:
        print("No FLP files found.")
        return 1

    errors = []
    warnings = []

    print("song                                      flp       gen       voices  clip  skip  mp3")
    print("-" * 92)

    for flp_path in flp_paths:
        stem = flp2c._c_name(os.path.splitext(os.path.basename(flp_path))[0])
        c_path = os.path.join(OUT_DIR, f"{stem}.c")

        arr = _arrangement_data(flp_path)
        gen = _generated_data(c_path)
        if gen is None:
            errors.append(f"{stem}: missing generated file {c_path}")
            continue

        flp_seconds = arr["seconds"]
        gen_seconds = gen["seconds"]
        diff = abs(flp_seconds - gen_seconds)
        voice_text = f"{gen['tracks']:2d}/{arr['voices']:2d}" if gen["tracks"] is not None else " ?/??"

        mp3_text = "-"
        ref_name = REF_MP3.get(stem)
        if ref_name:
            mp3_path = os.path.join(MUSIC_DIR, ref_name)
            mp3_seconds = _mp3_duration(mp3_path)
            if mp3_seconds is None:
                warnings.append(f"{stem}: could not read MP3 reference {ref_name}")
                mp3_text = f"{ref_name} ?"
            else:
                mp3_diff = abs(flp_seconds - mp3_seconds)
                mp3_text = f"{_format_seconds(mp3_seconds)} {ref_name}"
                if mp3_diff > MP3_EPS:
                    warnings.append(
                        f"{stem}: FLP/MP3 duration differs by {mp3_diff:.2f}s ({ref_name})"
                    )

        print(
            f"{stem[:40]:40} "
            f"{_format_seconds(flp_seconds)} "
            f"{_format_seconds(gen_seconds)} "
            f"{voice_text:>7} "
            f"{arr['clipped']:5d} "
            f"{arr['skipped']:5d} "
            f"{mp3_text}"
        )

        if diff > DURATION_EPS:
            errors.append(f"{stem}: FLP/generated duration differs by {diff:.2f}s")
        if gen["tracks"] != arr["voices"]:
            errors.append(f"{stem}: generated voices {gen['tracks']} != expected {arr['voices']}")

    if warnings:
        print("\nWarnings:")
        for warning in warnings:
            print(f"  {warning}")

    if errors:
        print("\nErrors:")
        for error in errors:
            print(f"  {error}")
        return 1

    print("\nOK: generated song timing matches FLP arrangements.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
