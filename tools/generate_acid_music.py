#!/usr/bin/env python3
"""Original 64-bar acid techno loop. Writes only assets/music/acid-wake.xm.

The monophonic phrase samples bake in slides, accents and moving resonant filters;
XM keeps the drum grid, phrase arrangement, panning and echo editable in a tracker.
No recordings or hardware emulation library are used. Standard library only.
"""

import math
import random
import struct

from generate_audio import (MUSIC_DIR, bandpass, event, finish_sample, fixed,
                            instrument, make_drums)

BPM = 132
RATE = 44100
STEP = 60 / BPM / 4
BARS = 64


def blep(phase, delta):
    """Round a saw's discontinuity over the neighbouring samples."""
    if phase < delta:
        x = phase / delta
        return x + x - x * x - 1
    if phase > 1 - delta:
        x = (phase - 1) / delta
        return x * x + x + x + 1
    return 0


def acid_phrase(index):
    # D minor: repeated roots, syncopated gaps, octave jumps and tied slides.
    notes = (38, 38, 50, 38, None, 41, 38, 45, 38, None, 50, 48, 38, 41, 45, 38)
    if index >= 4:
        notes = (38, None, 38, 50, 41, 38, 45, 48, None, 38, 50, 53, 45, 41, 38, 50)
    accents = {0, 3, 6, 10, 14}
    slides = {2, 6, 10, 14}  # A slide ties into the following sixteenth without retriggering.
    brightness = index % 4
    phase = x1 = x2 = y1 = y2 = 0.0
    frequency = 440 * 2 ** ((38 - 69) / 12)
    envelope = amplitude = 0.0
    values = []
    previous_step = -1
    coefficients = (0, 0, 0, 0)
    for i in range(round(16 * STEP * RATE)):
        position = i / RATE / STEP
        step = min(15, int(position))
        inside = position - step
        note = notes[step]
        tied = step > 0 and step - 1 in slides and notes[step - 1] is not None
        if step != previous_step:
            if note is not None and not tied:
                envelope = 1.0 if step in accents else .6
            previous_step = step
        target = 440 * 2 ** (((note if note is not None else 38) - 69) / 12)
        frequency += (target - frequency) * (1 - math.exp(-1 / (RATE * (.035 if tied else .001))))
        envelope *= math.exp(-1 / (RATE * (.14 if step in accents else .085)))
        gate = note is not None and (inside < .57 or step in slides or tied)
        level = (1.0 if step in accents else .72) if gate else 0
        amplitude += (level - amplitude) * (1 - math.exp(-1 / (RATE * (.002 if gate else .012))))
        delta = frequency / RATE
        phase = (phase + delta) % 1
        saw = 2 * phase - 1 - blep(phase, delta)
        square = (1 if phase < .5 else -1) + blep(phase, delta) - blep((phase + .5) % 1, delta)
        source = math.tanh(1.7 * (.85 * saw + .15 * square))
        if i % 16 == 0:
            # A stable resonant lowpass; brightness follows the phrase envelope and a slow sweep.
            sweep = .75 + .25 * math.sin(math.tau * position / 16 - .7)
            cutoff = 140 + brightness * 115 + envelope * (650 + brightness * 950) * sweep
            omega = math.tau * cutoff / RATE
            cosine = math.cos(omega)
            alpha = math.sin(omega) / (2 * (2.8 + brightness * .65))
            coefficients = ((1 - cosine) / (2 * (1 + alpha)),
                            (1 - cosine) / (1 + alpha),
                            -2 * cosine / (1 + alpha), (1 - alpha) / (1 + alpha))
        b0, b1, a1, a2 = coefficients
        filtered = b0 * (source + x2) + b1 * x1 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, source, y1, filtered
        values.append(math.tanh(1.5 * filtered + .12 * source) * amplitude)
    return finish_sample(values, .70, RATE, attack_seconds=.003, release_seconds=.012)


def percussion(kind):
    rng = random.Random(0x303 + (kind == "clap"))
    length = .30 if kind == "clap" else .22
    noise = [rng.uniform(-1, 1) for _ in range(round(length * RATE))]
    bright = bandpass(noise, 4600 if kind == "clap" else 8500, .7)
    values = []
    for i, value in enumerate(bright):
        t = i / RATE
        if kind == "clap":
            envelope = sum(math.exp(-(t - hit) / .008) for hit in (0, .012, .024)
                           if t >= hit)
            envelope += .5 * math.exp(-max(0, t - .024) / .055) if t >= .024 else 0
        else:
            envelope = (1 - math.exp(-t / .001)) * math.exp(-t / .07)
        values.append(value * envelope)
    return finish_sample(values, .60, RATE)


def stab():
    values = []
    for i in range(round(.6 * RATE)):
        t = i / RATE
        value = sum(math.sin(math.tau * 261.625565 * 2 ** (note / 12) * t)
                    for note in (0, 3, 7, 14))
        values.append(math.tanh(value * .5) * (1 - math.exp(-t / .004)) * math.exp(-t / .12))
    return finish_sample(values, .5, RATE)


def pattern(index):
    cells = [[event() for _ in range(8)] for _ in range(64)]
    # Kick / clap / closed hat / open hat / acid / echo / rim / minor-ninth stab.
    def put(row, channel, sample, volume, note=49, pan=128):
        cells[row][channel] = event(note, sample, 0x10 + volume, 8, pan)

    for bar in range(4):
        start = bar * 16
        breakdown = index == 8 or (index == 9 and bar < 2)
        intensity = (0, 1, 1, 2, 2, 3, 3, 3, 0, 2, 3, 3, 3, 2, 1, 0)[index]
        if not breakdown:
            for beat in range(4):
                put(start + beat * 4, 0, 1, 44)
        if index not in (0, 8) and not (index == 15 and bar >= 2):
            for row in (4, 12):
                put(start + row, 1, 3, 30 if not breakdown else 16, pan=145)
        for row in range(2, 16, 4):
            put(start + row, 2, 2, 19 if not breakdown else 9, pan=95)
        if index in range(3, 8) or index in range(10, 14):
            for row in (3, 7, 11, 15):
                put(start + row, 2, 2, 9, pan=95)
            for row in (2, 10):
                put(start + row, 3, 4, 20, pan=170)
        if index not in (0, 8) or bar % 2:
            variant = intensity + (4 if (bar % 2 and index >= 4) else 0)
            put(start, 4, 6 + variant, 32 if not breakdown else 19)
            put(start + 3, 5, 6 + variant, 8 if not breakdown else 11, pan=205)
        if index in (5, 7, 11, 13) and bar == 3:
            for row in (10, 13, 14, 15):
                put(start + row, 6, 2, 13 + (row % 3) * 3, note=44, pan=180)
        if index == 9 and bar == 3:
            for row in range(0, 16, 2):
                put(start + row, 1, 3, 16 + row)
        if index in (4, 8, 10, 12) and bar % 2 == 0:
            put(start + 2, 7, 5, 15 if index != 8 else 22, note=39, pan=65)
    data = b"".join(cell for row in cells for cell in row)
    return struct.pack("<IBHH", 9, 0, 64, len(data)) + data


def compose():
    drums = make_drums()
    samples = [("Four on the river", drums["kick"]), ("Closed hat", drums["hat"]),
               ("Warehouse clap", percussion("clap")), ("Open hat", percussion("open")),
               ("D minor ninth", stab())]
    samples += [(f"Acid phrase {i + 1}", acid_phrase(i)) for i in range(8)]
    out = bytearray(b"Extended Module: " + fixed("Acid Wake", 20) + b"\x1a")
    out += fixed("Proteus generator", 20) + struct.pack("<HI", 0x0104, 276)
    out += struct.pack("<8H", 16, 0, 8, 16, len(samples), 1, 6, BPM)
    out += bytes(range(16)).ljust(256, b"\0")
    for index in range(16):
        out += pattern(index)
    for name, sample in samples:
        out += instrument(name, sample)
    return bytes(out)


def main():
    path = MUSIC_DIR / "acid-wake.xm"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(compose())
    print(f"{path}: {BARS * 4 * 60 / BPM:.2f}s loop, {BPM} BPM, {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
