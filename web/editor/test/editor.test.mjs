// SPDX-License-Identifier: GPL-3.0-only
// Crafty Synth editor: WAV wavetables in (a Serum-style file made here), the bank record, the SysEx packing, the values.
import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "node:fs";
import { parseWav, makeBank, defaultPicks, bankBytes, bankName, N, PEAK } from "../serum.js";
import { pack7, unpack7 } from "../device.js";
import { fmt, bankOf, bankVal, semiOf, PI } from "../format.js";
import M from "../meta.js";

const engine = async () => (await WebAssembly.instantiate(readFileSync(new URL("../crafty.wasm", import.meta.url)), {})).instance.exports;

function wav(frames, size, clm = true) {
  const n = frames * size, data = 44 + (clm ? 24 : 0), b = new ArrayBuffer(data + n * 4), dv = new DataView(b);
  const s = (o, t) => [...t].forEach((c, i) => dv.setUint8(o + i, c.charCodeAt(0)));
  s(0, "RIFF"); dv.setUint32(4, b.byteLength - 8, true); s(8, "WAVE"); s(12, "fmt "); dv.setUint32(16, 16, true);
  dv.setUint16(20, 3, true); dv.setUint16(22, 1, true); dv.setUint32(24, 44100, true); dv.setUint32(28, 176400, true);
  dv.setUint16(32, 4, true); dv.setUint16(34, 32, true);
  let o = 36;
  if (clm) { s(o, "clm "); dv.setUint32(o + 4, 16, true); s(o + 8, "<!>" + size + " 0000000"); o += 24; }
  s(o, "data"); dv.setUint32(o + 4, n * 4, true);
  for (let f = 0; f < frames; f++)
    for (let i = 0; i < size; i++)   // frame f: harmonic f + 1
      dv.setFloat32(o + 8 + (f * size + i) * 4, Math.sin((2 * Math.PI * (f + 1) * i) / size), true);
  return b;
}

test("a Serum-style table: frames found, picked, band-limited to 256 samples", async () => {
  const t = parseWav(wav(8, 2048), await engine());
  assert.equal(t.frameSize, 2048);
  assert.equal(t.frames, 8);
  const picks = defaultPicks(t.frames);
  assert.deepEqual(picks, [0, 2, 4, 5, 7]);
  const b = makeBank(t, picks);
  assert.equal(b.length, 5 * N);
  // wave 1 is frame 1: one sine cycle, peak at a quarter
  assert.ok(Math.abs(b[64] - PEAK) < 300, `peak ${b[64]}`);
  // wave 5 is frame 8: eight cycles
  let crossings = 0;
  for (let i = 1; i < N; i++) if ((b[4 * N + i - 1] < 0) !== (b[4 * N + i] < 0)) crossings++;
  assert.ok(crossings >= 15 && crossings <= 17, `crossings ${crossings}`);
});

test("a single-cycle WAV with no clm chunk is one frame", async () => {
  const w = await engine(), t = parseWav(wav(1, 600, false), w);
  assert.equal(t.frames, 1);
  assert.equal(t.frameSize, 600);
  assert.throws(() => parseWav(new ArrayBuffer(64), w), /not a WAV/);
  assert.equal(bankName("Growl bass.wav"), "GROWL BASS");
});

test("Random Patch through the engine: a saved slot, a varied preset, the banks kept", async () => {
  const w = await engine();
  w.cw_init();
  const saved = new Uint8Array(w.memory.buffer, w.cw_rand_saved(), 192);
  saved.fill(0); for (let i = 0; i < 64; i++) saved[i] = 1; saved[100] = 1;
  w.cw_rand_seed(99);
  for (let n = 0; n < 200; n++) {
    const slot = w.cw_rand_slot();
    assert.ok(slot < 64 || slot === 100, `slot ${slot}`);
  }
  const fac = new Uint8Array(w.memory.buffer, w.cw_factory(), 64 * 128);
  const P = new Uint8Array(w.memory.buffer, w.cw_patch(), 128), C = new Uint8Array(w.memory.buffer, w.cw_rand_cur(), 128);
  P.set(fac.subarray(3 * 128, 4 * 128)); C.set(P); C[12 + PI.W1BANK] = 120; C[12 + PI.W2BANK] = 121;
  w.cw_rand_vary(1);
  assert.equal(P[12 + PI.W1BANK], 120);
  assert.equal(P[12 + PI.W2BANK], 121);
  assert.equal(P[12 + PI.VOL], fac[3 * 128 + 12 + PI.VOL]);
  let moved = 0;
  for (let i = 0; i < 82; i++) if (P[12 + i] !== fac[3 * 128 + 12 + i]) moved++;
  assert.ok(moved > 10, `moved ${moved}`);
});

test("the bank record is the firmware's 2,576 bytes", () => {
  const b = bankBytes("my reese!", new Int16Array(5 * N).fill(-2));
  assert.equal(b.length, 2576);
  assert.equal(b[0], 0xc7);
  assert.equal(String.fromCharCode(...b.slice(2, 11)), "MY REESE!");
  assert.equal(b[16], 0xfe);
  assert.equal(b[17], 0xff);
});

test("7-to-8 packing round trips", () => {
  const b = Uint8Array.from({ length: 96 }, (_, i) => (i * 37) & 255);
  assert.deepEqual(unpack7(pack7(b), 96), b);
});

test("values read as the FM-1 shows them", () => {
  for (let b = 0; b < 11; b++) assert.equal(bankOf(bankVal(b, true), true), b);
  for (const b of [0, 1, 7, 9, 10]) assert.equal(bankOf(bankVal(b, false), false), b);
  assert.equal(semiOf(64), 0);
  assert.equal(semiOf(127), 48);
  const p = new Uint8Array(64);
  assert.equal(fmt(M.params[PI.CUT], 127, p), "20.5 kHz");
  assert.equal(fmt(M.params[PI.GLIDE], 64, p), "OFF");
  assert.equal(fmt(M.params[PI.OMMODE], 9 * 8, p), "HARD SYNC");
  assert.equal(M.presets.length, 64);
});
