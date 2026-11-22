// SPDX-License-Identifier: GPL-3.0-only
// Crafty Synth: the editor's engine (the firmware's C as WebAssembly) plays every factory preset.
import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "node:fs";

const wasm = readFileSync(new URL("../crafty.wasm", import.meta.url));

test("every factory preset sounds in the WebAssembly engine", async () => {
  const { instance } = await WebAssembly.instantiate(wasm, {});
  const e = instance.exports;
  e.cw_init();
  const mem = () => new Uint8Array(e.memory.buffer);
  const fac = e.cw_factory();
  for (let n = 0; n < 64; n++) {
    mem().set(mem().slice(fac + n * 96, fac + n * 96 + 96), e.cw_patch());
    e.cw_apply();
    e.cw_key(48, 100);
    let peak = 0;
    for (let i = 0; i < 200; i++) {
      const p = e.cw_render(128) / 4;
      const f = new Float32Array(e.memory.buffer, p * 4, 256);
      for (const x of f) {
        assert.ok(Number.isFinite(x), `preset ${n + 1}: not a number`);
        peak = Math.max(peak, Math.abs(x));
      }
    }
    e.cw_key(48, 0);
    e.cw_all_off();
    assert.ok(peak > 0.05 && peak <= 1, `preset ${n + 1}: peak ${peak}`);
  }
});
