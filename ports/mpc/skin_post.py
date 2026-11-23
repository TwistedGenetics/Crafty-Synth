#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Crafty Synth on the MPC: after the toolkit builds the skin (vst.json "skin_post"), the 3D wavetable views'
filmstrips go back to a 48-colour palette (make_views.py draws them so; the builder saves them as full colour, twice
the size). Copyright (C) 2026 Jamie (TwistedGenetics)."""
import sys
from pathlib import Path

from PIL import Image

for f in sorted(Path(sys.argv[1]).rglob("sh_meter_*.png")):
    im = Image.open(f).convert("RGB")
    im.quantize(colors=48, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(f, optimize=True)
