#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""FM1-Craft: the 64 factory presets (8 banks of 8, as the CRAFTsynth), written as values a person would set, turned
into patch bytes here. The parameter table is read from src/core/cr_patch.c, so the two can't drift apart.

    tools/gen_presets.py OUT.h          the C header (CR_FACTORY[64][128])
    tools/gen_presets.py --json OUT     the same for the editor (names, bytes)

Copyright (C) 2026 Jamie (TwistedGenetics)."""
import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HDR = (ROOT / "src/core/cr_patch.h").read_text()
SRC = (ROOT / "src/core/cr_patch.c").read_text()

IDS = re.sub(r"/\*.*?\*/", "", re.search(r"enum \{\s*(CR_W1POS.*?)CR_NP\s*\}", HDR, re.S).group(1), flags=re.S)
IDS = [x.strip() for x in IDS.split(",") if x.strip()]
P = {name[3:]: i for i, name in enumerate(IDS)}
DEF = [0] * len(IDS)
for m in re.finditer(r"\[CR_(\w+)\] = \{(\d+), CR_K_\w+, (\d+),", SRC):
    DEF[P[m.group(1)]] = int(m.group(3))
DEST = re.search(r"enum \{\s*(CR_D_NONE.*?)CR_ND\s*\}", HDR, re.S).group(1)
DEST = [x.strip()[5:] for x in DEST.replace("\n", " ").split(",") if x.strip()]
D = {n: i for i, n in enumerate(DEST)}
SRCS = ["LFO1", "LFO2", "MEG", "NOTE", "VELO", "MODW", "EXPR", "AFTT"]
MODS = ["PM", "WSYNC", "RING", "TFOLD", "DEREZ", "ROR", "GLITCH", "WAMP", "FM", "HSYNC", "MIN", "SFOLD", "CRUSH",
        "SCRUNCH", "LOFI", "VOCAL"]
BANKS = ["ANALOGUE", "DIGITAL1", "MATH", "POLYGON", "CURATED1", "FORMANT", "DIGITAL2", "CURATED2", "NOISE"]
BMODES = ["FM", "PM", "SYNC", "RING", "FOLD"]
FTYPES = ["SVF", "LADDER", "MS20", "TB303", "SEM", "STEINER", "POLIVOKS", "FORMANT", "COMB", "PHASER", "DUALPEAK",
          "DECIMATOR"]
VMODES = ["ROOM", "HALL", "HALO", "SWELL", "ABYSS", "DUB"]
ERAS = ["OFF", "SP1200", "MPC60", "S950", "S1000", "AMIGA"]


def band_val(i, n):
    return (i * 128 + 64) // n


SHAPES = {"UNISON": 0, "MAJOR": 67, "MINOR": 74, "MAJ6": 81, "SUS4": 88, "FIFTHS": 95, "FIFTHOCT": 102,
          "OCT12": 109, "OCT1M1": 116, "OCTM12": 124}


def bank_val(b, wave2):
    n = 41 if wave2 else 40
    i = b if wave2 or b < 8 else b - 1
    return (i * 128 + 64) // n


def time_v(sec):
    return max(0, min(127, round(127 * ((max(sec, 0.001) - 0.001) / 4) ** (1 / 3))))


def hz_v(f):
    return max(0, min(127, round(127 * math.log2(f / 20) / 10)))


def bip(x):
    return max(1, min(127, 64 + round(x * 63)))


def semi_v(s):
    for v in range(128):
        d = v - 64
        got = (d * 48 + 31) // 63 if d >= 0 else -((-d * 48 + 31) // 64)
        if got == s:
            return v
    raise ValueError(s)


def oct_v(o):
    for v in range(128):
        if (v * 7) >> 7 == o + 2:
            return v + 8 if v + 8 < 128 and (v + 8) * 7 >> 7 == o + 2 else v
    raise ValueError(o)


def lfo_v(hz):
    return max(0, min(127, round(127 * math.log2(hz / 0.02) / 10.64)))


def wave(i):
    """wave 1..5 of a bank, or a float position between them"""
    return max(0, min(127, round((i - 1) / 4 * 127)))


PRESETS = []


def preset(name, **kw):
    p = list(DEF)
    mods = kw.pop("mod", {})
    steps = kw.pop("steps", [])
    root = kw.pop("root", 53)
    for k, v in kw.items():
        k = k.upper()
        if k == "B1":
            p[P["W1BANK"]] = bank_val(BANKS.index(v), False)
        elif k == "B2":
            p[P["W2BANK"]] = bank_val(BANKS.index(v), True)
        elif k == "MODE":
            p[P["OMMODE"]] = MODS.index(v) * 8 + 4
        elif k == "BMODE":
            p[P["BMODE"]] = band_val(BMODES.index(v), 5)
        elif k == "FTYPE":
            p[P["FTYPE"]] = band_val(FTYPES.index(v), 12)
        elif k == "VMODE":
            p[P["VMODE"]] = band_val(VMODES.index(v), 6)
        elif k == "ERA":
            p[P["ERA"]] = band_val(ERAS.index(v), 6)
        elif k in ("VMIX", "VDECAY", "VWARP", "VDENS", "VMOD", "CHMIX", "PHMIX", "CRUSH", "OTT"):
            p[P[k]] = round(v * 127)
        elif k == "SHAPE":
            p[P["SPREAD"]] = SHAPES[v]
        elif k in ("FA", "FD", "FR", "AA", "AD", "AR", "MA", "MD", "MR"):
            p[P[k]] = time_v(v)
        elif k in ("FS", "AS", "MS", "DFB"):
            p[P[k]] = round(v * 127)
        elif k == "CUT":
            p[P["CUT"]] = hz_v(v)
        elif k in ("FEG", "AEG"):
            p[P[k + "AMT"]] = bip(v)
        elif k == "TUNE":
            p[P["TUNE"]] = semi_v(v)
        elif k == "FINE":
            p[P["FINE"]] = bip(v)
        elif k == "OCT":
            p[P["OCT"]] = oct_v(v)
        elif k in ("L1", "L2"):
            p[P[k + "RATE"]] = lfo_v(v) if k == "L1" else min(63, round(lfo_v(v) / 2))
        elif k == "TEMPO":
            p[P["TEMPO"]] = (v - 40) // 2
        elif k in ("W1", "W2"):
            p[P[k + "POS"]] = wave(v)
        elif k in P:
            p[P[k]] = v
        else:
            raise KeyError(k)
    for src, (dst, depth) in mods.items():
        i = SRCS.index(src)
        p[P["DEP0"] + i] = bip(depth)
        p[P["DST0"] + i] = D[dst]
    assert len(name) <= 11, name
    b = bytearray(128)                       # cr_patch_t, version 2
    b[:len(name)] = name.encode()
    b[12:12 + len(p)] = bytes(p)
    b[108] = len(steps)
    b[109] = 0xC5
    b[110] = 2
    b[111] = root
    for i, s in enumerate(steps):
        b[112 + i] = 0 if s is None else 64 + s
    PRESETS.append((name, bytes(b)))


R = None   # a rest in a pattern

# ---- bank 1: CLASSICS ----
preset("INIT SAW")
preset("PWM LEAD", W1=4, W2=4, MODE="ROR", OMAMT=40, mod={"LFO1": ("OMAMT", 0.5)}, L1=0.7, CUT=5000, RES=20,
       AD=0.4, AS=0.8, GLIDE=74)
preset("SYNC SWEEP", W1=5, MODE="HSYNC", OMAMT=10, mod={"MEG": ("OMAMT", 0.85)}, MD=0.6, CUT=7000, FEG=0.2, FD=0.5)
preset("FM BELL", W1=1, W2=1, MODE="FM", OMAMT=55, TUNE=19, mod={"MEG": ("OMAMT", 0.6)}, MD=0.8, AD=1.6, AS=0,
       AR=1.5, DMIX=30, DTIME=70)
preset("ROUND PAD", W1=2.6, W2=4.6, MIX=50, SHAPE="UNISON", SPREAD=40, AA=0.8, AR=1.4, CUT=2400, RES=15,
       mod={"LFO1": ("W1", 0.2)}, L1=0.3, DMIX=25)
preset("WARM BASS", W1=5, W2=4, MIX=40, TUNE=-12, CUT=600, RES=30, FEG=0.45, FD=0.3, FS=0.2, AD=0.6, AS=0.7, OCT=-1)
preset("RING BELLS", B1="CURATED1", W1=3, B2="ANALOGUE", W2=1, MODE="RING", OMAMT=100, TUNE=17, AD=1.2, AS=0,
       AR=1.2, DMIX=35, DTIME=80, DFB=0.5)
preset("WEST COAST", W1=1, MODE="TFOLD", OMAMT=30, mod={"MEG": ("OMAMT", 0.7), "VELO": ("OMAMT", 0.3)}, MD=0.5,
       FD=0.5, FS=0, FEG=0.5, CUT=900, AD=0.5, AS=0, AR=0.5)
# ---- bank 2: BASS ----
preset("SUB BASS", W1=1, W2=2, MIX=20, OCT=-1, CUT=1500, AR=0.1)
preset("DREAD", B1="CURATED2", W1=1, B2="ANALOGUE", W2=1, MIX=35, TUNE=-12, OCT=-1, CUT=380, RES=45, FEG=0.3,
       FD=0.25, FS=0.3, DIST=40, GLIDE=72, AR=0.15)
preset("DONK", B1="CURATED2", W1=2, OCT=-1, CUT=2600, RES=25, FEG=0.35, FD=0.12, FS=0, AD=0.25, AS=0, AR=0.1)
preset("REESE", W1=5, W2=5, MIX=50, SPREAD=50, OCT=-1, CUT=900, RES=20, mod={"LFO1": ("W1", 0.15)}, L1=0.15,
       DIST=20, AR=0.2)
preset("ROLLER", W1=4, W2=5, MIX=30, TUNE=-12, OCT=-1, CUT=300, RES=35, FEG=0.55, FD=0.18, FS=0.1, AD=0.4,
       AS=0.6, AR=0.08)
preset("ACID", W1=5, CUT=400, RES=105, FEG=0.55, FD=0.2, FS=0.05, GLIDE=80, DIST=35, AR=0.1)
preset("FM GROWL", W1=1, W2=5, MODE="FM", OMAMT=50, TUNE=-12, OCT=-1, CUT=1100, RES=40,
       mod={"LFO1": ("OMAMT", 0.5)}, L1=3, L1SYNC=0, DIST=50)
preset("808 BOOM", W1=1, OCT=-1, CUT=3000, AD=1.4, AS=0, AR=0.9, MD=0.08, MS=0, DIST=30,
       mod={"MEG": ("PITCH", 0.25)})
# ---- bank 3: JUNGLE ----
preset("HOOVER", B1="CURATED2", W1=3, B2="ANALOGUE", W2=5, MIX=45, SHAPE="OCT1M1", GLIDE=100, CUT=4000, RES=20,
       mod={"MEG": ("PITCH", -0.06), "LFO1": ("W1", 0.15)}, MD=0.25, MS=0, L1=4.5, AR=0.4)
preset("RAVE STAB", W1=5, W2=4, MIX=40, SHAPE="MINOR", CUT=3500, RES=40, FEG=0.3, FD=0.2, FS=0, AD=0.35, AS=0,
       AR=0.25, DMIX=28, DSYNC=127, DTIME=60)
preset("PHRYG STAB", B1="DIGITAL1", W1=3, W2=5, SHAPE="SUS4", MIX=40, CUT=2800, RES=35, FEG=0.3, FD=0.25, FS=0,
       AD=0.4, AS=0, AR=0.3, DMIX=30, DSYNC=127, DTIME=75)
preset("DUB SIREN", W1=4, MODE="PM", OMAMT=20, L2=0.8, mod={"LFO2": ("PITCH", 0.2)}, L2SHAPE=60, DMIX=45, DFB=0.7,
       DTIME=95, CUT=3000, RES=50)
preset("NEURO", B1="CURATED2", W1=4, W2=5, MODE="FM", OMAMT=40, TUNE=-5, OCT=-1, CUT=800, RES=60,
       mod={"LFO1": ("CUT", 0.45), "LFO2": ("OMAMT", 0.3)}, L1SYNC=127, L1RATE=88, L2=0.4, DIST=70)
preset("TECH STAB", B1="DIGITAL2", W1=2, SHAPE="FIFTHOCT", CUT=2200, RES=55, FEG=0.35, FD=0.15, FS=0, AD=0.3,
       AS=0, AR=0.2, DMIX=30, DSYNC=127, DTIME=60, DIST=25)
preset("WOBBLE", W1=5, W2=4, MIX=50, SPREAD=25, OCT=-1, CUT=200, RES=70, mod={"LFO1": ("CUT", 0.8)}, L1SYNC=127,
       L1RATE=92, DIST=45)
preset("MIN REESE", W1=5, W2=5, MODE="MIN", OMAMT=127, FINE=0.12, OCT=-1, CUT=1200, RES=25,
       mod={"LFO1": ("O2PITCH", 0.02)}, L1=0.2, DIST=30)
# ---- bank 4: LEADS ----
preset("VOCAL LEAD", B1="FORMANT", W1=1, MODE="VOCAL", OMAMT=40, mod={"LFO1": ("OMAMT", 0.3), "MODW": ("PITCH", 0.01)},
       L1=0.5, GLIDE=74, CUT=6000, DMIX=25)
preset("FORMANTS", B1="FORMANT", W1=1, B2="FORMANT", W2=4, MIX=30, mod={"LFO1": ("W1", 0.9)}, L1=0.35, GLIDE=72,
       CUT=5000, RES=20, DMIX=20)
preset("CRUSH LEAD", W1=5, MODE="CRUSH", OMAMT=70, mod={"MEG": ("OMAMT", 0.3)}, MD=0.4, CUT=5000, DIST=30)
preset("GLIDE LEAD", W1=4, W2=5, MIX=50, SPREAD=20, GLIDE=90, CUT=3200, RES=40, FEG=0.25, FD=0.4, FS=0.5,
       DMIX=30, DSYNC=127, DTIME=75, mod={"MODW": ("PITCH", 0.01)})
preset("PPG SWEEP", B1="DIGITAL1", W1=1, mod={"MEG": ("W1", 0.9)}, MA=0.4, MD=1.2, MS=0.3, CUT=8000, DMIX=25)
preset("SCREAMER", W1=5, W2=4, MODE="HSYNC", OMAMT=50, mod={"LFO1": ("OMAMT", 0.25)}, L1=5, DIST=90, CUT=4000,
       RES=30)
preset("WHISTLE", W1=1, GLIDE=84, CUT=9000, AA=0.05, AR=0.3, mod={"LFO1": ("PITCH", 0.016), "MODW": ("L1DEP", 0.5)},
       L1=5.5, DMIX=35, DFB=0.5)
preset("POLY LEAD", B1="POLYGON", W1=3, B2="POLYGON", W2=5, MIX=40, MODE="PM", OMAMT=35, mod={"LFO1": ("W1", 0.4)},
       L1=1.5, CUT=6000, GLIDE=70)
# ---- bank 5: CHORDS ----
preset("MAJOR STAB", W1=5, SHAPE="MAJOR", CUT=3000, RES=25, FEG=0.3, FD=0.3, FS=0, AD=0.5, AS=0, AR=0.3, DMIX=25)
preset("MINOR STAB", W1=4, W2=5, MIX=50, SHAPE="MINOR", CUT=2400, RES=30, FEG=0.35, FD=0.25, FS=0, AD=0.45, AS=0,
       AR=0.3, DMIX=30, DSYNC=127, DTIME=75)
preset("MAJ6 KEYS", B1="CURATED1", W1=1, SHAPE="MAJ6", CUT=3500, AD=1.2, AS=0.2, AR=0.6, DMIX=20)
preset("SUS STAB", W1=3, W2=5, MIX=50, SHAPE="SUS4", CUT=2600, RES=40, FEG=0.3, FD=0.25, FS=0, AD=0.4, AS=0, AR=0.3)
preset("FIFTHS", W1=5, SHAPE="FIFTHS", CUT=1800, RES=30, AR=0.4, mod={"LFO1": ("CUT", 0.15)}, L1=0.4)
preset("ORGAN", B1="CURATED1", W1=1, SHAPE="OCT12", CUT=7000, AR=0.1)
preset("DETROIT", B1="DIGITAL1", W1=2, SHAPE="MINOR", CUT=2000, RES=45, FEG=0.25, FD=0.3, FS=0, AD=0.6, AS=0,
       AR=0.4, DMIX=40, DFB=0.55, DSYNC=127, DTIME=60)
preset("DUB CHORD", W1=4, SHAPE="MINOR", CUT=1200, RES=50, FEG=0.25, FD=0.2, FS=0, AD=0.3, AS=0, AR=0.2,
       DMIX=55, DFB=0.75, DSYNC=127, DTIME=75, MORPH=40)
# ---- bank 6: DRONES + PADS ----
preset("DRONE", W1=5, W2=4, MIX=50, SPREAD=45, AEG=0, CUT=700, RES=40, mod={"LFO1": ("CUT", 0.2), "LFO2": ("W1", 0.2)},
       L1=0.1, L2=0.07)
preset("VOWEL PAD", B1="FORMANT", W1=2, B2="FORMANT", W2=4, MIX=50, SPREAD=35, AA=1.5, AR=2, mod={"LFO1": ("W1", 0.5)},
       L1=0.12, DMIX=30, DFB=0.6)
preset("GLASS DRONE", B1="CURATED1", W1=3, MODE="RING", OMAMT=50, TUNE=7, AEG=0, SPREAD=30, CUT=5000,
       mod={"LFO1": ("OMAMT", 0.3)}, L1=0.08, DMIX=40, DFB=0.7)
preset("NOISE WIND", W1=1, B2="NOISE", W2=4, MIX=127, CUT=800, RES=95, MORPH=64, AA=1.2, AR=2,
       mod={"LFO1": ("CUT", 0.4)}, L1=0.12, L1SHAPE=0)
preset("SLOW VOWELS", B1="FORMANT", W1=1, AA=2, AR=2.5, SPREAD=20, mod={"LFO1": ("W1", 0.95)}, L1=0.05, DMIX=35)
preset("DARK SWELL", W1=5, W2=5, MIX=50, SPREAD=55, OCT=-1, AA=2.5, AR=3, CUT=150, FEG=0.4, FA=3, FD=2, FS=0.6,
       DMIX=30, DFB=0.6)
preset("POLYGONS", B1="POLYGON", W1=1, B2="POLYGON", W2=5, MIX=50, SPREAD=30, AA=1, AR=1.5,
       mod={"LFO1": ("W1", 0.5), "LFO2": ("W2", 0.5)}, L1=0.15, L2=0.11, CUT=4000)
preset("SHIMMER", W1=1, W2=1, TUNE=12, MIX=40, SHAPE="OCT12", AA=1, AR=2, DMIX=60, DFB=0.85, DTIME=105,
       mod={"LFO1": ("DTIME", 0.02)}, L1=0.2)
# ---- bank 7: MODIFIERS (one each) ----
preset("DE-REZ", B1="MATH", W1=1, MODE="DEREZ", OMAMT=60, mod={"LFO1": ("OMAMT", 0.6)}, L1=0.3, CUT=8000)
preset("GLITCH SINE", W1=1, MODE="GLITCH", OMAMT=60, mod={"MEG": ("OMAMT", 0.5)}, MD=0.8, CUT=9000)
preset("WINDOW AMP", W1=1, MODE="WAMP", OMAMT=20, mod={"LFO1": ("OMAMT", 0.4)}, L1=0.25, CUT=7000)
preset("MIN MOD", W1=2, W2=5, MODE="MIN", OMAMT=127, TUNE=-12, FINE=0.05, CUT=4000)
preset("SINE FOLD", W1=1, MODE="SFOLD", OMAMT=30, mod={"VELO": ("OMAMT", 0.4), "MEG": ("OMAMT", 0.3)}, MD=0.4,
       AD=0.6, AS=0.3)
preset("SCRUNCH", B1="DIGITAL1", W1=3, MODE="SCRUNCH", OMAMT=50, mod={"LFO1": ("OMAMT", 0.5)}, L1=0.5)
preset("LO-FI PHASE", B1="MATH", W1=2, MODE="LOFI", OMAMT=70, CUT=6000, DMIX=20)
preset("WINDOW SYNC", W1=5, MODE="WSYNC", OMAMT=20, mod={"LFO1": ("OMAMT", 0.5)}, L1=0.2, CUT=6000)
# ---- bank 8: ARP-SEQ ----
preset("ARP PLUCK", ARP=127, W1=4, CUT=2500, RES=40, FEG=0.4, FD=0.15, FS=0, AD=0.25, AS=0, AR=0.15, DMIX=30,
       DSYNC=127, DTIME=75)
preset("PHRYG SEQ", ARP=127, steps=[0, 1, 3, 0, 7, 8, 1, 12, 0, R, 10, 7, 0, 1, R, 3], W1=5, W2=4, MIX=30, OCT=-1,
       CUT=700, RES=70, FEG=0.45, FD=0.15, FS=0.05, AD=0.2, AS=0.2, AR=0.1, DIST=40)
preset("ACID SEQ", ARP=127, steps=[0, 0, 12, 0, 3, 0, 15, 10], W1=5, OCT=-1, GLIDE=70, CUT=350, RES=110, FEG=0.5,
       FD=0.15, FS=0, AD=0.2, AS=0.4, DIST=45)
preset("OCTAVES", ARP=127, steps=[0, 12, 0, 24, 0, 12, 7, 19], W1=3, W2=5, MIX=40, CUT=3000, RES=30, AD=0.2, AS=0,
       AR=0.1, DMIX=25)
preset("DUB SEQ", ARP=127, steps=[0, R, R, 7, R, R, 10, R], W1=4, SHAPE="MINOR", CUT=1400, RES=55, FEG=0.3, FD=0.2,
       FS=0, AD=0.3, AS=0, DMIX=55, DFB=0.7, DSYNC=127, DTIME=75)
preset("TRIPLETS", ARP=127, ARPRATE=90, W1=1, W2=5, MODE="FM", OMAMT=35, TUNE=12, AD=0.2, AS=0, AR=0.1, DMIX=25)
preset("BLIPS", ARP=127, ARPRATE=110, steps=[0, 7, 12, 19, 24, 19, 12, 7], W1=1, MODE="CRUSH", OMAMT=90, AD=0.08,
       AS=0, AR=0.05, DMIX=35, DFB=0.6, DSYNC=127, DTIME=60)
preset("RAVE SEQ", ARP=127, steps=[0, R, 0, R, 3, R, 0, 5], W1=5, W2=4, MIX=40, SHAPE="MINOR", CUT=3200, RES=40,
       FEG=0.3, FD=0.2, FS=0, AD=0.3, AS=0, AR=0.2, DMIX=25, DSYNC=127, DTIME=60, TEMPO=170)

assert len(PRESETS) == 64, len(PRESETS)

# 0.4: the new filters and effects on the factory sounds they suit (added after, so the sounds above stay as made)
EXTRA = {
    "PWM LEAD": dict(CHMIX=0.45, CHRATE=30), "FM BELL": dict(VMODE="HALL", VMIX=0.3), "ROUND PAD": dict(CHMIX=0.5, VMODE="HALL", VMIX=0.35),
    "RING BELLS": dict(VMODE="HALO", VMIX=0.35), "DREAD": dict(FTYPE="LADDER"), "DONK": dict(FTYPE="TB303"),
    "REESE": dict(CHMIX=0.35, CHRATE=20, OTT=0.4), "ROLLER": dict(FTYPE="LADDER", OTT=0.3), "ACID": dict(FTYPE="TB303"),
    "FM GROWL": dict(FTYPE="MS20", BMODE="FM", BAMT=40), "808 BOOM": dict(ERA="SP1200", CRUSH=0.2),
    "HOOVER": dict(CHMIX=0.4, VMODE="ROOM", VMIX=0.2), "RAVE STAB": dict(ERA="SP1200", CRUSH=0.35, VMODE="ROOM", VMIX=0.2),
    "PHRYG STAB": dict(VMODE="HALL", VMIX=0.25), "DUB SIREN": dict(VMODE="DUB", VMIX=0.35),
    "NEURO": dict(FTYPE="DUALPEAK", OTT=0.6), "TECH STAB": dict(ERA="MPC60", CRUSH=0.3, VMODE="ROOM", VMIX=0.25),
    "WOBBLE": dict(FTYPE="LADDER", OTT=0.5), "MIN REESE": dict(CHMIX=0.3, OTT=0.4),
    "VOCAL LEAD": dict(FTYPE="FORMANT", MORPH=40, VMODE="HALL", VMIX=0.25), "FORMANTS": dict(VMODE="HALL", VMIX=0.3),
    "CRUSH LEAD": dict(ERA="AMIGA", CRUSH=0.4), "GLIDE LEAD": dict(CHMIX=0.35, VMODE="HALL", VMIX=0.2),
    "PPG SWEEP": dict(VMODE="HALO", VMIX=0.25), "SCREAMER": dict(FTYPE="MS20"), "WHISTLE": dict(VMODE="HALL", VMIX=0.3),
    "POLY LEAD": dict(BMODE="PM", BAMT=30, CHMIX=0.3), "MAJOR STAB": dict(VMODE="ROOM", VMIX=0.25),
    "MINOR STAB": dict(VMODE="HALL", VMIX=0.25, ERA="S950", CRUSH=0.25), "MAJ6 KEYS": dict(CHMIX=0.5, VMODE="HALL", VMIX=0.25),
    "SUS STAB": dict(VMODE="ROOM", VMIX=0.3), "FIFTHS": dict(PHMIX=0.5, PHRATE=20), "ORGAN": dict(CHMIX=0.6, CHRATE=70),
    "DETROIT": dict(VMODE="HALL", VMIX=0.2), "DUB CHORD": dict(VMODE="DUB", VMIX=0.3),
    "DRONE": dict(VMODE="ABYSS", VMIX=0.45, PHMIX=0.4, PHRATE=10), "VOWEL PAD": dict(VMODE="SWELL", VMIX=0.45, CHMIX=0.4),
    "GLASS DRONE": dict(VMODE="ABYSS", VMIX=0.5), "NOISE WIND": dict(FTYPE="COMB", MORPH=100, VMODE="HALL", VMIX=0.4),
    "SLOW VOWELS": dict(FTYPE="FORMANT", VMODE="SWELL", VMIX=0.4), "DARK SWELL": dict(VMODE="ABYSS", VMIX=0.4, CHMIX=0.3),
    "POLYGONS": dict(VMODE="HALL", VMIX=0.35, CHMIX=0.35), "SHIMMER": dict(VMODE="HALO", VMIX=0.5),
    "WINDOW SYNC": dict(PHMIX=0.4), "ARP PLUCK": dict(VMODE="ROOM", VMIX=0.25), "PHRYG SEQ": dict(FTYPE="TB303", OTT=0.4),
    "ACID SEQ": dict(FTYPE="TB303"), "DUB SEQ": dict(VMODE="DUB", VMIX=0.3), "BLIPS": dict(VMODE="HALO", VMIX=0.25),
    "RAVE SEQ": dict(ERA="SP1200", CRUSH=0.3, VMODE="ROOM", VMIX=0.2),
}
for _i, (_n, _b) in enumerate(PRESETS):
    if _n in EXTRA:
        _p = bytearray(_b)
        for _k, _v in EXTRA[_n].items():
            if _k == "FTYPE": _p[12 + P["FTYPE"]] = band_val(FTYPES.index(_v), 12)
            elif _k == "VMODE": _p[12 + P["VMODE"]] = band_val(VMODES.index(_v), 6)
            elif _k == "ERA": _p[12 + P["ERA"]] = band_val(ERAS.index(_v), 6)
            elif _k == "BMODE": _p[12 + P["BMODE"]] = band_val(BMODES.index(_v), 5)
            elif isinstance(_v, float): _p[12 + P[_k]] = round(_v * 127)
            else: _p[12 + P[_k]] = _v
        PRESETS[_i] = (_n, bytes(_p))


def meta():
    """the editor's tables: the parameters (from cr_patch.c), destinations, sources, banks, modifiers, presets"""
    params = []
    for m in re.finditer(r'\[CR_(\w+)\] = \{(\d+), CR_K_(\w+), (\d+), "([^"]*)"\}', SRC):
        params.append({"id": P[m.group(1)], "key": m.group(1), "cc": int(m.group(2)), "kind": m.group(3),
                       "def": int(m.group(4)), "name": m.group(5)})
    params.sort(key=lambda x: x["id"])
    names = lambda arr: re.findall(r'"([^"]*)"', SRC[SRC.index(arr):SRC.index("};", SRC.index(arr))])
    return {"params": params, "dests": names("CR_DEST_NAME[CR_ND]"), "srcs": names("CR_SRC_NAME[CR_NSRC]"),
            "mods": names("CR_OM_NAME[CR_NOM]"), "banks": names("CR_BANK_NAME[CR_BANKS]"),
            "shapes": names("CR_SHAPE_NAME[10]"), "scales": names("CR_SCALE_NAME[CR_NSCALE]"),
            "presets": [{"name": n, "bytes": list(b)} for n, b in PRESETS]}


def main():
    out = sys.argv[-1]
    if "--meta" in sys.argv:
        Path(out).write_text("// generated by tools/gen_presets.py --meta: do not edit\nexport default " +
                             json.dumps(meta(), separators=(",", ":")) + ";\n")
        return
    if "--keys" in sys.argv:                            # the plugins' stable parameter ids (append only)
        ps = meta()["params"]
        Path(out).write_text("/* generated by tools/gen_presets.py --keys: each parameter's key (CR_<key> in cr_patch.h) */\n"
                             "#pragma once\n"
                             f"static const char *const CR_KEY[{len(ps)}] = {{\n    " +
                             ", ".join(f'"{x["key"]}"' for x in ps) + "};\n")
        return
    if "--json" in sys.argv:
        Path(out).write_text(json.dumps([{"name": n, "bytes": list(b)} for n, b in PRESETS], separators=(",", ":")))
        return
    L = ["/* generated by tools/gen_presets.py: FM1-Craft's 64 factory presets (8 banks of 8) */", "#pragma once",
         "#include <stdint.h>", "static const uint8_t CR_FACTORY[64][128] = {"]
    for n, b in PRESETS:
        L.append(f"    {{{','.join(map(str, b))}}},   /* {n} */")
    L.append("};")
    Path(out).write_text("\n".join(L) + "\n")


if __name__ == "__main__":
    main()
