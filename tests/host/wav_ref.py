# SPDX-License-Identifier: GPL-3.0-only
# Crafty Synth: the shared WAV importer (src/host/cr_wav.c, run as build/host/wav_test) checked against the editor's
# algorithm in double precision (numpy): a Serum table (float, 'clm '), a 16-bit single cycle, a 24-bit stereo one, an
# 8-bit one and a WAVE_FORMAT_EXTENSIBLE float. Copyright (C) 2026 Jamie (TwistedGenetics).
import struct, subprocess, sys
from pathlib import Path
import numpy as np

out = Path(sys.argv[1]); exe = sys.argv[2]
N, PEAK = 256, 26000

def wav(path, x, bits, fmt=1, ch=1, clm=None, ext=False):
    x = np.repeat(x[:, None], ch, axis=1).reshape(-1) if ch > 1 else x
    if fmt == 3: data = x.astype("<f4").tobytes()
    elif bits == 8: data = np.clip(np.round(x * 127 + 128), 0, 255).astype("u1").tobytes()
    elif bits == 16: data = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2").tobytes()
    elif bits == 24:
        v = np.clip(np.round(x * 8388607), -8388608, 8388607).astype("<i4")
        data = b"".join(int(a).to_bytes(4, "little", signed=True)[:3] for a in v)
    else: data = np.clip(np.round(x * 2147483647), -2**31, 2**31 - 1).astype("<i4").tobytes()
    tag = 0xFFFE if ext else fmt
    body = struct.pack("<HHIIHH", tag, ch, 44100, 44100 * ch * bits // 8, ch * bits // 8, bits)
    if ext: body += struct.pack("<HHI", 22, bits, 0) + struct.pack("<H", fmt) + b"\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71"
    chunks = b"fmt " + struct.pack("<I", len(body)) + body
    if clm: c = f"<!>{clm} 10000000 wwfx".encode(); chunks += b"clm " + struct.pack("<I", len(c)) + c + (b"\0" if len(c) & 1 else b"")
    chunks += b"data" + struct.pack("<I", len(data)) + data + (b"\0" if len(data) & 1 else b"")
    path.write_bytes(b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks)

def ref(x, size, frames):
    picks = [int(np.floor(k * (frames - 1) / 4 + 0.5)) for k in range(5)]
    waves = []
    for f in picks:
        s = x[f * size:(f + 1) * size]; L = len(s); o = np.zeros(N); i = np.arange(L); j = np.arange(N)
        for k in range(1, 129):
            if not k < L / 2: break
            re = 2 / L * np.sum(s * np.cos(2 * np.pi * k * i / L)); im = 2 / L * np.sum(s * np.sin(2 * np.pi * k * i / L))
            if k == 128: re *= 0.5; im = 0
            if re * re + im * im < 1e-12: continue
            o += re * np.cos(2 * np.pi * k * j / N) + im * np.sin(2 * np.pi * k * j / N)
        waves.append(o)
    w = np.array(waves); return np.round(w * PEAK / np.abs(w).max())

rng = np.random.default_rng(7)
t = np.arange(2048) / 2048
serum = np.concatenate([np.sin(2 * np.pi * t) * (1 - f / 63) + 0.6 * f / 63 * (2 * t - 1) + 0.05 * rng.standard_normal(2048) for f in range(64)])
cyc = np.sign(np.sin(2 * np.pi * np.arange(600) / 600)) * 0.8 + 0.1
cases = [("serum.wav", serum, dict(bits=32, fmt=3, clm=2048), 2048),
         ("cycle16.wav", cyc, dict(bits=16), 600),
         ("cycle24st.wav", cyc * 0.5, dict(bits=24, ch=2), 600),
         ("cycle8.wav", cyc * 0.7, dict(bits=8), 600),
         ("ext.wav", serum[:2048 * 8], dict(bits=32, fmt=3, ext=True), 2048)]
worst = 0
for name, x, kw, size in cases:
    p = out / name; wav(p, x.astype(float), **kw)
    r = subprocess.run([exe, str(p), str(out / (name + ".bin"))], capture_output=True, text=True)
    if r.returncode: sys.exit(f"FAIL wav {name}: {r.stdout}{r.stderr}")
    frames, fs = map(int, r.stdout.split())
    # the reference reads the samples as the file holds them (quantised)
    raw = p.read_bytes(); d = raw.index(b"data") + 8; n = struct.unpack("<I", raw[d - 4:d])[0]; ch = kw.get("ch", 1)
    if kw.get("fmt") == 3: q = np.frombuffer(raw[d:d + n], "<f4").astype(float)
    elif kw["bits"] == 8: q = (np.frombuffer(raw[d:d + n], "u1").astype(float) - 128) / 128
    elif kw["bits"] == 16: q = np.frombuffer(raw[d:d + n], "<i2") / 32768
    else: b = np.frombuffer(raw[d:d + n], "u1").reshape(-1, 3).astype(np.int32); q = ((b[:, 0] << 8 | b[:, 1] << 16 | b[:, 2] << 24) >> 8) / 8388608
    q = q[::ch]
    want = ref(q, size, len(q) // size)
    got = np.frombuffer((out / (name + ".bin")).read_bytes(), "<i2").reshape(5, N)
    e = int(np.abs(got - want).max()); worst = max(worst, e)
    if fs != size or frames != len(q) // size or e > 3: sys.exit(f"FAIL wav {name}: {frames} x {fs}, off by {e}")
print(f"ok   wav importer: {len(cases)} files match the editor's algorithm (within {worst} of 26000)")
