#!/usr/bin/env python3
#
# MUSX ultra-compressor v8
#
# pack:   python3 c2bin.py pack   input.c    output.musx
# unpack: python3 c2bin.py unpack input.musx output.c
#
# Format v8 changes vs v7:
#   - stores pad / crushed / duty / detune / reverb / song name (all were dropped)
#   - per-track palette + bit-packed indices for freq, dur, vel
#     (1/2/4/8 bits per index depending on palette size; saves ~2x on typical tracks)
#   - single interleaved bit-stream per track (no per-field byte-alignment waste)

import sys, re, struct
from collections import OrderedDict

MAGIC   = b"MUSX"
VERSION = 8

# ─── regexes ───────────────────────────────────────────────────────────────

ARRAY_RE = re.compile(
    r"static\s+const\s+Note\s+([A-Za-z0-9_]+)\[\]\s*=\s*\{(.*?)\};", re.S)
NOTE_RE  = re.compile(r"\{\s*([-0-9.]+)f?\s*,\s*([0-9]+)\s*,\s*([-0-9.]+)f?\s*\}")
SONG_RE  = re.compile(r"const\s+SongDef\s+([A-Za-z0-9_]+)\s*=\s*\{(.*?)\};", re.S)
# 8-field: array, n, wave, gain, duty, pad, detune, crushed
TRACK_RE = re.compile(
    r"\{\s*([A-Za-z0-9_]+)\s*,"    # array name
    r"\s*([0-9]+)\s*,"              # n
    r"\s*([0-9]+)\s*,"              # wave
    r"\s*([-0-9.]+)f?\s*,"         # gain
    r"\s*([-0-9.]+)f?\s*,"         # duty
    r"\s*([0-9]+)\s*,"              # pad
    r"\s*([-0-9.]+)f?\s*,"         # detune
    r"\s*([0-9]+)\s*\}"             # crushed
)
# 6-field: array, n, wave, gain, duty, crushed  (no pad, no detune)
TRACK_RE_6 = re.compile(
    r"\{\s*([A-Za-z0-9_]+)\s*,"    # array name
    r"\s*([0-9]+)\s*,"              # n
    r"\s*([0-9]+)\s*,"              # wave
    r"\s*([-0-9.]+)f?\s*,"         # gain
    r"\s*([-0-9.]+)f?\s*,"         # duty
    r"\s*([0-9]+)\s*\}"             # crushed
)

# ─── quantisation ──────────────────────────────────────────────────────────

def qbyte(f):  return min(255, max(0, int(round(f * 255))))
def dqbyte(v): return v / 255.0
def qfreq(f):  return int(round(f * 100))   # max ~93300 for 933 Hz — fits uint32
def dqfreq(v): return v / 100.0

# ─── varint ────────────────────────────────────────────────────────────────

def write_varint(v):
    assert v >= 0, v
    buf = bytearray()
    while True:
        b = v & 0x7F; v >>= 7
        buf.append(b | (0x80 if v else 0))
        if not v: break
    return bytes(buf)

def read_varint(data, pos):
    val = shift = 0
    while True:
        b = data[pos]; pos += 1
        val |= (b & 0x7F) << shift
        if not (b & 0x80): break
        shift += 7
    return val, pos

# ─── bit-stream ─────────────────────────────────────────────────────────────
# Width is derived from palette size so it never needs to be stored.
#   palette size → index bits needed
#   0-1 → 0   (always index 0, write nothing)
#   2   → 1
#   3-4 → 2
#   5-16 → 4
#   17-256 → 8

def _bits_for(n):
    if n <= 1:  return 0
    if n == 2:  return 1
    if n <= 4:  return 2
    if n <= 16: return 4
    return 8

class BitWriter:
    def __init__(self):
        self.buf = bytearray()
        self.acc = self.filled = 0

    def write(self, val, width):
        if not width: return
        self.acc |= (val & ((1 << width) - 1)) << self.filled
        self.filled += width
        while self.filled >= 8:
            self.buf.append(self.acc & 0xFF)
            self.acc >>= 8; self.filled -= 8

    def flush(self):
        if self.filled:
            self.buf.append(self.acc & 0xFF)
        return bytes(self.buf)

class BitReader:
    def __init__(self, data, pos):
        self.data = data; self.pos = pos
        self.acc = self.avail = 0

    def read(self, width):
        if not width: return 0
        while self.avail < width:
            self.acc |= self.data[self.pos] << self.avail
            self.pos += 1; self.avail += 8
        val = self.acc & ((1 << width) - 1)
        self.acc >>= width; self.avail -= width
        return val

    def byte_pos(self):
        self.acc = self.avail = 0
        return self.pos

# ─── parse C ───────────────────────────────────────────────────────────────

def parse_c_file(text):
    arrays = OrderedDict()
    for m in ARRAY_RE.finditer(text):
        arrays[m.group(1)] = [
            (float(n.group(1)), int(n.group(2)), float(n.group(3)))
            for n in NOTE_RE.finditer(m.group(2))
        ]
    if not arrays:
        raise RuntimeError("no note arrays found")

    sm = SONG_RE.search(text)
    if not sm:
        raise RuntimeError("SongDef not found")
    body = sm.group(2)

    last = body.rfind('}')
    rm = re.search(r'([-0-9.]+)f?', body[last + 1:])
    reverb = float(rm.group(1)) if rm else 0.0

    tracks = []
    for t in TRACK_RE.finditer(body):
        tracks.append({
            "array":   t.group(1),
            "wave":    int(t.group(3)),
            "gain":    float(t.group(4)),
            "duty":    float(t.group(5)),
            "pad":     int(t.group(6)),
            "detune":  float(t.group(7)),
            "crushed": int(t.group(8)),
        })
    if not tracks:
        for t in TRACK_RE_6.finditer(body):
            tracks.append({
                "array":   t.group(1),
                "wave":    int(t.group(3)),
                "gain":    float(t.group(4)),
                "duty":    float(t.group(5)),
                "pad":     0,
                "detune":  0.0,
                "crushed": int(t.group(6)),
            })
    if not tracks:
        raise RuntimeError("no tracks found")

    return sm.group(1), arrays, tracks, reverb

# ─── note encoding ─────────────────────────────────────────────────────────

def encode_notes(notes):
    out = bytearray()
    out += write_varint(len(notes))
    if not notes:
        return bytes(out)

    # Build insertion-order palettes (preserves round-trip stability)
    freq_pal = list(dict.fromkeys(qfreq(f) for f, d, v in notes))
    dur_pal  = list(dict.fromkeys(d         for f, d, v in notes))
    vel_pal  = list(dict.fromkeys(qbyte(v)  for f, d, v in notes))

    assert len(freq_pal) <= 255 and len(dur_pal) <= 255 and len(vel_pal) <= 255

    fi = {fq: i for i, fq in enumerate(freq_pal)}
    di = {d:  i for i, d  in enumerate(dur_pal)}
    vi = {vq: i for i, vq in enumerate(vel_pal)}

    fw = _bits_for(len(freq_pal))
    dw = _bits_for(len(dur_pal))
    vw = _bits_for(len(vel_pal))

    # Freq palette: count (1 byte) + uint32 LE per entry (freq*100 can exceed uint16)
    out += bytes([len(freq_pal)])
    for fq in freq_pal:
        out += struct.pack('<I', fq)

    # Dur palette: count (1 byte) + varint per entry (durations can be large)
    out += bytes([len(dur_pal)])
    for d in dur_pal:
        out += write_varint(d)

    # Vel palette: count (1 byte) + uint8 per entry
    out += bytes([len(vel_pal)])
    out += bytes(vel_pal)

    # Interleaved bit-stream: fw + dw + vw bits per note, one stream for the track
    bw = BitWriter()
    for f, d, v in notes:
        bw.write(fi[qfreq(f)], fw)
        bw.write(di[d],        dw)
        bw.write(vi[qbyte(v)], vw)
    out += bw.flush()

    return bytes(out)

def decode_notes(data, pos):
    count, pos = read_varint(data, pos)
    if count == 0:
        return [], pos

    nf = data[pos]; pos += 1
    freq_pal = [struct.unpack_from('<I', data, pos + i * 4)[0] for i in range(nf)]
    pos += nf * 4

    nd = data[pos]; pos += 1
    dur_pal = []
    for _ in range(nd):
        d, pos = read_varint(data, pos)
        dur_pal.append(d)

    nv = data[pos]; pos += 1
    vel_pal = list(data[pos:pos + nv]); pos += nv

    fw = _bits_for(nf)
    dw = _bits_for(nd)
    vw = _bits_for(nv)

    br = BitReader(data, pos)
    notes = []
    for _ in range(count):
        notes.append((
            dqfreq(freq_pal[br.read(fw)]),
            dur_pal[br.read(dw)],
            dqbyte(vel_pal[br.read(vw)]),
        ))
    pos = br.byte_pos()

    return notes, pos

# ─── pack ──────────────────────────────────────────────────────────────────

def pack(src, dst):
    text   = open(src, encoding="utf8").read()
    song_name, arrays, tracks, reverb = parse_c_file(text)

    blob = bytearray()
    blob += MAGIC
    blob += bytes([VERSION])
    blob += bytes([qbyte(reverb)])
    name_enc = song_name.encode()
    blob += bytes([len(name_enc)]) + name_enc
    blob += write_varint(len(tracks))

    for tr in tracks:
        blob += bytes([tr["wave"]])
        blob += bytes([(tr["pad"] & 1) | ((tr["crushed"] & 1) << 1)])
        blob += bytes([qbyte(tr["gain"])])
        blob += bytes([qbyte(tr["duty"])])
        blob += bytes([min(255, int(round(tr["detune"] * 10)))])
        blob += encode_notes(arrays[tr["array"]])

    open(dst, "wb").write(blob)

    total_notes = sum(len(arrays[tr["array"]]) for tr in tracks)
    naive = total_notes * (4 + 4 + 4)  # three floats per Note in C
    print(f"packed:   {len(tracks)} tracks, {total_notes} notes")
    print(f"size:     {len(blob)} bytes  (vs ~{naive} bytes raw)")
    print(f"ratio:    {naive / len(blob):.1f}x")

# ─── unpack ────────────────────────────────────────────────────────────────

def unpack(src, dst):
    data = open(src, "rb").read()

    if data[:4] != MAGIC:
        raise RuntimeError("bad magic")
    if data[4] != VERSION:
        raise RuntimeError(f"version {data[4]} != {VERSION}")

    pos = 5
    reverb = dqbyte(data[pos]); pos += 1
    nlen = data[pos]; pos += 1
    song_name = data[pos:pos + nlen].decode(); pos += nlen
    ntracks, pos = read_varint(data, pos)

    tracks = []
    for _ in range(ntracks):
        wave    = data[pos]; pos += 1
        flags   = data[pos]; pos += 1
        pad     = flags & 1
        crushed = (flags >> 1) & 1
        gain    = dqbyte(data[pos]); pos += 1
        duty    = dqbyte(data[pos]); pos += 1
        detune  = data[pos] / 10.0;  pos += 1
        notes, pos = decode_notes(data, pos)
        tracks.append(dict(wave=wave, pad=pad, crushed=crushed,
                           gain=gain, duty=duty, detune=detune, notes=notes))

    out = ['#include "../core/audio.h"\n']
    for i, tr in enumerate(tracks):
        out.append(f"static const Note track_{i}[] = {{")
        for f, d, v in tr["notes"]:
            out.append(f"    {{{f:.3f}f,{d},{v:.4f}f}},")
        out.append("};\n")

    out.append(f"const SongDef {song_name} = {{")
    out.append(f"    {ntracks},")
    out.append("    {")
    for i, tr in enumerate(tracks):
        out.append(
            f'        {{ track_{i}, {len(tr["notes"])}, {tr["wave"]}, '
            f'{tr["gain"]:.4f}f, {tr["duty"]:.4f}f, {tr["pad"]}, '
            f'{tr["detune"]:.1f}f, {tr["crushed"]} }},'
        )
    out.append("    },")
    out.append(f"    {reverb:.4f}f")
    out.append("};")

    open(dst, "w").write("\n".join(out))
    print(f"unpacked: {ntracks} tracks")

# ─── main ──────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("usage: pack input.c out.musx | unpack input.musx out.c")
        sys.exit(1)
    cmd = sys.argv[1]
    if cmd == "pack":
        pack(sys.argv[2], sys.argv[3])
    elif cmd == "unpack":
        unpack(sys.argv[2], sys.argv[3])
    else:
        print("unknown command"); sys.exit(1)
