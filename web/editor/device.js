// SPDX-License-Identifier: GPL-3.0-only
// Crafty Synth editor: talking to the FM-1 over Web MIDI (Chrome / Edge). Parameters go as MIDI CCs (the CRAFTsynth's
// chart: the FM-1 sends the same CCs when its knobs turn, so the two follow each other); patches and wavetables go in
// Felucca's editor frames F0 7D 46 4C cmd ... F7 (docs/PROTOCOL.md; src/app/cr_editor.c answers them).
// Copyright (C) 2026 Jamie (TwistedGenetics).
export const CMD = { INFO: 40, PGET: 41, PPUT: 42, SELECT: 43, GGET: 44, GPUT: 45, WPUT: 46, WCOMMIT: 47, EVT: 48,
  WGET: 49, PRESET_RESET: 50 };
export const WORKING = 0x3fff, PATCH_BYTES = 128, UBANK_BYTES = 2576, CHUNK = 256, NFACTORY = 64, NUSER = 128, NUBANK = 32;
const slot2 = (s) => [s & 0x7f, (s >> 7) & 0x7f];
const HDR = [0xf0, 0x7d, 0x46, 0x4c];
export const FM1_PORT = /fm-?1|crafty|felucca/i;
// inside the AU / VST3 plugin (ports/plugin): JUCE's WebView gives window.__JUCE__, and the same MIDI goes to the
// plugin as "crafty-midi" events instead of out of a MIDI port
export const inPlugin = typeof window !== "undefined" && !!(window.__JUCE__ && window.__JUCE__.backend);

export function pack7(b) {
  const out = [];
  for (let i = 0; i < b.length; i += 7) {
    const k = Math.min(7, b.length - i);
    let m = 0;
    for (let j = 0; j < k; j++) m |= (b[i + j] >> 7) << j;
    out.push(m);
    for (let j = 0; j < k; j++) out.push(b[i + j] & 0x7f);
  }
  return out;
}
export function unpack7(a, max) {
  const out = [];
  let i = 0;
  while (i < a.length && out.length < max) {
    const m = a[i++];
    for (let j = 0; j < 7 && i < a.length && out.length < max; j++) out.push(a[i++] | (((m >> j) & 1) << 7));
  }
  return Uint8Array.from(out);
}

export class Device {
  constructor() {
    this.input = this.output = null;
    this.waiting = [];
    this.info = null;
    this.channel = 0;
    this.onchange = () => {};
    this.oncc = null;      // (cc, value): a knob on the FM-1
    this.onevent = null;   // a preset loaded or saved on the FM-1
    this.onnote = null;
  }
  get connected() { return !!(this.input && this.output); }
  get target() { return inPlugin ? "plugin" : "FM-1"; }
  async connectPlugin() {
    const be = window.__JUCE__.backend;
    if (!this.pluginListening) {
      be.addEventListener("crafty-midi", (b) => this.recv(Uint8Array.from(b)));
      this.pluginListening = true;
    }
    this.input = { plugin: true };
    this.output = { send: (f) => be.emitEvent("crafty-midi", Array.from(f)) };
    this.info = await this.getInfo();
    this.channel = 0;
    this.onchange();
    return this.info;
  }
  async connect(access) {
    const ins = [...access.inputs.values()], outs = [...access.outputs.values()];
    this.input = ins.find((p) => FM1_PORT.test(p.name)) || null;
    this.output = outs.find((p) => FM1_PORT.test(p.name)) || null;
    if (!this.connected) { this.onchange(); throw new Error("No FM-1 found. Plug it in, switch it on, and close other apps using its MIDI port."); }
    this.input.onmidimessage = (e) => this.recv(e.data);
    access.onstatechange = () => {
      if (this.input && this.input.state === "disconnected") { this.input = this.output = null; this.info = null; this.onchange(); }
    };
    this.info = await this.getInfo();
    const g = await this.send(CMD.GGET);
    this.channel = unpack7(g, 16)[1] & 15;
    this.onchange();
    return this.info;
  }
  recv(d) {
    if (d[0] === 0xf0 && d[1] === 0x7d && d[2] === 0x46 && d[3] === 0x4c) {
      const cmd = d[4], body = Array.from(d.slice(5, d.length - 1));
      if (cmd === CMD.EVT) { if (this.onevent) this.onevent({ kind: body[0], cur: body[1] | (body[2] << 7), dirty: body[3] }); return; }
      const i = this.waiting.findIndex((w) => w.cmd === cmd);
      if (i >= 0) { const w = this.waiting.splice(i, 1)[0]; clearTimeout(w.timer); w.resolve(body); }
      return;
    }
    if ((d[0] & 0xf0) === 0xb0 && this.oncc) this.oncc(d[1], d[2]);
    else if ((d[0] & 0xe0) === 0x80 && this.onnote) this.onnote(d);
  }
  send(cmd, args = [], timeout = 3000) {
    if (!this.connected) return Promise.reject(new Error("not connected"));
    const frame = [...HDR, cmd, ...args.map((x) => x & 0x7f), 0xf7];
    return new Promise((resolve, reject) => {
      const w = { cmd, resolve, timer: setTimeout(() => {
        this.waiting = this.waiting.filter((x) => x !== w);
        reject(new Error(inPlugin ? "The plugin didn't answer." : "The FM-1 didn't answer. Is Crafty Synth installed on it?"));
      }, timeout) };
      this.waiting.push(w);
      this.output.send(frame);
    });
  }
  cc(cc, v) { if (this.connected) this.output.send([0xb0 | this.channel, cc & 0x7f, v & 0x7f]); }
  note(n, v) { if (this.connected) this.output.send([(v ? 0x90 : 0x80) | this.channel, n & 0x7f, v & 0x7f]); }
  async getInfo() {
    const b = await this.send(CMD.INFO);
    let i = 0, name = "";
    while (i < b.length && b[i]) name += String.fromCharCode(b[i++]);
    i++;
    const proto = b[i];
    if (proto < 2) throw new Error("This FM-1 runs an older Crafty Synth: install 0.4 or later with the flasher.");
    const nbank = b[i + 7];
    return { name, proto, nfactory: b[i + 1], nuser: b[i + 2] | (b[i + 3] << 7), cur: b[i + 4] | (b[i + 5] << 7),
      dirty: b[i + 6], ubank: Array.from(b.slice(i + 8, i + 8 + nbank), (x) => !!x) };
  }
  async getPatch(slot) {
    const b = await this.send(CMD.PGET, slot2(slot));
    return { slot: b[0] | (b[1] << 7), saved: b[2], bytes: unpack7(b.slice(3), PATCH_BYTES) };
  }
  async putPatch(slot, bytes) {
    const b = await this.send(CMD.PPUT, [...slot2(slot), ...pack7(bytes)], 6000);
    if (b[2] === 2) throw new Error("factory presets can't be overwritten: save to a user slot (U1-1 and up)");
    if (b[2] !== 0) throw new Error(`the ${this.target} couldn't store it`);
  }
  async select(slot) { await this.send(CMD.SELECT, slot2(slot)); }
  async resetPreset(slot) { await this.send(CMD.PRESET_RESET, slot2(slot), 6000); }
  async getBank(u) {                       // a user bank from flash: { name, wave } or null if it's empty
    const out = new Uint8Array(2576);
    let n = 0;
    for (let c = 0; c * CHUNK < out.length; c++) {
      const len = Math.min(CHUNK, out.length - c * CHUNK), b = await this.send(CMD.WGET, [u, c]);
      if (b.length <= 2) return null;
      out.set(unpack7(b.slice(2), len), c * CHUNK);
      n += len;
    }
    if (n !== out.length || out[0] !== 0xc7 || out[1] !== 1) return null;
    let name = "";
    for (let i = 2; i < 15 && out[i]; i++) name += String.fromCharCode(out[i]);
    const dv = new DataView(out.buffer), wave = new Int16Array(5 * 256);
    for (let i = 0; i < wave.length; i++) wave[i] = dv.getInt16(16 + 2 * i, true);
    return { name, wave };
  }
  async putBank(u, bytes) {
    for (let c = 0; c * CHUNK < bytes.length; c++)
      await this.send(CMD.WPUT, [u, c, ...pack7(bytes.slice(c * CHUNK, (c + 1) * CHUNK))]);
    const r = await this.send(CMD.WCOMMIT, [u], 8000);
    if (r[1] !== 0) throw new Error(r[1] === 2 ? `the ${this.target} got a broken bank` : `the ${this.target} couldn't store it`);
  }
}
