// SPDX-License-Identifier: GPL-3.0-only
// Crafty Synth editor: the panels (after Serum 2 and Shrub, in the FM-1's themes), the preview (the firmware's engine
// in WebAssembly), the FM-1 over Web MIDI, user wavetables from WAV files. Copyright (C) 2026 Jamie (TwistedGenetics).
import M from "./meta.js";
import { fmt, choices, PI, bankOf, band, bandVal, LISTS } from "./format.js";
import { Device, WORKING, inPlugin } from "./device.js";
import { parseWav, makeBank, defaultPicks, bankBytes, bankName, N, PEAK } from "./serum.js";

const $ = (id) => document.getElementById(id);
const el = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) k === "class" ? (e.className = v) : (e[k] = v);
  for (const k of kids) e.append(k);
  return e;
};

/* ------------------------------------------------------------------------------------------ the themes --- */
// the FM-1's (src/app/cr_app.c, CR_THEMES), same order and colours: bg panel bar ink soft acc acc2 track hil line
const THEMES = [
  ["CRAFT", "#e2e2de #f7f7f5 #cecec9 #0e0f11 #5c626a #1665d8 #282c32 #c8c8c3 #cee0fa #b6b6b1"],
  ["CRAFT NIGHT", "#101215 #1e2126 #2c3037 #f4f4f2 #a0a6ae #408cff #d6d9de #3c4149 #163460 #383d44"],
  ["SERUM", "#14171b #22272d #2e343c #e8ecef #8c96a1 #3fa9f5 #7edec8 #3a4048 #183a56 #343a42"],
  ["ACID", "#eef0e6 #fafbf5 #d6dac8 #0c0e0a #505648 #288000 #c8006e #cacebe #dcf5aa #bcc0b0"],
  ["AMBER", "#0e0a06 #1c140c #2e2010 #ffc46e #be823c #ffa028 #ffe2aa #402c16 #50300a #3c2a16"],
  ["PAPER", "#ffffff #ffffff #e6e6e6 #000000 #323232 #0046dc #000000 #bebebe #ffdc3c #8c8c8c"],
].map(([name, c]) => {
  const [bg, panel, bar, ink, soft, acc, acc2, track, hil, line] = c.split(" ");
  return { name, bg, panel, bar, ink, soft, acc, acc2, track, hil, line };
});
let T = THEMES[2];
function setTheme(i) {
  T = THEMES[i] || THEMES[2];
  for (const [k, v] of Object.entries(T)) if (k !== "name") document.documentElement.style.setProperty(`--${k}`, v);
  try { localStorage.setItem("crafty-theme", String(i)); } catch (e) { /* no storage: fine */ }
  drawAll();
}
const rgba = (hex, a) => {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${n >> 16},${(n >> 8) & 255},${n & 255},${a})`;
};

/* ---------------------------------------------------------------------------------------------- state --- */
const patch = new Uint8Array(128);    // cr_patch_t v2: name[12], p[96], steps, magic, ver, root, step[16]
const O_STEPS = 108, O_MAGIC = 109, O_VER = 110, O_ROOT = 111, O_STEP = 112;
const P = (k) => patch[12 + PI[k]];
const ubank = new Array(32).fill(null);      // the user banks as the editor knows them (null: plays bank 1)
const ubankName = new Array(32).fill("");
let cur = 0;
const dev = new Device();
let ctx = null, node = null, wasm = null;

const pending = [];                   // notes played while the audio starts: they sound once it has
function post(m) {
  if (node) node.port.postMessage(m);
  else if (m.t === "key" && pending.length < 64) pending.push(m);
}
function loadBytes(b) { patch.set(b); post({ t: "patch", bytes: patch.slice() }); refresh(); }
function factory(n) { loadBytes(Uint8Array.from(M.presets[n].bytes)); }
function setParam(id, v, fromDevice = false) {
  v = Math.max(0, Math.min(127, v | 0));
  if (patch[12 + id] === v) return;
  patch[12 + id] = v;
  post({ t: "param", id, v });
  if (!fromDevice) dev.cc(M.params[id].cc, v);
  refresh();
}

/* ------------------------------------------------------------------------------------------- canvases --- */
function fitCanvas(c) {
  const r = devicePixelRatio || 1, w = c.clientWidth, h = c.clientHeight;
  if (c.width !== Math.round(w * r) || c.height !== Math.round(h * r)) { c.width = Math.round(w * r); c.height = Math.round(h * r); }
  const g = c.getContext("2d");
  g.setTransform(r, 0, 0, r, 0, 0);
  g.clearRect(0, 0, w, h);
  return { g, w, h };
}
function waveData(bank) {
  if (bank >= 9) { const u = ubank[bank - 9]; if (u) return u; bank = 0; }
  if (!wasm) return null;
  return new Int16Array(wasm.memory.buffer, wasm.cw_wave(bank), 5 * N);
}
function waveAt(d, bank, pos, k) {
  const m = pos * 4, a = Math.min(3, Math.floor(m)), f = m - a;
  let v = (d[a * N + k] + (d[(a + 1) * N + k] - d[a * N + k]) * f) / PEAK;
  if (bank === 8 && pos > 0) {                     // the sine / noise bank
    const n = (((k * 2654435761) >>> 23) & 255) / 128 - 1;
    v += (n - v) * Math.min(1, pos * 2);
  }
  return v;
}
// a wavetable as Serum draws it: frames stacked in depth, faint and tinted, the one playing bright and filled
function drawTable(c, bank, pos127, color) {
  const { g, w, h } = fitCanvas(c), d = waveData(bank);
  if (!d) return;
  const F = 15, dx = w * 0.018, dy = h * 0.035, fw = w - 16 - dx * (F - 1), amp = (h - 20 - dy * (F - 1)) * 0.45;
  const x0 = 8, base = h - 10 - amp, pos = pos127 / 127;
  for (let f = F - 1; f >= 0; f--) {
    const ox = x0 + f * dx, oy = base - f * dy;
    g.strokeStyle = rgba(color, 0.14 + 0.32 * (1 - f / F));
    g.lineWidth = 1;
    g.beginPath();
    for (let i = 0; i <= fw; i += 2) {
      const y = oy - waveAt(d, bank, f / (F - 1), Math.min(255, Math.floor((i / fw) * 256))) * amp;
      i ? g.lineTo(ox + i, y) : g.moveTo(ox + i, y);
    }
    g.stroke();
  }
  const sx = x0 + pos * dx * (F - 1), sy = base - pos * dy * (F - 1);
  const pts = [];
  for (let i = 0; i <= fw; i++) pts.push([sx + i, sy - waveAt(d, bank, pos, Math.min(255, Math.floor((i / fw) * 256))) * amp]);
  g.fillStyle = rgba(color, 0.28);
  g.beginPath(); g.moveTo(sx, sy);
  for (const [x, y] of pts) g.lineTo(x, y);
  g.lineTo(sx + fw, sy); g.closePath(); g.fill();
  g.strokeStyle = color; g.lineWidth = 2.5; g.shadowColor = color; g.shadowBlur = 8;
  g.beginPath();
  pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y)));
  g.stroke();
  g.shadowBlur = 0;
}
function fillCurve(g, pts, floor, color) {
  g.fillStyle = rgba(color, 0.25);
  g.beginPath(); g.moveTo(pts[0][0], floor);
  for (const [x, y] of pts) g.lineTo(x, y);
  g.lineTo(pts[pts.length - 1][0], floor); g.closePath(); g.fill();
  g.strokeStyle = color; g.lineWidth = 2.5;
  g.beginPath();
  pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y)));
  g.stroke();
}
function grid(g, w, h, n) {
  g.strokeStyle = T.line; g.lineWidth = 1;
  for (let i = 1; i < n; i++) { g.beginPath(); g.moveTo((i * w) / n, 0); g.lineTo((i * w) / n, h); g.stroke(); }
}
function drawFilter(c) {
  const { g, w, h } = fitCanvas(c);
  grid(g, w, h, 10);
  const fc = (P("CUT") / 127) * 10, k = 2 * (1 - (0.985 * P("RES")) / 127), m = P("MORPH") / 127;
  const wl = m < 0.5 ? 1 - 2 * m : 0, wh = m < 0.5 ? 0 : 2 * m - 1, wb = m < 0.5 ? 2 * m : 2 - 2 * m, pts = [];
  for (let x = 0; x <= w; x++) {
    const r = Math.pow(2, (x / w) * 10 - fc), r2 = r * r, re = 1 - r2, im = k * r, den = re * re + im * im;
    const nr = wl - wh * r2, ni = wb * r, hr = (nr * re + ni * im) / den, hi = (ni * re - nr * im) / den;
    const db = Math.max(-36, Math.min(18, 10 * Math.log10(hr * hr + hi * hi + 1e-9)));
    pts.push([x, 8 + ((18 - db) / 54) * (h - 16)]);
  }
  fillCurve(g, pts, h, T.acc);
}
function drawEnv(c, a) {
  const { g, w, h } = fitCanvas(c);
  const v = [0, 1, 2, 3].map((i) => patch[12 + a + i] / 127);
  const t = v.map((x, i) => (i === 2 ? 0.6 : 0.05 + 4 * x * x * x)), tot = t.reduce((s, x) => s + x), sc = (w - 16) / tot;
  const top = 10, bot = h - 8, sus = bot - v[2] * (bot - top);
  let x = 8;
  const pts = [[x, bot]];
  x += t[0] * sc; pts.push([x, top]);
  x += t[1] * sc; pts.push([x, sus]);
  x += t[2] * sc; pts.push([x, sus]);
  x += t[3] * sc; pts.push([x, bot]);
  grid(g, w, h, 8);
  fillCurve(g, pts, bot, T.acc);
  g.fillStyle = T.ink;
  for (const [px, py] of pts.slice(1, 4)) g.fillRect(px - 3, py - 3, 6, 6);
}
function drawLfo(c, shapeKey) {
  const { g, w, h } = fitCanvas(c);
  const s = (P(shapeKey) / 127) * 4, k = Math.min(3, Math.floor(s)), f = s - k, mid = h / 2, pts = [];
  let r = 0x1234, hold = 0;
  g.strokeStyle = T.line; g.beginPath(); g.moveTo(0, mid); g.lineTo(w, mid); g.stroke();
  for (let x = 0; x <= w; x++) {
    let ph = (x / w) * 2; ph -= Math.floor(ph);
    if (((x * 8) % w) < 8) { r = (Math.imul(r, 1103515245) + 12345) >>> 0; hold = ((r >>> 16) & 255) / 128 - 1; }
    const v = [Math.sin(2 * Math.PI * ph), ph < 0.25 ? 4 * ph : ph < 0.75 ? 2 - 4 * ph : 4 * ph - 4, 2 * ph - 1, ph < 0.5 ? 1 : -1, hold];
    pts.push([x, mid - (v[k] + (v[k + 1] - v[k]) * f) * (h * 0.42)]);
  }
  g.fillStyle = rgba(T.acc, 0.25);
  g.beginPath(); g.moveTo(0, mid);
  for (const [x, y] of pts) g.lineTo(x, y);
  g.lineTo(w, mid); g.closePath(); g.fill();
  g.strokeStyle = T.acc; g.lineWidth = 2.5; g.beginPath();
  pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y)));
  g.stroke();
}

/* ------------------------------------------------------------------------------------------- the knobs --- */
const knobs = [];
const BIP = new Set(["BIP", "SEMI", "CENT", "GLIDE"]);
function knob(key, label, color = "acc") {
  const k = { key, color, el: el("div", { class: "knob", tabIndex: 0 }) };
  k.label = el("label", { textContent: label || M.params[PI[key]].name });
  k.cv = el("canvas", { width: 56, height: 56 });
  k.val = el("span", { class: "val" });
  k.el.append(k.label, k.cv, k.val);
  const id = () => PI[k.key];
  let y0 = 0, v0 = 0, acc = 0;
  k.cv.onpointerdown = (e) => { k.cv.setPointerCapture(e.pointerId); y0 = e.clientY; v0 = patch[12 + id()]; acc = 0; };
  k.cv.onpointermove = (e) => {
    if (!k.cv.hasPointerCapture(e.pointerId)) return;
    acc = (y0 - e.clientY) / (e.shiftKey ? 8 : 2);
    setParam(id(), Math.round(v0 + acc));
  };
  k.cv.ondblclick = () => setParam(id(), M.params[id()].def);
  k.el.onwheel = (e) => { e.preventDefault(); setParam(id(), patch[12 + id()] + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 1 : 2)); };
  k.el.onkeydown = (e) => {
    const d = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1, PageUp: 10, PageDown: -10 }[e.key];
    if (d) { e.preventDefault(); setParam(id(), patch[12 + id()] + d); }
  };
  k.draw = () => {
    const p = M.params[id()], v = patch[12 + p.id] / 127, bip = BIP.has(p.kind), { g, w, h } = fitCanvas(k.cv);
    if (w < 16 || h < 16) return;                  // on a page not showing (the plugin's pages)
    const cx = w / 2, cy = h / 2 + 3, r = w / 2 - 6, a0 = Math.PI * 0.75, a1 = Math.PI * 2.25, col = T[k.color];
    const av = a0 + (a1 - a0) * v, af = bip ? a0 + (a1 - a0) * 0.5 : a0;
    g.lineCap = "round"; g.lineWidth = 5;
    g.strokeStyle = T.track; g.beginPath(); g.arc(cx, cy, r, a0, a1); g.stroke();
    g.strokeStyle = col; g.beginPath(); g.arc(cx, cy, r, Math.min(af, av), Math.max(af, av)); g.stroke();
    g.fillStyle = T.panel; g.beginPath(); g.arc(cx, cy, r - 7, 0, 2 * Math.PI); g.fill();
    g.strokeStyle = T.ink; g.lineWidth = 2.5; g.beginPath();
    g.moveTo(cx + Math.cos(av) * 4, cy + Math.sin(av) * 4); g.lineTo(cx + Math.cos(av) * (r - 9), cy + Math.sin(av) * (r - 9)); g.stroke();
    const t = fmt(p, patch[12 + p.id], patch.subarray(12, 108)).replace("ALWAYS ", "A ").replace("LEGATO ", "L ").replace("UNISON ", "UNI ").replace(" cents", "c").replace(" semi", " st");
    k.val.textContent = t;
    k.val.style.fontSize = t.length > 9 ? "15px" : t.length > 6 ? "17px" : "20px";
    k.el.title = `${p.name}: ${t} (CC ${p.cc}) · drag, wheel or arrows; shift: fine; double-click: default`;
  };
  knobs.push(k);
  return k.el;
}
function pick(key, label) {
  const p = M.params[PI[key]], s = el("select");
  for (const [v, n] of choices(p)) s.append(el("option", { value: v, textContent: n }));
  s.onchange = () => setParam(p.id, +s.value);
  const box = el("div", { class: "pick" }, el("label", { textContent: label || p.name }), s);
  picks.push({ key, s, p });
  return box;
}
const picks = [];
function syncPick(pk) {
  const v = P(pk.key), ch = choices(pk.p);
  let best = ch[0][0];
  if (pk.p.kind === "BANK") best = ch.find(([cv]) => bankOf(cv, pk.key === "W2BANK") === bankOf(v, pk.key === "W2BANK"))[0];
  else if (pk.p.kind === "OMMODE") best = (v >> 3) * 8 + 4;
  else if (pk.p.kind === "OCT") best = ch[(v * 7) >> 7][0];
  else if (pk.p.kind === "ARPRATE") best = ch[(v * 6) >> 7][0];
  else if (pk.p.kind === "LMODE") best = v < 42 ? 20 : v < 84 ? 63 : 105;
  else if (pk.p.kind === "ONOFF") best = v >= 64 ? 127 : 0;
  else if (pk.p.kind === "DEST") best = v;
  else if (LISTS[pk.p.kind]) { const n = LISTS[pk.p.kind].length; best = bandVal(band(v, n), n); }
  pk.s.value = best;
}

/* ------------------------------------------------------------------------------------------- presets --- */
// 0-63 the factory's (in meta.js too), 64-191 the user's (on the FM-1: their names come from it when connected)
const userNames = new Array(128).fill(null);
const label = (i) => (i < 64 ? `${(i >> 3) + 1}-${(i & 7) + 1}` : `U${((i - 64) >> 3) + 1}-${((i - 64) & 7) + 1}`);
function buildPresetList() {
  const sel = $("preset"), slot = $("slot");
  sel.replaceChildren(); slot.replaceChildren();
  const fg = el("optgroup", { label: "FACTORY" }), ug = el("optgroup", { label: inPlugin ? "USER" : "USER (on the FM-1)" });
  M.presets.forEach((p, i) => fg.append(el("option", { value: i, textContent: `${label(i)}  ${p.name}` })));
  for (let i = 64; i < 192; i++) {
    const n = userNames[i - 64];
    ug.append(el("option", { value: i, textContent: `${label(i)}  ${n || "-"}`, disabled: !dev.connected }));
    slot.append(el("option", { value: i, textContent: `${label(i)}  ${n || "(empty)"}` }));
  }
  sel.append(fg, ug);
  sel.value = cur;
}
async function readUserNames() {          // the user presets' names, from the FM-1 (a patch each)
  for (let i = 0; i < 128; i++) {
    try {
      const r = await dev.getPatch(64 + i);
      userNames[i] = r.saved ? String.fromCharCode(...r.bytes.slice(0, 12)).replace(/\0.*$/, "").trim() || "?" : null;
    } catch (e) { break; }
  }
  buildPresetList();
}

/* ------------------------------------------------------------------------------------------ the panels --- */
const views = {};
let envTab = 1, lfoTab = 0;
const ENV = [["FILTER", ["FA", "FD", "FS", "FR"]], ["AMP", ["AA", "AD", "AS", "AR"]], ["MOD", ["MA", "MD", "MS", "MR"]]];
const LFO = [["LFO 1", ["L1RATE", "L1SHAPE"], ["L1MODE", "L1SYNC"]], ["LFO 2", ["L2RATE", "L2SHAPE"], ["L2MODE", "L2SYNC"]]];
const envKnobs = [], lfoKnobs = [], lfoPicks = [];

function panel(cls, title, ...kids) { return el("section", { class: `panel ${cls}` }, title, ...kids); }
function head(badge, text, extra) {
  const h = el("h2", { class: "head" });
  if (badge) h.append(el("span", { class: `badge ${badge === "B" ? "b" : ""}`, textContent: badge }));
  h.append(text);
  if (extra) h.append(extra);
  return h;
}
function tabs(names, on, cb) {
  const t = el("div", { class: "tabs" });
  names.forEach((n, i) => {
    const b = el("button", { textContent: n, class: i === on ? "on" : "" });
    b.onclick = () => { [...t.children].forEach((x, j) => x.classList.toggle("on", j === i)); cb(i); };
    t.append(b);
  });
  return t;
}

/* 0.13, the plugin: a wavetable file from its Wavetables folder in place of a wave's bank (the Twisted Tracker's
   TABLE 1 / 2 FILE). The plugin keeps the list and the choice ("crafty-tables" events); "" is the bank. */
const tableSel = [];
let tableFiles = [], tableNow = ["", ""];
const tableLabel = (p) => p.slice(p.lastIndexOf("/") + 1).replace(/\.wav$/i, "");
function tableFilePick(k) {
  const s = el("select");
  s.onchange = () => setTableFile(k, s.value);
  s.onfocus = () => window.__JUCE__.backend.emitEvent("crafty-tables", { get: 1 });   // new files on the disk
  tableSel[k] = s;
  return el("div", { class: "pick" }, el("label", { textContent: "TABLE FILE" }), s);
}
function renderTables() {
  tableSel.forEach((s, k) => {
    const opts = [el("option", { value: "", textContent: "(the bank)" })], groups = new Map();
    for (const p of tableFiles) {
      const i = p.lastIndexOf("/"), f = i < 0 ? "" : p.slice(0, i);
      if (!groups.has(f)) groups.set(f, f ? el("optgroup", { label: f }) : null);
      const o = el("option", { value: p, textContent: tableLabel(p) });
      if (groups.get(f)) groups.get(f).append(o); else opts.push(o);
    }
    if (tableNow[k] && !tableFiles.includes(tableNow[k]))
      opts.push(el("option", { value: tableNow[k], textContent: `MISSING: ${tableLabel(tableNow[k])}` }));
    s.replaceChildren(...opts, ...[...groups.values()].filter(Boolean));
    s.value = tableNow[k];
  });
}
function setTableFile(k, path) {
  tableNow[k] = path;
  window.__JUCE__.backend.emitEvent("crafty-tables", { set: k, path });
}
if (inPlugin) {
  window.__JUCE__.backend.addEventListener("crafty-tables", (v) => {
    tableFiles = v.files || []; tableNow = v.sel || ["", ""]; renderTables();
  });
}

function build() {
  const rack = $("rack");
  for (const [w, k] of [["A", 1], ["B", 2]]) {
    const bankSel = pick(k === 1 ? "W1BANK" : "W2BANK", "BANK").querySelector("select");
    views[`wt${w}`] = el("canvas", { class: "view" });
    const ks = el("div", { class: "knobs" });
    if (k === 1) ks.append(knob("W1POS", "WT POS"), knob("OMAMT"), pick("OMMODE"));
    else ks.append(knob("W2POS", "WT POS", "acc2"), knob("TUNE", null, "acc2"), knob("FINE", null, "acc2"),
      knob("BAMT", "B MOD", "acc2"), pick("BMODE", "B MODE"));
    if (inPlugin) ks.append(tableFilePick(k - 1));
    rack.append(panel("osc", head(w, `OSC ${w}`, bankSel), views[`wt${w}`], ks));
  }
  rack.append(panel("voice", head(null, "VOICE"), el("div", { class: "knobs" }, knob("MIX"), knob("SPREAD"), knob("GLIDE"),
    pick("OCT", "OCTAVE"), knob("VOL"), knob("AEGAMT", "AMP EG")),
    el("div", { class: "subhead", textContent: "SUB OSC" }),             // 0.12: under OSC A, before the filter
    el("div", { class: "knobs" }, knob("SUBLVL", "LEVEL"), pick("SUBOCT", "OCTAVE"), pick("SUBWAVE", "WAVE"))));
  views.filter = el("canvas", { class: "view" });
  rack.append(panel("filter", head(null, "FILTER"), views.filter,
    el("div", { class: "knobs" }, pick("FTYPE", "TYPE"), knob("CUT"), knob("RES"), knob("MORPH"), knob("FEGAMT", "FILT EG"))));
  views.env = el("canvas", { class: "view" });
  const ek = el("div", { class: "knobs" });
  ENV[envTab][1].forEach((key) => { ek.append(knob(key)); envKnobs.push(knobs[knobs.length - 1]); });
  rack.append(panel("env", head(null, "ENVELOPES", tabs(ENV.map((e) => e[0]), envTab, (i) => {
    envTab = i; envKnobs.forEach((kn, j) => (kn.key = ENV[i][1][j])); refresh();
  })), views.env, ek));
  views.lfo = el("canvas", { class: "view" });
  const lk = el("div", { class: "knobs" });
  LFO[0][1].forEach((key) => { lk.append(knob(key)); lfoKnobs.push(knobs[knobs.length - 1]); });
  LFO[0][2].forEach((key) => { lk.append(pick(key)); lfoPicks.push(picks[picks.length - 1]); });
  rack.append(panel("lfo", head(null, "LFO", tabs(LFO.map((l) => l[0]), 0, (i) => {
    lfoTab = i;
    lfoKnobs.forEach((kn, j) => (kn.key = LFO[i][1][j]));
    lfoPicks.forEach((pk, j) => { pk.key = LFO[i][2][j]; pk.p = M.params[PI[pk.key]]; });
    refresh();
  })), views.lfo, lk));
  const t = el("table", { class: "mtx" });
  M.srcs.forEach((s, i) => {
    const sel = el("select", { id: `md${i}` });
    M.dests.forEach((d, j) => sel.append(el("option", { value: j, textContent: d })));
    sel.onchange = () => setParam(PI.DST0 + i, +sel.value);
    const r = el("input", { type: "range", min: 1, max: 127, id: `mp${i}` });
    r.oninput = () => setParam(PI.DEP0 + i, +r.value);
    r.ondblclick = () => setParam(PI.DEP0 + i, 64);
    t.append(el("tr", {}, el("td", { textContent: s }), el("td", {}, sel), el("td", { class: "dep" }, r), el("td", { class: "dv", id: `mv${i}` })));
  });
  rack.append(panel("matrix", head(null, "MATRIX"), t));
  rack.append(panel("fx", head(null, "DRIVE + DELAY"), el("div", { class: "knobs" }, knob("DIST", "DISTORT"), knob("DMIX", "DELAY"),
    knob("DTIME"), knob("DFB"), pick("DSYNC", "DLY SYNC"))));
  rack.append(panel("fx", head(null, "REVERB", el("small", { textContent: "Horizon, after Super-Massive" })), el("div", { class: "knobs" },
    pick("VMODE", "MODE"), knob("VMIX", "REVERB"), knob("VSIZE", "SIZE"), knob("VDECAY", "DECAY"), knob("VWARP", "WARP"),
    knob("VDENS", "DENSITY"), knob("VMOD", "MOD"))));
  rack.append(panel("fx", head(null, "MOD FX"), el("div", { class: "knobs" }, knob("CHMIX", "CHORUS"), knob("CHRATE", "CH RATE"),
    knob("PHMIX", "PHASER"), knob("PHRATE", "PH RATE"))));
  rack.append(panel("fx", head(null, "CRUSH + OTT", el("small", { textContent: "EraCrush" })), el("div", { class: "knobs" },
    pick("ERA"), knob("CRUSH"), knob("OTT"))));
  rack.append(panel("arp", head(null, "ARP-SEQ"), el("div", { class: "knobs" }, pick("ARP"), knob("TEMPO"), pick("ARPRATE", "RATE"))));
  buildPresetList();
  THEMES.forEach((th, i) => $("theme").append(el("option", { value: i, textContent: th.name })));
}

function drawAll() {
  if (!views.wtA) return;
  drawTable(views.wtA, bankOf(P("W1BANK"), false), P("W1POS"), T.acc);
  drawTable(views.wtB, bankOf(P("W2BANK"), true), P("W2POS"), T.acc2);
  drawFilter(views.filter);
  drawEnv(views.env, PI[ENV[envTab][1][0]]);
  drawLfo(views.lfo, LFO[lfoTab][1][1]);
  knobs.forEach((k) => k.draw());
}
function refresh() {
  picks.forEach(syncPick);
  for (let i = 0; i < 8; i++) {
    $(`md${i}`).value = P(`DST${i}`);
    $(`mp${i}`).value = P(`DEP${i}`);
    $(`mv${i}`).textContent = (P(`DEP${i}`) > 64 ? "+" : "") + (P(`DEP${i}`) - 64);
  }
  $("preset").value = cur;
  const nm = $("pname");
  if (nm && document.activeElement !== nm) nm.value = patchName();
  drawAll();
  drawSteps();
}

/* --------------------------------------------------------------------------------------------- the seq --- */
function drawSteps() {
  const box = $("steps");
  box.replaceChildren();
  for (let i = 0; i < patch[O_STEPS]; i++) {
    const s = patch[O_STEP + i];
    const b = el("button", { textContent: s === 0 ? "REST" : (s > 64 ? "+" : "") + (s - 64), class: s === 0 ? "rest" : "" });
    b.onclick = (e) => {
      let v = patch[O_STEP + i];
      if (e.altKey) v = v === 0 ? 64 : 0;
      else if (v) v = Math.max(1, Math.min(127, v + (e.shiftKey ? -1 : 1)));
      patch[O_STEP + i] = v;
      pushPatch();
    };
    box.append(b);
  }
}
/* the preset's name: 12 characters the FM-1's screen can show (capitals, digits, space, a little punctuation) */
function patchName() { return String.fromCharCode(...patch.slice(0, 12)).replace(/\0.*$/, ""); }
function cleanName(s) { return s.toUpperCase().replace(/[^A-Z0-9 \-.\/+&!?#'()*:]/g, "").slice(0, 12); }
let nameTimer = null;
function setName(s) {
  const n = cleanName(s);
  for (let i = 0; i < 12; i++) patch[i] = i < n.length ? n.charCodeAt(i) : 0;
  clearTimeout(nameTimer);                       // the working patch follows once typing pauses
  nameTimer = setTimeout(() => { if (dev.connected) dev.putPatch(WORKING, patch).catch(status); }, 300);
  return n;
}
function pushPatch() {
  post({ t: "patch", bytes: patch.slice() });
  if (dev.connected) dev.putPatch(WORKING, patch).catch(status);
  refresh();
}

/* ---------------------------------------------------------------------------------------------- audio --- */
// The firmware's engine renders in an AudioWorklet; where that can't start (an old browser, a page that blocks
// WebAssembly in workers) it runs on the main thread through a ScriptProcessor instead. Audio starts on the first
// click or key anywhere (browsers allow sound only after one); SOUND ON / OFF mutes it.
let audioState = "off", muted = false, gain = null, meter = null, sp = null, starting = null;
function handleMain(e, d) {      // the same messages as worklet.js, for the main-thread fallback
  const mem = () => new Uint8Array(e.memory.buffer);
  switch (d.t) {
    case "patch": mem().set(d.bytes, e.cw_patch()); e.cw_apply(); break;
    case "param": e.cw_param(d.id, d.v); break;
    case "key": e.cw_key(d.n, d.v); break;
    case "alloff": e.cw_all_off(); break;
    case "ctl": e.cw_ctl(d.src, d.v); break;
    case "ubank": new Int16Array(e.memory.buffer, e.cw_ubank(d.u), 5 * 256).set(d.data); e.cw_ubank_changed(d.u); break;
  }
}
async function startWorklet(bytes) {
  await ctx.audioWorklet.addModule("worklet.js");
  const n = new AudioWorkletNode(ctx, "crafty", { outputChannelCount: [2] });
  await new Promise((ok, bad) => {
    const t = setTimeout(() => bad(new Error("the audio worker didn't start")), 4000);
    n.port.onmessage = (m) => {
      if (m.data.t === "ready") { clearTimeout(t); ok(); }
      if (m.data.t === "error") { clearTimeout(t); bad(new Error(m.data.msg)); }
    };
    n.port.postMessage({ t: "init", wasm: bytes });
  });
  return n;
}
async function startMain(bytes) {     // the fallback: the engine on this thread
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const e = instance.exports;
  e.cw_init();
  sp = ctx.createScriptProcessor(1024, 0, 2);
  sp.onaudioprocess = (ev) => {
    const L = ev.outputBuffer.getChannelData(0), R = ev.outputBuffer.getChannelData(1);
    for (let k = 0; k < L.length; k += 128) {
      const m = Math.min(128, L.length - k), f = new Float32Array(e.memory.buffer, e.cw_render(m), 2 * m);
      for (let i = 0; i < m; i++) { L[k + i] = f[2 * i]; R[k + i] = f[2 * i + 1]; }
    }
  };
  return { port: { postMessage: (d) => handleMain(e, d) }, connect: (x) => sp.connect(x) };
}
function ensureAudio() {
  if (inPlugin) return Promise.resolve();          // the plugin plays through the host, not the page
  if (starting) return starting;
  starting = (async () => {
    try {
      if (navigator.audioSession) navigator.audioSession.type = "playback";   // iOS: sound with the silent switch on
      const AC = window.AudioContext || window.webkitAudioContext;
      ctx = new AC({ sampleRate: 44100 });
      if (ctx.state !== "running") await ctx.resume();
      const bytes = await (await fetch("crafty.wasm")).arrayBuffer();
      gain = ctx.createGain();
      meter = ctx.createAnalyser();
      meter.fftSize = 1024;
      gain.connect(meter);
      meter.connect(ctx.destination);
      let how = "worklet";
      try {
        if (!ctx.audioWorklet) throw new Error("no AudioWorklet");
        node = await startWorklet(bytes.slice(0));
      } catch (err) {
        console.warn("Crafty Synth: the audio worker failed, using the main thread:", err);
        node = await startMain(bytes);
        how = "main thread";
      }
      node.connect(gain);
      post({ t: "patch", bytes: patch.slice() });
      ubank.forEach((u, i) => u && post({ t: "ubank", u: i, data: u }));
      for (const m of pending.splice(0)) post(m);
      audioState = "on";
      status(`Sound on (${Math.round(ctx.sampleRate / 100) / 10} kHz, ${how})`);
      $("audio").textContent = "SOUND OFF";
      $("audio").classList.remove("go");
      runMeter();
    } catch (err) {
      audioState = "failed";
      starting = null;
      status(`No sound: ${err.message || err}`);
    }
  })();
  return starting;
}
async function wake() {               // a browser may suspend audio (a tab in the background, iOS): resume it
  if (inPlugin) return;
  if (!ctx) return ensureAudio();
  if (ctx.state !== "running") { try { await ctx.resume(); } catch (e) { /* the next gesture tries again */ } }
}
function runMeter() {
  const bar = $("meter"), a = new Float32Array(meter.fftSize);
  let level = 0;
  const tick = () => {
    meter.getFloatTimeDomainData(a);
    let pk = 0;
    for (const x of a) pk = Math.max(pk, Math.abs(x));
    level = Math.max(pk, level * 0.9);
    bar.style.width = `${Math.min(100, level * 140)}%`;
    requestAnimationFrame(tick);
  };
  tick();
}
async function soundButton() {
  if (!ctx || audioState !== "on") { await ensureAudio(); muted = false; return; }
  muted = !muted;
  gain.gain.value = muted ? 0 : 1;
  if (!muted) await wake();
  $("audio").textContent = muted ? "SOUND ON" : "SOUND OFF";
  $("audio").classList.toggle("go", muted);
  status(muted ? "Muted" : "Sound on");
}
addEventListener("pointerdown", () => { if (audioState === "off" || audioState === "on") wake(); }, true);
addEventListener("keydown", () => { if (audioState === "off" || audioState === "on") wake(); }, true);

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
  const kb = $("kb"), white = [0, 2, 4, 5, 7, 9, 11], ww = 100 / 15;
  let wx = 0;
  for (let i = 0; i < 25; i++) {
    const n = 48 + i, isW = white.includes(i % 12), k = el("div", { class: `k ${isW ? "w" : "b"}` });
    k.dataset.n = n;
    if (isW) { k.style.left = `${wx * ww}%`; k.style.width = `${ww}%`; wx++; }
    else { k.style.left = `${wx * ww - ww * 0.3}%`; k.style.width = `${ww * 0.6}%`; }
    k.onpointerdown = (e) => { k.setPointerCapture(e.pointerId); key(n + 12 * kbOct, 100); };
    k.onpointerup = k.onpointercancel = () => key(n + 12 * kbOct, 0);
    kb.append(k);
  }
  // the computer keys play wherever the focus is (a dropdown just used included: it would jump to a preset starting
  // with that letter), except while typing a name
  const map = "awsedftgyhujk", typing = (t) => t.tagName === "INPUT" && t.type === "text";
  addEventListener("keydown", (e) => {
    const k = e.key.toLowerCase(), i = map.indexOf(k);
    if (e.metaKey || e.ctrlKey || e.altKey || typing(e.target) || (i < 0 && k !== "z" && k !== "x")) return;
    e.preventDefault();
    if (e.repeat) return;
    if (k === "z") kbOct = Math.max(-2, kbOct - 1);
    else if (k === "x") kbOct = Math.min(3, kbOct + 1);
    else key(60 + i + 12 * kbOct, 100);
  }, true);
  addEventListener("keyup", (e) => {
    const i = map.indexOf(e.key.toLowerCase());
    if (i >= 0 && !typing(e.target)) key(60 + i + 12 * kbOct, 0);
  }, true);
  addEventListener("blur", () => { for (const n of [...held]) key(n, 0); });   // no stuck notes when the window loses focus
  addEventListener("change", (e) => { if (e.target.tagName === "SELECT") e.target.blur(); });
}

/* ------------------------------------------------------------------------------------ user wavetables --- */
let ubankUsed = new Array(32).fill(false);
function renderUbankSlots() {
  const sel = $("ubslot");
  if (!sel) return;
  const v = sel.value || "0";
  sel.replaceChildren();
  for (let u = 0; u < 32; u++) sel.append(el("option", { value: u, textContent: `USER ${u + 1}${ubankUsed[u] ? " (used)" : ""}` }));
  sel.value = v;
}
let showUbank = () => {};
function buildUbanks() {
  // one importer: pick a user bank (banks 10-41 on OSC A and B), see what it holds, open a WAV to replace it,
  // pick five frames, send. Changing the bank drops an unsent WAV and puts the old bank's waves back.
  const box = $("ubanks");
  let table = null, picksU = [0, 0, 0, 0, 0], data = null, at = 0, before = null;
  const name = el("input", { type: "text", value: "", maxLength: 13 });
  const file = el("input", { type: "file", accept: ".wav,audio/wav" });
  const slot = el("select", { id: "ubslot" });
  const frames = el("div", { class: "frames" });
  const info = el("span", { class: "status" });
  const send = el("button", { textContent: inPlugin ? "STORE" : "SEND TO FM-1", disabled: true });
  const view = el("canvas", { class: "view" });
  const cvs = [], rng = [];
  for (let k = 0; k < 5; k++) {
    const c = el("canvas"), r = el("input", { type: "range", min: 0, max: 0, value: 0, disabled: true }), lab = el("span", { textContent: `WAVE ${k + 1}` });
    r.oninput = () => { picksU[k] = +r.value; lab.textContent = `WAVE ${k + 1}: FRAME ${picksU[k] + 1}`; update(); };
    cvs.push(c); rng.push([r, lab]);
    frames.append(el("div", {}, c, r, lab));
  }
  function draw(u, d) {
    drawTable(view, 9 + u, 64, T.acc);
    cvs.forEach((c, k) => {
      const { g, w, h } = fitCanvas(c);
      if (!d) return;
      const s = d.subarray(k * N, (k + 1) * N), pts = [];
      for (let x = 0; x <= w; x++) pts.push([x, h / 2 - (s[Math.min(N - 1, Math.floor((x / w) * N))] / PEAK) * h * 0.42]);
      g.fillStyle = rgba(T.acc, 0.25); g.beginPath(); g.moveTo(0, h / 2);
      for (const [x, y] of pts) g.lineTo(x, y);
      g.lineTo(w, h / 2); g.fill();
      g.strokeStyle = T.acc; g.lineWidth = 2; g.beginPath(); pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y))); g.stroke();
    });
  }
  function put(u, d) {                          // into the editor's engine (the preview), or back to bank 1
    ubank[u] = d;
    if (wasm) post({ t: "ubank", u, data: d || new Int16Array(wasm.memory.buffer, wasm.cw_wave(0), 5 * N).slice() });
    refresh();
  }
  function show() {                             // the bank as it is, no WAV open
    const u = at;
    table = null; data = null; before = null; file.value = "";
    send.disabled = true;
    rng.forEach(([r, lab], k) => { r.max = 0; r.value = 0; r.disabled = true; lab.textContent = `WAVE ${k + 1}`; });
    name.value = ubankName[u] || (ubank[u] ? "" : "MY TABLE");
    info.textContent = ubank[u] ? `USER ${u + 1}${ubankName[u] ? ": " + ubankName[u] : ""}. Open a WAV to replace it.`
      : `USER ${u + 1} is empty (it plays bank 1). Open a WAV (Serum, Vital, or a single cycle) to fill it.`;
    draw(u, ubank[u] || waveData(0));
  }
  showUbank = (u) => { if (u === at && !table) show(); };
  function update() {
    if (!table) return;
    data = makeBank(table, picksU);
    put(at, data);
    draw(at, data);
  }
  slot.onchange = () => {
    if (table && before !== undefined) put(at, before);   // the unsent WAV leaves; the bank gets its waves back
    at = +slot.value;
    show();
  };
  file.onchange = async () => {
    if (!file.files[0]) return;
    try {
      const t = parseWav(await file.files[0].arrayBuffer(), wasm);
      if (!table) before = ubank[at];
      table = t;
      picksU = defaultPicks(table.frames);
      rng.forEach(([r, lab], k) => { r.disabled = false; r.max = table.frames - 1; r.value = picksU[k]; lab.textContent = `WAVE ${k + 1}: FRAME ${picksU[k] + 1}`; });
      name.value = bankName(file.files[0].name);
      info.textContent = `${table.frames} frame${table.frames > 1 ? "s" : ""} of ${table.frameSize} samples, previewing in USER ${at + 1}. Send to keep it.`;
      send.disabled = false;
      update();
    } catch (e) { info.textContent = e.message; }
  };
  send.onclick = async () => {
    if (!dev.connected) return status(inPlugin ? "The plugin isn't ready." : "Connect the FM-1 first.");
    const u = at;
    try {
      status(`Sending USER ${u + 1}...`);
      await dev.putBank(u, bankBytes(name.value, data));
      ubankUsed[u] = true;
      ubankName[u] = bankName(name.value);
      renderUbankSlots();
      before = data;                              // it's the bank now: changing slot keeps it
      status(inPlugin ? `USER ${u + 1} stored.` : `USER ${u + 1} is on the FM-1.`);
    } catch (e) { status(e.message); }
  };
  box.append(el("div", { class: "ubank" }, el("div", { class: "row" }, file, el("label", {}, "INTO ", slot), name, send),
    view, frames, info));
  renderUbankSlots();
  show();
}
async function readUserBanks() {                // what the FM-1's user banks hold, so the preview plays them
  for (let u = 0; u < 32; u++) {
    if (!dev.connected) return;
    let b = null;
    if (ubankUsed[u]) b = await dev.getBank(u).catch(() => null);
    ubankName[u] = b ? b.name : "";
    ubank[u] = b ? b.wave : null;
    if (wasm) post({ t: "ubank", u, data: b ? b.wave : new Int16Array(wasm.memory.buffer, wasm.cw_wave(0), 5 * N).slice() });
    showUbank(u);
  }
  refresh();
}

/* --------------------------------------------------------------------------------------------- device --- */
function status(m) { $("status").textContent = m instanceof Error ? m.message : m; }
async function connect() {
  try {
    let info;
    if (inPlugin) info = await dev.connectPlugin();
    else {
      if (!navigator.requestMIDIAccess) throw new Error("This browser has no Web MIDI: use Chrome or Edge.");
      info = await dev.connect(await navigator.requestMIDIAccess({ sysex: true }));
    }
    $("devbox").hidden = false;
    $("devname").textContent = info.name;
    status("Connected: reading the user presets...");
    cur = info.cur;
    ubankUsed = info.ubank;
    await pull();
    await readUserNames();
    $("slot").value = cur >= 64 ? cur : String(64 + Math.max(0, userNames.indexOf(null)));
    renderUbankSlots();
    status("Connected: reading the user wavetables...");
    await readUserBanks();
    status("Connected");
    if (inPlugin) window.__JUCE__.backend.emitEvent("crafty-tables", { get: 1 });
  } catch (e) { status(e); }
}
async function pull() { loadBytes((await dev.getPatch(0x3fff)).bytes); }
dev.onchange = () => { if (!dev.connected) { $("devbox").hidden = true; status("Not connected"); } };
dev.oncc = (cc, v) => {
  const p = M.params.find((x) => x.cc === cc);
  if (!p) return;
  setParam(p.id, v, true);
  const k = knobs.find((x) => PI[x.key] === p.id);
  if (k) { k.el.classList.add("hit"); setTimeout(() => k.el.classList.remove("hit"), 700); }
};
dev.onevent = (e) => { cur = e.cur; pull().catch(status); if (e.kind === 2) readUserNames(); };

/* ----------------------------------------------------------------------------------------------- start --- */
build();
buildKb();
buildUbanks();
async function choose(n) {
  const max = dev.connected ? 192 : 64;
  cur = (n + max) % max;
  $("slot").value = cur;
  if (dev.connected) { await dev.select(cur).catch(status); await pull().catch(status); } else factory(cur);
}
$("preset").onchange = () => choose(+$("preset").value);
$("prev").onclick = () => choose(cur - 1);
$("next").onclick = () => choose(cur + 1);
$("theme").onchange = () => setTheme(+$("theme").value);
$("init").onclick = () => {
  const b = new Uint8Array(128);
  "INIT".split("").forEach((c, i) => (b[i] = c.charCodeAt(0)));
  M.params.forEach((p) => (b[12 + p.id] = p.def));
  b[O_MAGIC] = 0xc5; b[O_VER] = 2;
  patch.set(b);
  pushPatch();
};
/* RANDOM: the engine's Random Patch (src/core cr_patch_random, as the FM-1's SAVE + REC and every other version): a
   random preset (a saved user one too when connected), varied so it stays playable. Shift-click keeps the banks. */
let randSeeded = false;
async function randomPatch(keep) {
  if (!wasm) return status("The editor's engine is still loading.");
  if (!randSeeded) { wasm.cw_rand_seed((Math.random() * 4294967296) >>> 0 || 1); randSeeded = true; }
  const saved = new Uint8Array(wasm.memory.buffer, wasm.cw_rand_saved(), 192);
  for (let i = 0; i < 192; i++) saved[i] = i < 64 || (dev.connected && userNames[i - 64] !== null) ? 1 : 0;
  const slot = wasm.cw_rand_slot();
  const bytes = slot < 64 ? Uint8Array.from(M.presets[slot].bytes) : (await dev.getPatch(slot)).bytes;
  new Uint8Array(wasm.memory.buffer, wasm.cw_rand_cur(), 128).set(patch);
  new Uint8Array(wasm.memory.buffer, wasm.cw_patch(), 128).set(bytes);
  wasm.cw_rand_vary(keep ? 1 : 0);
  patch.set(new Uint8Array(wasm.memory.buffer, wasm.cw_patch(), 128));
  pushPatch();
  if (inPlugin && !keep)                         // as the Tracker: half the time one of your files, else the bank
    for (const k of [0, 1])
      setTableFile(k, tableFiles.length && Math.random() < 0.5 ? tableFiles[Math.floor(Math.random() * tableFiles.length)] : "");
  status(`Random patch from ${label(slot)}${keep ? ", wavetables kept" : ""}.`);
}
$("random").onclick = (e) => randomPatch(e.shiftKey).catch(status);
$("audio").onclick = (e) => { e.stopPropagation(); soundButton(); };
$("connect").onclick = connect;
$("save").onclick = async () => {
  const sl = +$("slot").value;
  try {
    await dev.putPatch(sl, patch);
    userNames[sl - 64] = String.fromCharCode(...patch.slice(0, 12)).replace(/\0.*$/, "").trim() || "?";
    buildPresetList();
    $("slot").value = sl;
    status(`Saved to ${label(sl)}.`);
  } catch (e) { status(e); }
};
$("pull").onclick = () => pull().catch(status);
$("pname").oninput = (e) => {
  const at = e.target.selectionStart, n = setName(e.target.value);
  if (e.target.value !== n) { e.target.value = n; e.target.setSelectionRange(at, at); }
};
$("pname").onkeydown = (e) => { if (e.key === "Enter") { e.preventDefault(); $("save").click(); } };
$("reset").onclick = async () => {
  const sl = +$("slot").value;
  try { await dev.resetPreset(sl); userNames[sl - 64] = null; buildPresetList(); $("slot").value = sl; status(`${label(sl)} emptied.`); }
  catch (e) { status(e); }
};
const addStep = (v) => { if (patch[O_STEPS] >= 16) return; if (!patch[O_STEPS]) patch[O_ROOT] = 60; patch[O_STEP + patch[O_STEPS]++] = v; pushPatch(); };
$("stepadd").onclick = () => addStep(patch[O_STEPS] ? patch[O_STEP + patch[O_STEPS] - 1] || 64 : 64);
$("steprest").onclick = () => addStep(0);
$("stepdel").onclick = () => { if (patch[O_STEPS]) { patch[O_STEPS]--; pushPatch(); } };
$("stepclear").onclick = () => { patch[O_STEPS] = 0; pushPatch(); };
let th = 2;
try { const s = localStorage.getItem("crafty-theme"); if (s !== null) th = +s; } catch (e) { /* no storage */ }
$("theme").value = th;
setTheme(th);
addEventListener("resize", () => drawAll());
/* the plugin's window: 0.7's layout, drawn at one width and zoomed to the window's width, so dragging the corner makes
   the whole GUI bigger or smaller (Serum-style) instead of moving the panels; the page scrolls down as in 0.7 */
const DESIGN_W = 1240;
function applyScale() {
  if (!inPlugin) return;
  document.body.style.width = `${DESIGN_W}px`;
  document.documentElement.style.zoom = String(Math.max(0.3, Math.min(4, innerWidth / DESIGN_W)));
}
/* the resize handle in the bottom-right corner (Serum-style): the plugin's window can't be dragged by an edge of its
   own (the page covers it), so dragging this asks the plugin for a new size; the zoom follows the width */
function resizeHandle() {
  const h = el("div", { class: "resizer", title: "Drag to resize" });
  let sx = 0, sy = 0, sw = 0, shh = 0, want = null, busy = false;
  const send = () => {
    busy = false;
    if (want) window.__JUCE__.backend.emitEvent("crafty-resize", want);
  };
  h.onpointerdown = (e) => {
    e.preventDefault();
    h.setPointerCapture(e.pointerId);
    sx = e.screenX; sy = e.screenY; sw = innerWidth; shh = innerHeight;   // the window's size in screen pixels
  };
  h.onpointermove = (e) => {
    if (!h.hasPointerCapture(e.pointerId)) return;
    want = { w: Math.round(sw + e.screenX - sx), h: Math.round(shh + e.screenY - sy) };
    if (!busy) { busy = true; requestAnimationFrame(send); }
  };
  h.onpointerup = (e) => { h.releasePointerCapture(e.pointerId); want = null; };
  document.body.append(h);
}
if (inPlugin) {
  addEventListener("resize", applyScale);
  applyScale();
  resizeHandle();
}
if (inPlugin) {
  document.body.classList.add("plugin");
  $("save").textContent = "SAVE";
  $("pull").textContent = "RELOAD";
  connect();
} else factory(0);
fetch("crafty.wasm").then((r) => r.arrayBuffer()).then((b) => WebAssembly.instantiate(b, {})).then(({ instance }) => {
  wasm = instance.exports;
  refresh();
});
