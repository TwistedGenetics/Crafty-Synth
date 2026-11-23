// SPDX-License-Identifier: GPL-3.0-only
// Crafty Synth editor: wavetables from WAV files (Serum's, Vital's, or any single-cycle WAV) into a user bank, by the
// engine's own importer (src/host/cr_wav.c through crafty.wasm): five frames picked from the table, each band-limited
// to 128 harmonics and resampled to 256 samples (the firmware's format), the bank normalised. Copyright (C) 2026 Jamie (TwistedGenetics).
export const N = 256, PEAK = 26000, UBANK_MAGIC = 0xc7;

/* the importer is the engine's own (src/host/cr_wav.c, in crafty.wasm), the one every version uses: open a WAV file
 * (an ArrayBuffer) with the editor's engine instance; makeBank then picks five of its frames */
export function parseWav(buf, wasm) {
  if (!wasm) throw new Error("the editor's engine isn't loaded yet");
  const at = wasm.cw_heap(), need = at + buf.byteLength, have = wasm.memory.buffer.byteLength;
  if (need > have) wasm.memory.grow(Math.ceil((need - have) / 65536));
  new Uint8Array(wasm.memory.buffer, at, buf.byteLength).set(new Uint8Array(buf));
  const frames = wasm.cw_wav_open(buf.byteLength);
  if (!frames) throw new Error("not a WAV this reads (PCM 8-32 bit or 32-bit float, frames of up to 4096 samples)");
  return { wasm, frames, frameSize: wasm.cw_wav_frame_size() };
}

/* five frames of the open table (their indices) to a bank: Int16Array(5 * 256), normalised to the firmware's peak */
export function makeBank(table, picks) {
  const w = table.wasm, p = picks.map((f) => Math.min(Math.max(0, f | 0), table.frames - 1));
  const at = w.cw_wav_bank(p[0], p[1], p[2], p[3], p[4]);
  return at ? new Int16Array(w.memory.buffer, at, 5 * N).slice() : new Int16Array(5 * N);
}

export function defaultPicks(frames) {
  return [0, 1, 2, 3, 4].map((k) => Math.round((k * (frames - 1)) / 4));
}

/* the FM-1's user bank record: magic, version, a name (13 + 0), then the 5 x 256 samples, little-endian */
export function bankBytes(name, wave16) {
  const b = new Uint8Array(2576);
  b[0] = UBANK_MAGIC;
  b[1] = 1;
  const nm = bankName(name);
  for (let i = 0; i < nm.length; i++) b[2 + i] = nm.charCodeAt(i);
  const dv = new DataView(b.buffer);
  for (let i = 0; i < 5 * N; i++) dv.setInt16(16 + 2 * i, wave16[i], true);
  return b;
}

/* a bank's name: capitals, the screen's characters, 13 at most (cr_wav_name's rule; a file's ".wav" dropped) */
export function bankName(s) {
  return s.replace(/\.wav$/i, "").toUpperCase().replace(/[^ -_]/g, "").slice(0, 13);
}

