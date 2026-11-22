// SPDX-License-Identifier: GPL-3.0-only
// Crafty Synth editor: the audio thread. The firmware's engine (crafty.wasm, the same C) renders 128 frames a call.
// Copyright (C) 2026 Jamie (TwistedGenetics).
class Crafty extends AudioWorkletProcessor {
  constructor() {
    super();
    this.e = null;
    this.q = [];
    this.port.onmessage = async (m) => {
      const d = m.data;
      if (d.t === "init") {
        const { instance } = await WebAssembly.instantiate(d.wasm, {});
        this.e = instance.exports;
        this.e.cw_init();
        for (const x of this.q) this.handle(x);
        this.q = [];
        this.port.postMessage({ t: "ready" });
      } else if (!this.e) this.q.push(d);
      else this.handle(d);
    };
  }
  mem() { return new Uint8Array(this.e.memory.buffer); }
  handle(d) {
    const e = this.e;
    switch (d.t) {
      case "patch": this.mem().set(d.bytes, e.cw_patch()); e.cw_apply(); break;
      case "param": e.cw_param(d.id, d.v); break;
      case "key": e.cw_key(d.n, d.v); break;
      case "alloff": e.cw_all_off(); break;
      case "ctl": e.cw_ctl(d.src, d.v); break;
      case "bend": e.cw_bend(d.v); break;
      case "ubank": {
        const p = e.cw_ubank(d.u);
        new Int16Array(e.memory.buffer, p, 5 * 256).set(d.data);
        e.cw_ubank_changed(d.u);
        break;
      }
    }
  }
  process(inputs, outputs) {
    const out = outputs[0];
    if (!this.e || !out.length) return true;
    const n = out[0].length;
    for (let k = 0; k < n; k += 128) {
      const m = Math.min(128, n - k);
      const p = this.e.cw_render(m);
      const f = new Float32Array(this.e.memory.buffer, p, 2 * m);
      for (let i = 0; i < m; i++) {
        out[0][k + i] = f[2 * i];
        if (out[1]) out[1][k + i] = f[2 * i + 1];
      }
    }
    return true;
  }
}
registerProcessor("crafty", Crafty);
