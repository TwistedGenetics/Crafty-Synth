// SPDX-License-Identifier: GPL-3.0-only
// Crafty Synth editor: the pages (as the FM-1's), the preview (the firmware's engine in WebAssembly), the FM-1 over
// Web MIDI, user wavetables from WAV files. Copyright (C) 2026 Jamie (TwistedGenetics).
import M from "./meta.js";
import { fmt, choices, PI, bankOf } from "./format.js";
import { Device } from "./device.js";
import { parseWav, makeBank, defaultPicks, bankBytes, N, PEAK } from "./serum.js";

const $ = (id) => document.getElementById(id);
const el = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) k === "class" ? (e.className = v) : (e[k] = v);
  for (const k of kids) e.append(k);
  return e;
};

/* ---------------------------------------------------------------------------------------------- state --- */
const patch = new Uint8Array(96);     // the working patch: name[12], p[64], steps, magic, ver, root, step[16]
const P = (k) => patch[12 + PI[k]];
const ubank = [null, null];           // Int16Array(5 * 256) each, or null (bank 1 stands in)
let cur = 0;
const dev = new Device();
let ctx = null, node = null, wasm = null;

function post(m) { if (node) node.port.postMessage(m); }
function loadBytes(b) {
  patch.set(b);
  post({ t: "patch", bytes: patch.slice() });
  render();
}
function factory(n) { loadBytes(Uint8Array.from(M.presets[n].bytes)); }
function setParam(id, v, fromDevice = false) {
  v = Math.max(0, Math.min(127, v | 0));
  patch[12 + id] = v;
  post({ t: "param", id, v });
  if (!fromDevice) dev.cc(M.params[id].cc, v);
  refresh();
}

/* ---------------------------------------------------------------------------------------------- pages --- */
const PAGES = [
  ["OSC", ["W1POS", "W2POS", "MIX", "OMAMT"], "waves"],
  ["WAVES", ["W1BANK", "W2BANK", "OMMODE", "SPREAD"]],
  ["TUNE", ["TUNE", "FINE", "OCT", "GLIDE"]],
  ["FILTER", ["CUT", "RES", "MORPH", "FEGAMT"]],
  ["FILTER EG", ["FA", "FD", "FS", "FR"]],
  ["AMP EG", ["AA", "AD", "AS", "AR", "AEGAMT"]],
  ["MOD EG", ["MA", "MD", "MS", "MR"]],
  ["LFO 1", ["L1RATE", "L1SHAPE", "L1MODE", "L1SYNC"]],
  ["LFO 2", ["L2RATE", "L2SHAPE", "L2MODE", "L2SYNC"]],
  ["MATRIX", [], "matrix"],
  ["FX", ["DIST", "DMIX", "DTIME", "DFB", "DSYNC", "VOL"]],
  ["ARP", ["ARP", "TEMPO", "ARPRATE"]],
];
const ctls = [];

function control(key) {
  const p = M.params[PI[key]];
  const box = el("div", { class: "ctl" });
  const val = el("span", { class: "val" });
  box.append(el("label", { textContent: p.name }), val);
  const ch = choices(p);
  let input;
  if (ch) {
    input = el("select");
    for (const [v, n] of ch) input.append(el("option", { value: v, textContent: n }));
    input.onchange = () => setParam(p.id, +input.value);
    box.append(input);
    val.hidden = true;
  } else {
    input = el("input", { type: "range", min: 0, max: 127, step: 1 });
    input.oninput = () => setParam(p.id, +input.value);
    box.append(input);
  }
  ctls.push({ p, box, val, input, ch });
  return box;
}

function wavePanel() {
  const w = el("div", { class: "waves" });
  for (const k of [1, 2]) {
    const c = el("canvas", { class: "wave", width: 300, height: 90, id: `wv${k}` });
    w.append(el("div", {}, el("b", { textContent: `WAVE ${k}` }), c));
  }
  return w;
}

function matrixPanel() {
  const t = el("table", { class: "matrix" });
  M.srcs.forEach((s, i) => {
    const sel = el("select", { id: `md${i}` });
    M.dests.forEach((d, j) => sel.append(el("option", { value: j, textContent: d })));
    sel.onchange = () => setParam(PI.DST0 + i, +sel.value);
    const r = el("input", { type: "range", min: 1, max: 127, id: `mp${i}` });
    r.oninput = () => setParam(PI.DEP0 + i, +r.value);
    t.append(el("tr", {}, el("td", { textContent: s }), el("td", {}, sel), el("td", {}, r), el("td", { class: "dv", id: `mv${i}` })));
  });
  return t;
}

function build() {
  const pages = $("pages");
  for (const [name, keys, extra] of PAGES) {
    const sec = el("section", { class: "panel" }, el("h2", { textContent: name }));
    if (extra === "waves") sec.append(wavePanel());
    if (extra === "matrix") sec.append(matrixPanel());
    for (const k of keys) sec.append(control(k));
    pages.append(sec);
  }
  M.presets.forEach((p, i) => {
    const label = `${(i >> 3) + 1}-${(i & 7) + 1} ${p.name}`;
    $("preset").append(el("option", { value: i, textContent: label }));
    $("slot").append(el("option", { value: i, textContent: `${(i >> 3) + 1}-${(i & 7) + 1}` }));
  });
}

/* ------------------------------------------------------------------------------------------- drawing --- */
function waveData(bank) {
  if (bank >= 9) { const u = ubank[bank - 9]; if (u) return u; bank = 0; }
  if (!wasm) return null;
  return new Int16Array(wasm.memory.buffer, wasm.cw_wave(bank), 5 * N);
}
function drawWave(c, bank, pos, color) {
  const g = c.getContext("2d"), w = c.width, h = c.height, d = waveData(bank);
  g.clearRect(0, 0, w, h);
  g.strokeStyle = "#bebab0";
  g.beginPath(); g.moveTo(0, h / 2); g.lineTo(w, h / 2); g.stroke();
  if (!d) return;
  const m = (pos * 4) / 127, a = Math.min(3, Math.floor(m)), f = m - a;
  g.strokeStyle = color; g.lineWidth = 3; g.beginPath();
  for (let x = 0; x <= w; x++) {
    const k = Math.min(255, Math.floor((x / w) * 256));
    const v = d[a * N + k] + (d[(a + 1) * N + k] - d[a * N + k]) * f;
    const y = h / 2 - (v / PEAK) * h * 0.44;
    x ? g.lineTo(x, y) : g.moveTo(x, y);
  }
  g.stroke();
}
function drawSingle(c, d) {
  const g = c.getContext("2d"), w = c.width, h = c.height;
  g.clearRect(0, 0, w, h);
  g.strokeStyle = "#005aaa"; g.lineWidth = 2; g.beginPath();
  for (let x = 0; x <= w; x++) {
    const v = d[Math.min(N - 1, Math.floor((x / w) * N))];
    const y = h / 2 - (v / PEAK) * h * 0.44;
    x ? g.lineTo(x, y) : g.moveTo(x, y);
  }
  g.stroke();
}

function refresh() {
  const p = patch.subarray(12, 76);
  for (const c of ctls) {
    const v = p[c.p.id];
    if (c.ch) {
      let best = c.ch[0][0], bd = 999;
      for (const [cv] of c.ch) if (Math.abs(cv - v) < bd || (c.p.kind === "DEST" && cv === v)) { bd = Math.abs(cv - v); best = cv; }
      if (c.p.kind === "OMMODE") best = (v >> 3) * 8 + 4;
      if (c.p.kind === "BANK") best = c.ch.find(([cv]) => bankOf(cv, c.p.key === "W2BANK") === bankOf(v, c.p.key === "W2BANK"))[0];
      if (c.p.kind === "OCT") best = c.ch[((v * 7) >> 7)][0];
      if (c.p.kind === "ARPRATE") best = c.ch[(v * 6) >> 7][0];
      if (c.p.kind === "LMODE") best = v < 42 ? 20 : v < 84 ? 63 : 105;
      if (c.p.kind === "ONOFF") best = v >= 64 ? 127 : 0;
      c.input.value = best;
    } else c.input.value = v;
    c.val.textContent = fmt(c.p, v, p);
  }
  for (let i = 0; i < 8; i++) {
    $(`md${i}`).value = p[PI.DST0 + i];
    $(`mp${i}`).value = p[PI.DEP0 + i];
    $(`mv${i}`).textContent = (p[PI.DEP0 + i] > 64 ? "+" : "") + (p[PI.DEP0 + i] - 64);
  }
  drawWave($("wv1"), bankOf(P("W1BANK"), false), P("W1POS"), "#0c0e10");
  drawWave($("wv2"), bankOf(P("W2BANK"), true), P("W2POS"), "#005aaa");
  drawSteps();
}
function render() {
  $("preset").value = cur;
  refresh();
}

/* --------------------------------------------------------------------------------------------- the seq --- */
function drawSteps() {
  const box = $("steps");
  box.replaceChildren();
  const n = patch[76];
  for (let i = 0; i < n; i++) {
    const s = patch[80 + i];
    const b = el("button", { textContent: s === 0 ? "REST" : (s > 64 ? "+" : "") + (s - 64), class: s === 0 ? "rest" : "" });
    b.title = "click: up a semitone; shift-click: down; alt-click: rest / note";
    b.onclick = (e) => {
      let v = patch[80 + i];
      if (e.altKey) v = v === 0 ? 64 : 0;
      else if (v) v = Math.max(1, Math.min(127, v + (e.shiftKey ? -1 : 1)));
      patch[80 + i] = v;
      pushPatch();
    };
    box.append(b);
  }
}
function pushPatch() {
  post({ t: "patch", bytes: patch.slice() });
  if (dev.connected) dev.putPatch(127, patch).catch(status);
  refresh();
}

/* ---------------------------------------------------------------------------------------------- audio --- */
async function audioOn() {
  if (ctx) { ctx.state === "running" ? await ctx.suspend() : await ctx.resume(); $("audio").textContent = ctx.state === "running" ? "Sound off" : "Sound on"; return; }
  ctx = new AudioContext({ sampleRate: 44100 });
  await ctx.audioWorklet.addModule("worklet.js");
  node = new AudioWorkletNode(ctx, "crafty", { outputChannelCount: [2] });
  node.connect(ctx.destination);
  node.port.postMessage({ t: "init", wasm: await (await fetch("crafty.wasm")).arrayBuffer() });
  post({ t: "patch", bytes: patch.slice() });
  ubank.forEach((u, i) => u && post({ t: "ubank", u: i, data: u }));
  $("audio").textContent = "Sound off";
}

/* ----------------------------------------------------------------------------------------- the keyboard --- */
let kbOct = 0;
const held = new Set();
function key(n, v) {
  if (v) held.add(n); else held.delete(n);
  post({ t: "key", n, v });
  dev.note(n, v);
  document.querySelectorAll(".kb .k").forEach((k) => k.classList.toggle("down", held.has(+k.dataset.n + 12 * kbOct)));
}
function buildKb() {
  const kb = $("kb"), white = [0, 2, 4, 5, 7, 9, 11], base = 48;
  let wx = 0;
  const ww = 100 / 15;
  for (let i = 0; i < 25; i++) {
    const pc = i % 12, isW = white.includes(pc), n = base + i;
    const k = el("div", { class: `k ${isW ? "w" : "b"}` });
    k.dataset.n = n;
    if (isW) { k.style.left = `${wx * ww}%`; k.style.width = `${ww}%`; wx++; }
    else { k.style.left = `${wx * ww - ww * 0.3}%`; k.style.width = `${ww * 0.6}%`; }
    k.onpointerdown = (e) => { k.setPointerCapture(e.pointerId); key(n + 12 * kbOct, 100); };
    k.onpointerup = k.onpointercancel = () => key(n + 12 * kbOct, 0);
    kb.append(k);
  }
  const map = "awsedftgyhujk";
  addEventListener("keydown", (e) => {
    if (e.repeat || e.target.tagName === "INPUT" && e.target.type === "text") return;
    if (e.key === "z") kbOct = Math.max(-2, kbOct - 1);
    if (e.key === "x") kbOct = Math.min(3, kbOct + 1);
    const i = map.indexOf(e.key);
    if (i >= 0) key(60 + i + 12 * kbOct, 100);
  });
  addEventListener("keyup", (e) => {
    const i = map.indexOf(e.key);
    if (i >= 0) key(60 + i + 12 * kbOct, 0);
  });
}

/* ------------------------------------------------------------------------------------ user wavetables --- */
function buildUbanks() {
  const box = $("ubanks");
  for (let u = 0; u < 2; u++) {
    let table = null, picks = [0, 0, 0, 0, 0];
    const name = el("input", { type: "text", value: `USER ${u + 1}`, maxLength: 13 });
    const file = el("input", { type: "file", accept: ".wav,audio/wav" });
    const frames = el("div", { class: "frames" });
    const info = el("span", { textContent: "No table loaded: bank 1 stands in." });
    const send = el("button", { textContent: "Send to FM-1", disabled: true });
    const cvs = [], rng = [];
    for (let k = 0; k < 5; k++) {
      const c = el("canvas", { width: 120, height: 70 });
      const r = el("input", { type: "range", min: 0, max: 0, value: 0 });
      const lab = el("span", { textContent: `WAVE ${k + 1}` });
      r.oninput = () => { picks[k] = +r.value; lab.textContent = `WAVE ${k + 1}: frame ${picks[k] + 1}`; update(); };
      cvs.push(c); rng.push([r, lab]);
      frames.append(el("div", {}, c, r, lab));
    }
    function update() {
      if (!table) return;
      ubank[u] = makeBank(table, picks);
      for (let k = 0; k < 5; k++) drawSingle(cvs[k], ubank[u].subarray(k * N, (k + 1) * N));
      post({ t: "ubank", u, data: ubank[u] });
      refresh();
    }
    file.onchange = async () => {
      try {
        table = parseWav(await file.files[0].arrayBuffer());
        picks = defaultPicks(table.frames);
        rng.forEach(([r, lab], k) => { r.max = table.frames - 1; r.value = picks[k]; lab.textContent = `WAVE ${k + 1}: frame ${picks[k] + 1}`; });
        name.value = file.files[0].name.replace(/\.wav$/i, "").toUpperCase().slice(0, 13);
        info.textContent = `${table.frames} frame${table.frames > 1 ? "s" : ""} of ${table.frameSize} samples.`;
        send.disabled = false;
        update();
      } catch (e) { info.textContent = e.message; }
    };
    send.onclick = async () => {
      if (!dev.connected) return status("Connect the FM-1 first.");
      try {
        status(`Sending USER ${u + 1}...`);
        await dev.putBank(u, bankBytes(name.value, ubank[u]));
        status(`USER ${u + 1} is on the FM-1.`);
      } catch (e) { status(e.message); }
    };
    box.append(el("div", { class: "ubank" }, el("h2", { textContent: `USER ${u + 1} (bank ${10 + u})` }),
      el("div", { class: "row" }, file, name, send), frames, info));
  }
}

/* --------------------------------------------------------------------------------------------- device --- */
function status(m) { $("status").textContent = m instanceof Error ? m.message : m; }
async function connect() {
  try {
    if (!navigator.requestMIDIAccess) throw new Error("This browser has no Web MIDI: use Chrome or Edge.");
    const access = await navigator.requestMIDIAccess({ sysex: true });
    const info = await dev.connect(access);
    $("devbox").hidden = false;
    $("devname").textContent = info.name;
    status("Connected");
    cur = info.cur;
    await pull();
  } catch (e) { status(e); }
}
async function pull() {
  const r = await dev.getPatch(127);
  loadBytes(r.bytes);
}
dev.onchange = () => { if (!dev.connected) { $("devbox").hidden = true; status("Not connected"); } };
dev.oncc = (cc, v) => {
  const p = M.params.find((x) => x.cc === cc);
  if (!p) return;
  setParam(p.id, v, true);
  const c = ctls.find((x) => x.p.id === p.id);
  if (c) { c.box.classList.add("hit"); setTimeout(() => c.box.classList.remove("hit"), 700); }
};
dev.onevent = (e) => { cur = e.cur; pull().catch(status); };

/* ----------------------------------------------------------------------------------------------- start --- */
build();
buildKb();
buildUbanks();
$("preset").onchange = async () => {
  cur = +$("preset").value;
  $("slot").value = cur;
  if (dev.connected) { await dev.select(cur).catch(status); await pull().catch(status); } else factory(cur);
};
$("init").onclick = () => {
  const b = new Uint8Array(96);
  "INIT".split("").forEach((c, i) => (b[i] = c.charCodeAt(0)));
  M.params.forEach((p) => (b[12 + p.id] = p.def));
  b[77] = 0xc5; b[78] = 1;
  patch.set(b);
  pushPatch();
};
$("random").onclick = () => {
  const r = (a, b) => a + Math.floor(Math.random() * (b - a + 1));
  for (const k of ["W1POS", "W2POS", "MIX", "OMAMT", "CUT", "RES", "MORPH", "FEGAMT", "FD", "FS", "L1RATE", "L1SHAPE"]) patch[12 + PI[k]] = r(0, 127);
  patch[12 + PI.W1BANK] = r(0, 127); patch[12 + PI.W2BANK] = r(0, 127); patch[12 + PI.OMMODE] = r(0, 15) * 8 + 4;
  patch[12 + PI.SPREAD] = r(0, 1) ? r(0, 63) : r(64, 127);
  patch[12 + PI.CUT] = r(50, 127);
  patch[12 + PI.DST0] = r(1, 13); patch[12 + PI.DEP0] = r(40, 90);
  pushPatch();
};
$("audio").onclick = () => audioOn().catch(status);
$("connect").onclick = connect;
$("save").onclick = async () => {
  try { await dev.putPatch(+$("slot").value, patch); status("Saved."); } catch (e) { status(e); }
};
$("pull").onclick = () => pull().catch(status);
$("reset").onclick = async () => {
  try { await dev.resetPreset(+$("slot").value); status("Back to the factory preset."); } catch (e) { status(e); }
};
const addStep = (v) => {
  if (patch[76] >= 16) return;
  if (patch[76] === 0) patch[79] = 60;
  patch[80 + patch[76]] = v; patch[76]++;
  pushPatch();
};
$("stepadd").onclick = () => addStep(patch[76] ? patch[80 + patch[76] - 1] || 64 : 64);
$("steprest").onclick = () => addStep(0);
$("stepdel").onclick = () => { if (patch[76]) { patch[76]--; pushPatch(); } };
$("stepclear").onclick = () => { patch[76] = 0; pushPatch(); };
fetch("crafty.wasm").then((r) => r.arrayBuffer()).then((b) => WebAssembly.instantiate(b, {})).then(({ instance }) => {
  wasm = instance.exports;
  factory(0);
});
factory(0);
