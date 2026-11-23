#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Crafty Synth on the MPC: the oscillators' 3D wavetable views, drawn as the editor and the AU / VST3 draw them
(web/editor/app.js drawTable: the bank's frames stacked in depth, faint, the one playing bright and filled).

One filmstrip per factory bank and wave (art/wt1_<bank>.png, art/wt2_<bank>.png): frame n is WT POS at n / (N - 1),
so MPC shows the right one as WT POS turns (a `meter` on the position, shown while that bank plays). art/wt_user.png
is what a user bank or a wavetable file shows (its waves are only known on the MPC). Run by make_port.py --views;
the PNGs are committed. Copyright (C) 2026 Jamie (TwistedGenetics)."""
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

PORT = Path(__file__).resolve().parent
ART = PORT / "art"
W, H = 560, 180                    # the view (the layout's meter w x h)
FRAMES = 17                        # WT POS in 16 steps: each of the 5 waves and 3 morphs between (memory: 7 MB a strip)
COLOURS = 48                       # a palette: a strip in about 400 KB
SS = 2                             # drawn at twice the size, then scaled down (smooth lines)
BG, LINE = (0x14, 0x17, 0x1B), (0x34, 0x3A, 0x42)
ACC, ACC2 = (0x3F, 0xA9, 0xF5), (0x7E, 0xDE, 0xC8)
PEAK = 26000
N = 256


def factory(gen):
    """the factory banks from build/gen/cr_wavetables.h: [bank][wave][256]"""
    nums = [int(x) for x in re.findall(r"-?\d+", gen.read_text().split("= {", 1)[1])]
    return [[nums[(b * 5 + w) * N:(b * 5 + w + 1) * N] for w in range(5)] for b in range(len(nums) // (5 * N))]


def wave_at(d, bank, pos, k):
    """the editor's waveAt: the five waves morphed, the sine / noise bank's noise"""
    m = pos * 4
    a = min(3, int(m))
    f = m - a
    v = (d[a][k] + (d[a + 1][k] - d[a][k]) * f) / PEAK
    if bank == 8 and pos > 0:
        n = ((((k * 2654435761) & 0xFFFFFFFF) >> 23) & 255) / 128 - 1
        v += (n - v) * min(1, pos * 2)
    return v


def frame(d, bank, pos, col):
    w, h = W * SS, H * SS
    im = Image.new("RGBA", (w, h), BG + (255,))
    F = 15
    dx, dy = w * 0.018, h * 0.035
    fw = w - 16 * SS - dx * (F - 1)
    amp = (h - 20 * SS - dy * (F - 1)) * 0.45
    x0, base = 8 * SS, h - 10 * SS - amp
    lay = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    g = ImageDraw.Draw(lay)
    for f in range(F - 1, -1, -1):
        ox, oy = x0 + f * dx, base - f * dy
        a = int(255 * (0.14 + 0.32 * (1 - f / F)))
        pts = [(ox + i, oy - wave_at(d, bank, f / (F - 1), min(255, int(i / fw * 256))) * amp)
               for i in range(0, int(fw) + 1, 2 * SS)]
        g.line(pts, fill=col + (a,), width=SS)
    im = Image.alpha_composite(im, lay)
    sx, sy = x0 + pos * dx * (F - 1), base - pos * dy * (F - 1)
    pts = [(sx + i, sy - wave_at(d, bank, pos, min(255, int(i / fw * 256))) * amp) for i in range(0, int(fw) + 1, SS)]
    fill = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(fill).polygon([(sx, sy)] + pts + [(sx + fw, sy)], fill=col + (int(255 * 0.28),))
    im = Image.alpha_composite(im, fill)
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(glow).line(pts, fill=col + (200,), width=4 * SS)
    im = Image.alpha_composite(im, glow.filter(ImageFilter.GaussianBlur(5 * SS)))
    top = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(top).line(pts, fill=col + (255,), width=int(2.5 * SS))
    im = Image.alpha_composite(im, top)
    ImageDraw.Draw(im).rectangle([0, 0, w - 1, h - 1], outline=LINE, width=SS)
    return im.resize((W, H), Image.LANCZOS).convert("RGB")


def strip(d, bank, col):
    s = Image.new("RGB", (W, H * FRAMES))
    for n in range(FRAMES):
        s.paste(frame(d, bank, n / (FRAMES - 1), col), (0, n * H))
    return s


NCOL, LEVELS = 56, 33                # a user table's view: 56 columns, each one of 33 heights (16 up, 16 down)


def column_strip(col):
    """one column of a user table's view (its waves are only known on the MPC: the engine sets 56 columns' heights,
    each a filmstrip of 33 frames): frame k fills from the centre line to (k - 16) / 16 of the height, capped bright"""
    cw = W // NCOL
    s = Image.new("RGBA", (cw, H * LEVELS), (0, 0, 0, 0))
    d = ImageDraw.Draw(s)
    amp = (H - 24) / 2
    for k in range(LEVELS):
        y0, mid = k * H, k * H + H // 2
        v = (k - 16) / 16
        top = mid - v * amp
        d.rectangle([0, min(mid, top), cw - 1, max(mid, top)], fill=col + (int(255 * 0.32),))
        d.rectangle([0, top - 1.5, cw - 1, top + 1.5], fill=col + (255,))
    return s


def user_view():
    """a user bank or file's view behind its columns: the box, a faint grid and the centre line"""
    w, h = W * SS, H * SS
    im = Image.new("RGBA", (w, h), BG + (255,))
    lay = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    g = ImageDraw.Draw(lay)
    for gx in range(1, 8):                               # a faint grid
        g.line([(w * gx / 8, 0), (w * gx / 8, h)], fill=LINE + (120,), width=SS)
    for gy in (1, 3):
        g.line([(0, h * gy / 4), (w, h * gy / 4)], fill=LINE + (120,), width=SS)
    g.line([(0, h / 2), (w, h / 2)], fill=LINE + (255,), width=SS)
    im = Image.alpha_composite(im, lay)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, w - 1, h - 1], outline=LINE, width=SS)
    return im.resize((W, H), Image.LANCZOS).convert("RGB")


def badge(letter, col):
    """the editor's oscillator badge: a 30 px rounded square in the oscillator's colour, its letter in the panel's"""
    s = 30 * 4
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=5 * 4, fill=col + (255,))
    font = ImageFont.truetype(str(PORT / "fonts" / "BarlowSemiCondensed-Bold.ttf"), 22 * 4)
    d.text((s / 2, s / 2 + 2 * 4), letter, font=font, fill=(0x22, 0x27, 0x2D), anchor="mm")
    return im.resize((30, 30), Image.LANCZOS)


def main(gen):
    ART.mkdir(exist_ok=True)
    banks = factory(gen)
    for wave, col, nb in ((1, ACC, 8), (2, ACC2, 9)):      # Wave 1 has no sine / noise bank
        for b in range(nb):
            q = strip(banks[b], b, col).quantize(colors=COLOURS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
            q.save(ART / f"wt{wave}_{b}.png", optimize=True)
    user_view().quantize(colors=COLOURS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(
        ART / "wt_user.png", optimize=True)
    for letter, col in (("a", ACC), ("b", ACC2)):           # a user table's columns, per oscillator colour
        column_strip(col).save(ART / f"wtcol_{letter}.png", optimize=True)
    for letter, col in (("A", ACC), ("B", ACC2)):           # the editor's badges beside OSC A / OSC B
        badge(letter, col).save(ART / f"badge_{letter.lower()}.png", optimize=True)


if __name__ == "__main__":
    main(Path(sys.argv[1]) if len(sys.argv) > 1 else PORT / "build" / "gen" / "cr_wavetables.h")
