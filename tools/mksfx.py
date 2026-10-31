"""Synthesize the placeholder sound effects as 8 kHz mono WAVs (converted to BRR by snesbrr).

Usage: mksfx.py <output dir>
Order must match the SFX_* constants in src/main.c.
"""
import math
import os
import random
import struct
import sys
import wave

RATE = 8000  # played back with pitch 2 (2 x 4 kHz)
rng = random.Random(7)


def square(f, t):
    return 1.0 if (f * t) % 1.0 < 0.5 else -1.0


def lock():  # short wooden knock
    n = int(0.06 * RATE)
    return [0.7 * math.sin(2 * math.pi * (180 - 900 * i / RATE) * i / RATE) * math.exp(-i / RATE * 60) for i in range(n)]


def clear():  # rising arpeggio
    out = []
    for f in (660, 830, 990, 1320):
        for i in range(int(0.05 * RATE)):
            t = i / RATE
            out.append(0.45 * square(f, t) * (1 - t / 0.06))
    return out


def junk():  # low thump with some noise: rows pushed into your well
    n = int(0.25 * RATE)
    out, lp = [], 0.0
    for i in range(n):
        t = i / RATE
        lp += 0.3 * (rng.uniform(-1, 1) - lp)
        out.append((0.6 * math.sin(2 * math.pi * 70 * t) + lp) * math.exp(-t * 12))
    return out


def join():
    out = []
    for f, d in ((990, 0.04), (1320, 0.06)):
        for i in range(int(d * RATE)):
            out.append(0.4 * square(f, i / RATE) * (1 - i / (d * RATE)))
    return out


def topout():  # falling square sweep
    n = int(0.6 * RATE)
    out, phase = [], 0.0
    for i in range(n):
        t = i / RATE
        phase += max(700 * (1 - t * 1.5), 60) / RATE
        out.append(0.5 * (1.0 if phase % 1.0 < 0.5 else -1.0) * (1 - t / 0.6))
    return out


SOUNDS = [('lock', lock), ('clear', clear), ('junk', junk), ('join', join), ('topout', topout)]

out_dir = sys.argv[1]
os.makedirs(out_dir, exist_ok=True)
for name, fn in SOUNDS:
    with wave.open(os.path.join(out_dir, name + '.wav'), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(max(-1, min(1, s)) * 30000)) for s in fn()))
