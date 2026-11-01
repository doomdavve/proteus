#!/usr/bin/env python3
"""A little boat, cozy variation. Writes only a-little-boat-cozy.xm."""
import math
import random
import struct

from generate_audio import MUSIC_DIR, event, finish_sample, fixed, instrument
from generate_boating_music import voice

BPM = 76
CHANNELS = 14


def acoustic(kind):
    """Soft, finite PCM approximations, tuned to XM C-4; no external recordings."""
    rate = 22050
    duration = {"piano": 4, "ukulele": 2, "kalimba": 3, "pad": 8,
                "woodwind": 2, "brush": .45}[kind]
    rng = random.Random(0xC02E)
    values = []
    noise = 0.0
    for i in range(round(rate * duration)):
        t = i / rate
        p = math.tau * 261.625565 * t
        if kind == "piano":
            value = sum(gain * math.sin(p * harmonic * (1 + .00012 * harmonic**2))
                        * math.exp(-t / decay)
                        for harmonic, gain, decay in ((1, 1, .95), (2, .28, .55),
                                                      (3, .09, .3), (4, .025, .15)))
        elif kind == "ukulele":
            value = sum(math.sin(p * h) / h**1.6 * math.exp(-t * (3 + h))
                        for h in range(1, 7))
        elif kind == "kalimba":
            value = (math.sin(p) * math.exp(-t / .65)
                     + .12 * math.sin(2.76 * p) * math.exp(-t / .13))
        elif kind == "pad":
            value = sum(g * (math.sin(p * r) + .1 * math.sin(2 * p * r))
                        for r, g in ((1, .6), (.998, .2), (1.002, .2)))
            value *= (1 - math.exp(-t / .65)) * math.exp(-t / 3)
        elif kind == "woodwind":
            value = (math.sin(p + .007 * math.sin(math.tau * 4.2 * t))
                     + .08 * math.sin(3 * p))
            value *= (1 - math.exp(-t / .09)) * math.exp(-t / .4)
        else:
            noise += .24 * (rng.uniform(-1, 1) - noise)
            value = (noise + .05 * math.sin(math.tau * 175 * t))
            value *= (1 - math.exp(-t / .018)) * math.exp(-t / .10)
        values.append(value)
    return finish_sample(values, .55, rate,
                         attack_seconds=.025 if kind != "pad" else .2,
                         release_seconds=2 if kind == "pad" else .12)


def pattern(index, piano_phrases=None):
    channels = CHANNELS + 2 if piano_phrases is not None else CHANNELS
    cells = [[event() for _ in range(channels)] for _ in range(64)]
    # Retain the original's C-major, two-bar harmonic rhythm and call/answer contour.
    chords = [(37, (53, 56, 58)), (44, (51, 56, 60)),
              (42, (53, 58, 61)), (37, (53, 56, 58)),
              (46, (53, 56, 61)), (42, (54, 58, 63)),
              (39, (54, 58, 61)), (44, (51, 56, 60))]
    melodies = [(65, 68, 70, 68), (63, 65, 68, 0),
                (65, 66, 70, 66), (68, 65, 63, 0),
                (68, 70, 73, 70), (66, 65, 61, 0),
                (66, 65, 63, 61), (63, 68, 63, 0)]
    for half in range(2):
        ci = (index * 2 + half) % 8
        root, chord = chords[ci]
        start = half * 32
        cells[start][7] = event(root, 7, 0x21, 8, 128)
        # Alternate pad banks: old tails decay beneath the next chord without retriggers.
        for j, note in enumerate(chord):
            cells[start][8 + half * 3 + j] = event(note, 4, 0x20, 8, (96, 160, 128)[j])
        for beat in (2, 18):
            for j, note in enumerate(chord):
                cells[start + beat + j][1 + j] = event(note, 2, 0x23 - j, 8,
                                                                     (96, 152, 120)[j])
        phrase = melodies[ci]
        if index >= 4:
            phrase = (phrase[0], phrase[2], phrase[1], phrase[3])
        for j, note in enumerate(phrase):
            if note:
                cells[start + 4 + j * 6][0] = event(note - 12, 1, 0x2b - (j % 2) * 2,
                                                   8, 120)
        if piano_phrases is not None:
            # Alternate melodies replace only the piano lane; accompaniment is shared.
            for row in range(start, start + 32):
                cells[row][0] = event()
            for j, (row, note) in enumerate(piano_phrases[half]):
                # Two piano voices let neighbouring notes decay together naturally.
                lane = 0 if j % 2 == 0 else CHANNELS
                cells[start + row][lane] = event(note, 1, 0x27, 8, 120)
        # Occasional replies leave breathing space around the piano.
        cells[start + 14][4] = event(chord[1] + 12, 3, 0x20, 8, 158)
        if ci in (1, 3, 5, 7):
            cells[start + 28][5] = event(chord[0] + 12, 5, 0x21, 8, 104)
        for row in (4, 12, 20, 28):
            cells[start + row][6] = event(49, 6, 0x1c if row % 16 == 4 else 0x1e,
                                         8, 136)
    data = b"".join(cell for row in cells for cell in row)
    return struct.pack("<IBHH", 9, 0, 64, len(data)) + data


def write_module(filename, title, pattern_fn=pattern, channels=CHANNELS):
    names = ("piano", "ukulele", "kalimba", "pad", "woodwind", "brush")
    out = bytearray(b"Extended Module: " + fixed(title, 20) + b"\x1a")
    out += fixed("Proteus generator", 20) + struct.pack("<HI", 0x0104, 276)
    out += struct.pack("<8H", 8, 0, channels, 8, 7, 1, 6, BPM)
    out += bytes(range(8)).ljust(256, b"\0")
    for index in range(8):
        out += pattern_fn(index)
    for name in names:
        out += instrument(name, acoustic(name))
    out += instrument("warm bass", voice("bass"))
    path = MUSIC_DIR / filename
    path.write_bytes(out)
    print(f"{path}: {32 * 4 * 60 / BPM:.2f}s loop, {BPM} BPM")


def main():
    write_module("a-little-boat-cozy.xm", "A little boat - cozy")


if __name__ == "__main__":
    main()
