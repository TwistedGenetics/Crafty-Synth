#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""FM1-Craft: the factory wavetable banks, after the layout of the Modal CRAFTsynth 2.0's (9 banks of 5 morphable
waves: VA, two digital banks, math, polygon, two curated banks, formant, and a sine / noise bank for Wave 2). The
waves themselves are FM1-Craft's own, generated here (Modal's are not copied).

Each wave is one cycle of 256 samples, DC removed, peak at CR_WT_PEAK (int16). The firmware band-limits them into
mip levels at run time (src/dsp/cr_wt.c), so this is only the base level, and the user banks (Serum import) go
through the same path.

    tools/gen_wavetables.py OUT.h            the C header (CR_WT_FACTORY, CR_SIN256)
    tools/gen_wavetables.py --json OUT.json  the same numbers, for the editor's pictures and tests
    tools/gen_wavetables.py --png OUT.png    a sheet of every wave (to look at)

Copyright (C) 2026 Jamie (TwistedGenetics)."""
import json
import math
import sys

import numpy as np

N = 256
PEAK = 26000                     # int16: the band-limited levels overshoot (Gibbs) by up to ~10 %
T = np.arange(N) / N             # phase 0..1


def additive(amps, phases=None):
    """sum of harmonics 1..len(amps) (sine phase), amps[k-1] for harmonic k"""
    y = np.zeros(N)
    for k, a in enumerate(amps, start=1):
        if a and k < N // 2:
            ph = 0.0 if phases is None else phases[k - 1]
            y += a * np.sin(2 * np.pi * k * T + ph)
    return y


def bl(y, hmax=127):
    """band-limit a drawn wave to hmax harmonics (the base level must not alias against itself)"""
    f = np.fft.rfft(y)
    f[hmax + 1:] = 0
    f[0] = 0
    return np.fft.irfft(f, N)


def norm(y):
    y = y - y.mean()
    m = np.max(np.abs(y))
    return y / m if m > 0 else y


def formant_wave(formants, f0=110.0, nh=100):
    """a vowel: harmonics of f0 weighted by resonances (centre Hz, bandwidth Hz, gain)"""
    amps = []
    for k in range(1, nh + 1):
        f = k * f0
        a = 0.0
        for fc, bw, g in formants:
            a += g / (1.0 + ((f - fc) / (bw / 2)) ** 2)
        amps.append(a / k ** 0.3)
    return additive(amps)


def polygon(n, teeth=0.0, rot=0.0):
    """the x of a point going round a regular n-gon (its outline traced at constant speed): n = 3 a skewed
    triangle-like wave, 4 a trapezoid, many a sine; teeth > 0 makes it a star"""
    y = np.zeros(N)
    for i, t in enumerate(T):
        a = 2 * np.pi * t + rot
        seg = 2 * np.pi / n
        k = math.floor(a / seg)
        a0, a1 = k * seg, (k + 1) * seg
        f = (a - a0) / seg
        r0 = r1 = 1.0
        if teeth:
            r1 = 1.0 - teeth                     # every other vertex pulled in: a star
            if k % 2:
                r0, r1 = r1, r0
        x0, y0 = r0 * math.cos(a0), r0 * math.sin(a0)
        x1, y1 = r1 * math.cos(a1), r1 * math.sin(a1)
        y[i] = y0 + (y1 - y0) * f
    return y


def banks():
    saw = additive([1 / k for k in range(1, 128)])
    sq = additive([1 / k if k % 2 else 0 for k in range(1, 128)])
    tri = additive([((-1) ** ((k - 1) // 2)) / k ** 2 if k % 2 else 0 for k in range(1, 128)])
    sine = np.sin(2 * np.pi * T)
    pulse25 = bl(np.where(T < 0.25, 1.0, -1.0 / 3))
    B = []
    # 1 ANALOGUE: sine, triangle, pulse, square, saw (Wave CC 0 / 32 / 64 / 96 / 127)
    B.append([sine, tri, pulse25, sq, saw])
    # 2 DIGITAL 1: harmonic bands sweeping up, a PPG-ish set
    d1 = []
    for c, w in ((3, 2.0), (6, 2.5), (10, 3.0), (16, 4.0), (24, 6.0)):
        d1.append(additive([math.exp(-((k - c) / w) ** 2) + (0.5 if k == 1 else 0) for k in range(1, 64)]))
    B.append(d1)
    # 3 MATH: sin(x + a sin x), a sinc pulse train, a cubed sine, Chebyshev T5 of a sine, |sin| products
    m = [np.sin(2 * np.pi * T + 1.5 * np.sin(2 * np.pi * T)),
         bl(np.sinc((T - 0.5) * 24)),
         np.sin(2 * np.pi * T) ** 3 + 0.3 * np.sin(6 * np.pi * T),
         np.cos(5 * np.arccos(np.clip(0.98 * np.sin(2 * np.pi * T), -1, 1))),
         bl(np.abs(np.sin(2 * np.pi * T)) * np.sin(6 * np.pi * T) + 0.4 * np.sign(np.sin(2 * np.pi * T)) *
            np.abs(np.sin(10 * np.pi * T)))]
    B.append(m)
    # 4 POLYGON: triangle, square, pentagon, a 5-point star, a 7-point star
    B.append([bl(polygon(3, rot=0.5)), bl(polygon(4, rot=math.pi / 4)), bl(polygon(6, rot=0.3)), bl(polygon(10, 0.55)), bl(polygon(14, 0.7))])
    # 5 CURATED 1: hollow organ, reed, glass, metallic, rich brass
    c1 = [additive([1, 0, 0.6, 0, 0.4, 0, 0, 0.5] + [0] * 7 + [0.25]),
          additive([1 / k ** 0.6 if k % 2 else 0.15 / k for k in range(1, 64)]),
          additive([1, 0, 0, 0.5, 0, 0, 0, 0, 0.35, 0, 0, 0, 0, 0, 0, 0.2]),
          additive([0.6, 0.2, 0.9, 0.1, 0.1, 0.7, 0, 0.4, 0, 0, 0.55, 0, 0, 0.3, 0, 0, 0.2, 0, 0, 0, 0.3]),
          additive([1.0 / k ** 0.5 * (1.0 if k < 12 else 0.3) for k in range(1, 48)])]
    B.append(c1)
    # 6 FORMANT: A, E, I, O, U (male-ish formants on a 110 Hz cycle)
    vowels = {"A": [(730, 90, 1.0), (1090, 110, 0.5), (2440, 160, 0.25)],
              "E": [(530, 60, 1.0), (1840, 100, 0.55), (2480, 120, 0.3)],
              "I": [(270, 60, 1.0), (2290, 100, 0.4), (3010, 120, 0.3)],
              "O": [(570, 70, 1.0), (840, 80, 0.6), (2410, 140, 0.15)],
              "U": [(300, 60, 1.0), (870, 70, 0.35), (2240, 120, 0.1)]}
    B.append([formant_wave(v) for v in vowels.values()])
    # 7 DIGITAL 2: stepped saw, comb, sync-like, two-level pulse train, spectral spikes
    st = np.floor(T * 8) / 8
    B.append([bl(st * 2 - 1),
              additive([1 / k if k % 3 else 0 for k in range(1, 100)]),
              bl(np.sin(2 * np.pi * (T * 3.5) % 1 * 1.0) * (1 - T)),
              bl(np.where((T * 6) % 1 < 0.3, 1.0, -0.4) * np.where(T < 0.5, 1, 0.5)),
              additive([1.0 if k in (1, 7, 13, 19, 29, 41) else 0 for k in range(1, 60)])])
    # 8 CURATED 2 (for jungle / DnB basses): dread (saw + sub-octave weight), donk (sine, 2nd, a click), hoover-ish
    # pulse-saw, growl (an FM spectrum), wide (all harmonics, scattered phases: a supersaw's one cycle)
    rng = np.random.default_rng(1993)
    B.append([additive([1.6] + [1 / k for k in range(2, 80)]),
              additive([1, 0.7, 0.15, 0.4, 0.05, 0.25]) + 0.15 * bl(np.exp(-T * 60) * np.sin(2 * np.pi * 40 * T)),
              bl(0.6 * (2 * ((T + 0.1) % 1) - 1) + 0.5 * np.where(T < 0.18, 1.0, -0.22)),
              np.sin(2 * np.pi * T + 2.2 * np.sin(4 * np.pi * T + 0.9 * np.sin(6 * np.pi * T))),
              additive([1 / k ** 0.8 for k in range(1, 100)], rng.uniform(0, 2 * np.pi, 99))])
    # 9 SINE / NOISE (Wave 2): the firmware makes the noise; the table only holds the sine for pictures
    B.append([sine] * 5)
    return [[norm(w) for w in b] for b in B]


def as_int(B):
    return [[[int(round(float(v) * PEAK)) for v in w] for w in b] for b in B]


def main():
    out = sys.argv[-1]
    B = as_int(banks())
    if "--json" in sys.argv:
        open(out, "w").write(json.dumps({"peak": PEAK, "banks": B}, separators=(",", ":")))
        return
    if "--png" in sys.argv:
        from PIL import Image, ImageDraw
        im = Image.new("RGB", (5 * 160, 9 * 90), "white")
        d = ImageDraw.Draw(im)
        for bi, b in enumerate(B):
            for wi, w in enumerate(b):
                x0, y0 = wi * 160, bi * 90
                pts = [(x0 + 5 + i * 150 / N, y0 + 45 - v / PEAK * 38) for i, v in enumerate(w)]
                d.line(pts, fill="black", width=2)
        im.save(out)
        return
    L = ["/* generated by tools/gen_wavetables.py: FM1-Craft's factory wavetables, 9 banks x 5 waves x 256 */",
         "#pragma once", "#include <stdint.h>", f"#define CR_WT_PEAK {PEAK}",
         "static const int16_t CR_WT_FACTORY[9][5][256] = {"]
    for b in B:
        L.append(" {")
        for w in b:
            L.append("  {" + ",".join(map(str, w)) + "},")
        L.append(" },")
    L.append("};")
    L.append("/* sin(2 pi i / 256), for the band-limiting (exact, not the synth's fast approximation) */")
    L.append("static const float CR_SIN256[256] = {" +
             ",".join(f"{math.sin(2 * math.pi * i / 256):.8f}f" for i in range(256)) + "};")
    open(out, "w").write("\n".join(L) + "\n")


if __name__ == "__main__":
    main()
