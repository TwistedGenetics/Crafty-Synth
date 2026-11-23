#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Crafty Synth on the MPC: after the toolkit builds the skin (vst.json "skin_post"), the 3D wavetable views'
filmstrips go back to a 48-colour palette (make_views.py draws them so; the builder saves them as full colour, twice
the size). Copyright (C) 2026 Jamie (TwistedGenetics)."""
import sys
from pathlib import Path

from PIL import Image

PORT = Path(__file__).resolve().parent
skin = Path(sys.argv[1])
# 0.20: a wavetable file's 3D view gets a fixed name (FILE_PIC in cr_mpc.c), so the plugin can draw it on the MPC
want = {Image.open(PORT / "art" / f"wt{k}_file.png").convert("RGB").tobytes(): f"sh_crafty_file{k}.png"
        for k in (1, 2)}
for f in sorted(skin.rglob("sh_meter_*.png")):
    im = Image.open(f).convert("RGB")
    name = want.get(im.tobytes()) if im.size == (560, 180 * 17) else None
    if name:
        for j in skin.rglob("*.json"):
            t = j.read_text()
            if f.name in t:
                j.write_text(t.replace(f.name, name))
        f.rename(f.with_name(name))
        continue
    im.quantize(colors=48, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(f, optimize=True)
