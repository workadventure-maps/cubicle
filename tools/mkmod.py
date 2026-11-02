"""Turn a piano score (MIDI) into an Impulse Tracker module that SNESMOD (PVSnesLib) can play.

Usage: python3 tools/mkmod.py assets/mountain_king.mid music/mountain_king.it

The .it file is committed: after hand edits in a tracker it is the source, so only rerun this to start over.
Rules from pvsneslib_snesmod.txt: instrument mode, 8 channels max (channel 8 stays free for sound effects),
loop lengths multiple of 16, linear slides, no old effects, echo set in the song message.
A row is a 16th note at speed 6, so the IT tempo equals the score's BPM. The module has no tempo or jump
commands: the game sets the tempo and picks the next pattern (src/main.c, update_music).
"""
import struct
import sys

import numpy as np

SPEED = 6
ROWS_PER_BEAT = 4
TICKS_PER_BEAT = SPEED * ROWS_PER_BEAT
ROWS_PER_BAR = 16
TEMPO = 138  # Alla marcia; the game changes it with the level
C5 = 261.6256  # IT note 60 = MIDI 60

# ---------------------------------------------------------------- MIDI


def vlq(d, i):
    v = 0
    while True:
        b = d[i]
        i += 1
        v = (v << 7) | (b & 0x7f)
        if b < 0x80:
            return v, i


def read_midi(path):
    """Returns ticks per beat and the notes as (start, end, pitch, velocity, track)."""
    d = open(path, 'rb').read()
    assert d[:4] == b'MThd'
    ntrk, div = struct.unpack('>HH', d[10:14])
    i = 8 + struct.unpack('>I', d[4:8])[0]
    notes = []
    for t in range(ntrk):
        assert d[i:i + 4] == b'MTrk'
        j, end = i + 8, i + 8 + struct.unpack('>I', d[i + 4:i + 8])[0]
        tick, status, on = 0, 0, {}
        while j < end:
            dt, j = vlq(d, j)
            tick += dt
            if d[j] & 0x80:
                status = d[j]
                j += 1
            if status == 0xff:
                ln, j = vlq(d, j + 1)
                j += ln
            elif status in (0xf0, 0xf7):
                ln, j = vlq(d, j)
                j += ln
            else:
                kind, a = status & 0xf0, d[j]
                b = d[j + 1] if kind not in (0xc0, 0xd0) else 0
                j += 1 if kind in (0xc0, 0xd0) else 2
                if kind == 0x90 and b:
                    on.setdefault(a, []).append((tick, b))
                elif (kind == 0x80 or kind == 0x90) and on.get(a):
                    s, v = on[a].pop(0)
                    notes.append((s, tick, a, v, t))
        i = end
    return div, sorted(notes)

# ---------------------------------------------------------------- samples (all synthesized)


def cycle(wave, n=32):
    w = wave(np.arange(n) / n)
    return w / np.abs(w).max()


def pluck(rate=16000, f=130.81, secs=0.6):
    """Karplus-Strong string: a pizzicato bass like Grieg's low strings."""
    n = int(round(rate / f - 0.5))
    noise = np.random.default_rng(46).uniform(-1, 1, n + 2)
    buf = list(np.convolve(noise, [0.25, 0.5, 0.25], 'same')[:n])  # softer attack than raw noise
    rho = 0.01 ** (1 / (secs * rate / (n + 0.5)))  # -40 dB over `secs`
    out = []
    for i in range(int(secs * rate)):
        y = buf[i % n]
        out.append(y)
        buf[i % n] = rho * 0.5 * (y + buf[(i + 1) % n])
    out = np.array(out)
    out[-len(out) // 8:] *= np.linspace(1, 0, len(out) // 8)
    return out / np.abs(out).max(), rate * C5 / (rate / (n + 0.5))


SINGLE = C5 * 32  # C5 speed of a 32-sample cycle
pizz, pizz_c5 = pluck()
SAMPLES = [  # name, data (-1..1), c5speed, looped, default volume
    ('pulse 25%', cycle(lambda x: np.where(x < 0.25, 1.0, -1.0)), SINGLE, True, 64),
    ('square', cycle(lambda x: np.where(x < 0.5, 1.0, -1.0)), SINGLE, True, 64),
    ('pizzicato', pizz, pizz_c5, False, 64),
    ('mellow', cycle(lambda x: sum(np.sin(2 * np.pi * k * x) / k ** 2 for k in (1, 3, 5, 7))), SINGLE, True, 64),
]

# name, sample (1-based), volume envelope [(tick, level)], sustain node, fadeout
INSTRUMENTS = [
    ('Lead', 1, [(0, 64), (2, 58), (10, 46), (14, 0)], 2, 512),
    ('Harmony', 2, [(0, 52), (8, 40), (12, 0)], 1, 512),
    ('Pizz bass', 3, [(0, 64), (1, 64), (4, 0)], 1, 512),
    ('Chords', 4, [(0, 56), (6, 44), (10, 0)], 1, 512),
]

# ---------------------------------------------------------------- arrangement

HAND_CHANNELS = {1: [0, 1, 2], 2: [3, 4, 5]}  # MIDI track -> channels (RH high first, LH low first)
CHANNEL_INSTRUMENT = [1, 2, 2, 3, 4, 4, 0, 0]
PAN = [32, 22, 42, 32, 24, 40, 32, 32]
CHVOL = [56, 40, 40, 64, 36, 36, 64, 64]
ECHO = 'edl 3\refb 40\revol 24 24\reon 1 2 3 5 6'


def volume(velocity):
    return max(1, min(64, round(28 + (velocity - 62) * 36 / 39)))


def arrange(notes, div):
    """Returns {(row, channel): [note, instrument, volume, command, param]} and the song length in rows."""
    cells = {}
    end_row = 0
    for track, chans in HAND_CHANNELS.items():
        busy = {c: 0 for c in chans}      # tick until which each channel holds a note
        offs = {c: [] for c in chans}     # (row, start row) of pending note-offs
        hand = sorted((round(s * TICKS_PER_BEAT / div), round(e * TICKS_PER_BEAT / div), p, v)
                      for s, e, p, v, t in notes if t == track)
        main = [n for n in hand if n[1] - n[0] >= 3]
        items = [n + (None, None) for n in main]
        runs = []
        for g in (n for n in hand if n[1] - n[0] < 3):  # grace notes, shorter than half a row
            if runs and g[0] - runs[-1][-1][0] <= 3:
                runs[-1].append(g)
            else:
                runs.append([g])
        for run in runs:  # play each run as an arpeggio (Jxy) on the row before the note it leads to
            target = min(n[0] for n in main if n[0] >= run[-1][0]) // SPEED
            steps = [g[2] - run[0][2] for g in run[1:3]] + [0, 0]
            assert all(0 <= s <= 15 for s in steps), run
            items.append(((target - 1) * SPEED, target * SPEED, run[0][2], run[0][3], 10, steps[0] * 16 + steps[1]))
        groups = {}
        for it in items:
            groups.setdefault(it[0] // SPEED, []).append(it)
        for row, grp in sorted(groups.items()):
            grp.sort(key=lambda g: -g[2] if track == 1 else g[2])
            if len(grp) > len(chans):  # drop octave doublings first
                seen, keep = set(), []
                for g in grp:
                    if g[2] % 12 not in seen:
                        keep.append(g)
                    seen.add(g[2] % 12)
                grp = keep[:len(chans)]
            for i, (s, e, p, v, cmd, prm) in enumerate(grp):
                order = [chans[i]] + [c for c in chans if c != chans[i]]
                free = [c for c in order if busy[c] <= s and (row, c) not in cells]
                ch = free[0] if free else order[0]
                if cmd is None and s % SPEED:
                    cmd, prm = 19, 0xD0 | (s % SPEED)  # SDx note delay (triplets)
                cells[(row, ch)] = [p, CHANNEL_INSTRUMENT[ch], volume(v), cmd, prm]
                busy[ch] = e
                offs[ch].append((max(row + 1, round(e / SPEED)), row))
                end_row = max(end_row, row + 1)
        for ch, lst in offs.items():
            for off, start in lst:
                nxt = min((r for (r, c) in cells if c == ch and r > start), default=1 << 30)
                if off < nxt and (off, ch) not in cells:
                    cells[(off, ch)] = [255, None, None, None, None]
    return cells, end_row

# ---------------------------------------------------------------- Impulse Tracker writer


def it_instrument(name, sample, env, sus, fadeout):
    b = bytearray(554)
    b[0:4] = b'IMPI'
    struct.pack_into('<H', b, 0x14, fadeout)
    b[0x17], b[0x18], b[0x19] = 60, 128, 128 | 32
    b[0x20:0x3A] = name.encode()[:25].ljust(26, b'\0')
    for n in range(120):
        b[0x40 + 2 * n], b[0x41 + 2 * n] = n, sample
    o = 0x130
    b[o], b[o + 1], b[o + 4], b[o + 5] = 1 | 4, len(env), sus, sus
    for i, (t, v) in enumerate(env):
        b[o + 6 + 3 * i] = v
        struct.pack_into('<H', b, o + 7 + 3 * i, t)
    return bytes(b)


def it_sample(name, n, c5, looped, vol, ptr):
    b = bytearray(80)
    b[0:4] = b'IMPS'
    b[0x11], b[0x12], b[0x13] = 64, 1 | 2 | (0x10 if looped else 0), vol
    b[0x14:0x2E] = name.encode()[:25].ljust(26, b'\0')
    b[0x2E], b[0x2F] = 1, 32
    struct.pack_into('<7I', b, 0x30, n, 0, n if looped else 0, round(c5), 0, 0, ptr)
    return bytes(b)


def it_pattern(rows):
    out = bytearray()
    for row in rows:
        for ch in sorted(row):
            note, ins, vol, cmd, prm = row[ch]
            mask = (note is not None) | (ins is not None) << 1 | (vol is not None) << 2 | (cmd is not None) << 3
            out += bytes([(ch + 1) | 0x80, mask])
            out += bytes(x for x in (note, ins, vol) if x is not None)
            if cmd is not None:
                out += bytes([cmd, prm])
        out.append(0)
    return struct.pack('<HH4x', len(out), len(rows)) + bytes(out)


def write_it(path, title, message, patterns):
    pcm = [(np.round(d * 0.7 * 32767).astype('<i2').tobytes(), c5, lp, vol, nm) for nm, d, c5, lp, vol in SAMPLES]
    ins = [it_instrument(*i) for i in INSTRUMENTS]
    pats = [it_pattern(p) for p in patterns]
    orders = bytes(range(len(pats))) + b'\xff'
    msg = message.encode() + b'\0'
    head = 0xC0 + len(orders) + 4 * (len(ins) + len(pcm) + len(pats))
    pos = head + len(msg)
    ins_off = []
    for i in ins:
        ins_off.append(pos)
        pos += len(i)
    smp_off = list(range(pos, pos + 80 * len(pcm), 80))
    pos += 80 * len(pcm)
    pat_off = []
    for p in pats:
        pat_off.append(pos)
        pos += len(p)
    smp_hdr = []
    for data, c5, lp, vol, nm in pcm:
        smp_hdr.append(it_sample(nm, len(data) // 2, c5, lp, vol, pos))
        pos += len(data)
    h = bytearray(0xC0)
    h[0:4] = b'IMPM'
    h[4:30] = title.encode()[:25].ljust(26, b'\0')
    struct.pack_into('<BB6HH', h, 0x1E, 4, 16, len(orders), len(ins), len(pcm), len(pats), 0x0214, 0x0214, 0x0D)
    struct.pack_into('<H6BHI', h, 0x2E, 1, 128, 48, SPEED, TEMPO, 128, 0, len(msg), head)
    h[0x40:0x80] = bytes(PAN) + bytes([160] * 56)
    h[0x80:0xC0] = bytes(CHVOL) + bytes([64] * 56)
    out = bytes(h) + orders + struct.pack(f'<{len(ins_off + smp_off + pat_off)}I', *ins_off, *smp_off, *pat_off)
    out += msg + b''.join(ins) + b''.join(smp_hdr) + b''.join(pats) + b''.join(d for d, *_ in pcm)
    assert len(out) == pos
    open(path, 'wb').write(out)


def main(src, dst):
    div, notes = read_midi(src)
    cells, rows = arrange(notes, div)
    rows = -(-rows // ROWS_PER_BAR) * ROWS_PER_BAR
    grid = [dict() for _ in range(rows)]
    for (r, ch), cell in cells.items():
        if r < rows:
            grid[r][ch] = cell
    # pattern 0: the opening bar (fermata), then 4 bars per pattern
    firsts = [0] + list(range(ROWS_PER_BAR, rows, 64))
    # The game plays patterns in any order, so each one starts every channel with a note or a note-off: a note
    # lasting to the end of a pattern gets its note-off on the next pattern's first row (none crosses a boundary).
    for r in firsts:
        for ch in sum(HAND_CHANNELS.values(), []):
            grid[r].setdefault(ch, [255, None, None, None, None])
    patterns = [grid[i:i + 64 if i else ROWS_PER_BAR] for i in firsts]
    message = ('In the Hall of the Mountain King\rEdvard Grieg, Peer Gynt Op. 46 No. 4\r'
               'Notes from the Mutopia Project score #1888 (public domain)\r\r[[SNESMOD]]\r' + ECHO + '\r')
    write_it(dst, 'Hall of the Mountain King', message, patterns)
    print(f'{dst}: {len(patterns)} patterns, {rows} rows, {len(cells)} cells')


if __name__ == '__main__':
    main(*sys.argv[1:])
