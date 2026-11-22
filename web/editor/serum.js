// SPDX-License-Identifier: GPL-3.0-only
// Crafty Synth editor: wavetables from WAV files (Serum's, Vital's, or any single-cycle WAV) into a user bank: five
// frames picked from the table, each band-limited to 128 harmonics and resampled to 256 samples (the firmware's
// format), the bank normalised. Copyright (C) 2026 Jamie (TwistedGenetics).
export const N = 256, PEAK = 26000, UBANK_MAGIC = 0xc7;

/* a WAV file's first channel as floats, and the frame size its 'clm ' chunk gives (Serum writes "<!>2048") */
export function parseWav(buf) {
  const dv = new DataView(buf);
  const tag = (o) => String.fromCharCode(dv.getUint8(o), dv.getUint8(o + 1), dv.getUint8(o + 2), dv.getUint8(o + 3));
  if (tag(0) !== "RIFF" || tag(8) !== "WAVE") throw new Error("not a WAV file");
  let o = 12, fmt = null, data = null, clm = 0;
  while (o + 8 <= buf.byteLength) {
    const id = tag(o), len = dv.getUint32(o + 4, true), body = o + 8;
    if (id === "fmt ") fmt = { format: dv.getUint16(body, true), ch: dv.getUint16(body + 2, true), bits: dv.getUint16(body + 14, true) };
    else if (id === "data") data = { off: body, len: Math.min(len, buf.byteLength - body) };
    else if (id === "clm ") {
      const s = new TextDecoder().decode(new Uint8Array(buf, body, Math.min(len, 16)));
      const m = s.match(/<!>(\d+)/);
      if (m) clm = +m[1];
    }
    o = body + len + (len & 1);
  }
  if (!fmt || !data) throw new Error("the WAV has no format or no data");
  const bps = fmt.bits / 8, step = bps * fmt.ch, n = Math.floor(data.len / step), x = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const p = data.off + i * step;
    if (fmt.format === 3 && fmt.bits === 32) x[i] = dv.getFloat32(p, true);
    else if (fmt.bits === 16) x[i] = dv.getInt16(p, true) / 32768;
    else if (fmt.bits === 24) x[i] = ((dv.getUint8(p) | (dv.getUint8(p + 1) << 8) | (dv.getInt8(p + 2) << 16))) / 8388608;
    else if (fmt.bits === 32) x[i] = dv.getInt32(p, true) / 2147483648;
    else if (fmt.bits === 8) x[i] = (dv.getUint8(p) - 128) / 128;
    else throw new Error(`${fmt.bits}-bit WAVs aren't read`);
  }
  let size = clm || 2048;
  if (x.length < size) size = x.length;                 // one cycle, whatever its length
  return { samples: x, frameSize: size, frames: Math.max(1, Math.floor(x.length / size)) };
}

/* one frame (any length) to 256 samples with at most 128 harmonics, DC removed */
export function resampleFrame(src) {
  const L = src.length, out = new Float32Array(N);
  for (let k = 1; k <= 128 && k < L / 2; k++) {
    let re = 0, im = 0;
    for (let i = 0; i < L; i++) {
      const a = (2 * Math.PI * k * i) / L;
      re += src[i] * Math.cos(a);
      im += src[i] * Math.sin(a);
    }
    re *= 2 / L; im *= 2 / L;
    if (k === 128) { re *= 0.5; im = 0; }
    if (re * re + im * im < 1e-12) continue;
    for (let i = 0; i < N; i++) {
      const a = (2 * Math.PI * k * i) / N;
      out[i] += re * Math.cos(a) + im * Math.sin(a);
    }
  }
  return out;
}

/* five frames of a table (their indices) to a bank: Int16Array(5 * 256), normalised to the firmware's peak */
export function makeBank(table, picks) {
  const waves = picks.map((f) => {
    const s = Math.min(Math.max(0, f | 0), table.frames - 1) * table.frameSize;
    return resampleFrame(table.samples.subarray(s, s + table.frameSize));
  });
  let peak = 0;
  for (const w of waves) for (const v of w) peak = Math.max(peak, Math.abs(v));
  const g = peak > 0 ? PEAK / peak : 0, out = new Int16Array(5 * N);
  waves.forEach((w, k) => w.forEach((v, i) => { out[k * N + i] = Math.round(v * g); }));
  return out;
}

export function defaultPicks(frames) {
  return [0, 1, 2, 3, 4].map((k) => Math.round((k * (frames - 1)) / 4));
}

/* the FM-1's user bank record: magic, version, a name (13 + 0), then the 5 x 256 samples, little-endian */
export function bankBytes(name, wave16) {
  const b = new Uint8Array(2576);
  b[0] = UBANK_MAGIC;
  b[1] = 1;
  const nm = name.toUpperCase().replace(/[^ -_]/g, "").slice(0, 13);
  for (let i = 0; i < nm.length; i++) b[2 + i] = nm.charCodeAt(i);
  const dv = new DataView(b.buffer);
  for (let i = 0; i < 5 * N; i++) dv.setInt16(16 + 2 * i, wave16[i], true);
  return b;
}
