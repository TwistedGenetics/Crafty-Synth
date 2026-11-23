// SPDX-License-Identifier: GPL-3.0-only
// Crafty Synth editor: a parameter's value as text, as the FM-1's screen shows it (src/app/cr_app.c, cr_ptext).
// Copyright (C) 2026 Jamie (TwistedGenetics).
import M from "./meta.js";

const exp2 = (x) => Math.pow(2, x);
const time = (s) => (s < 1 ? `${Math.round(s * 1000)} ms` : `${s.toFixed(1)} s`);
const L1SYNC = ["1/16", "1/8", "3/16", "1/4", "3/8", "1/2", "3/4", "1", "3/2", "2", "3", "4", "6", "8", "12", "16"];
const L2SYNC = ["1/16", "1/8", "1/4", "1/2", "1", "5/4", "2", "4"];
const L2ROOT = ["ROOT/8", "ROOT/4", "ROOT/2", "ROOT", "ROOT x1.5", "ROOT x2", "ROOT x2.5", "ROOT x3"];
const DSYNC = ["1/16", "1/12", "1/8", "1/8D", "1/4", "1/2", "3/4", "4/4", "6/4"];
const ARPRATE = ["1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"];
export const PI = Object.fromEntries(M.params.map((p) => [p.key, p.id]));

export function bankOf(v, wave2) {
  const n = wave2 ? 41 : 40;
  let b = Math.floor((v * n) / 128);
  if (!wave2 && b >= 8) b++;
  return b;
}
export function bankVal(b, wave2) {
  const n = wave2 ? 41 : 40;
  const i = !wave2 && b > 8 ? b - 1 : b;
  return Math.floor((i * 128 + 64) / n);
}
export function semiOf(v) {
  const d = v - 64;
  return d >= 0 ? Math.floor((d * 48 + 31) / 63) : -Math.floor((-d * 48 + 31) / 64);
}
export const band = (v, n) => (v * n) >> 7;
export const bandVal = (i, n) => Math.floor((i * 128 + 64) / n);
const BMODE = ["FM FROM A", "PM FROM A", "SYNC TO A", "RING WITH A", "WAVEFOLD"];
const FTYPE = ["SVF", "LADDER", "MS-20", "TB-303", "SEM", "STEINER", "POLIVOKS", "FORMANT", "COMB +/-", "PHASER", "DUAL PEAK", "DECIMATOR"];
const VMODE = ["ROOM", "HALL", "HALO", "SWELL", "ABYSS", "DUB"];
const ERA = ["OFF", "SP-1200", "MPC60", "S950", "S1000", "AMIGA"];
const SUBOCT = ["-1 OCT", "-2 OCT"];
const SUBWAVE = ["SINE", "TRIANGLE", "SQUARE"];
export const LISTS = { BMODE, FTYPE, VMODE, ERA, SUBOCT, SUBWAVE };
export function spreadShape(v) { return v < 64 ? 0 : Math.min(9, Math.floor((v - 64) / 7) + 1); }

/* the choices a list-like parameter has: [value, label] (the middle of each band, as the firmware steps) */
export function choices(p) {
  switch (p.kind) {
    case "BANK": {
      const w2 = p.key === "W2BANK";
      return M.banks.map((n, b) => [bankVal(b, w2), `${b + 1} ${n}`]).filter((_, b) => w2 || b !== 8);
    }
    case "OMMODE": return M.mods.map((n, i) => [i * 8 + 4, `${i + 1} ${n}`]);
    case "LMODE": return [[20, "RETRIGGER"], [63, "FREE"], [105, "SINGLE"]];
    case "ONOFF": return [[0, "OFF"], [127, "ON"]];
    case "DEST": return M.dests.map((n, i) => [i, n]);
    case "ARPRATE": return ARPRATE.map((n, i) => [Math.floor((i * 128) / 6) + 10, n]);
    case "BMODE": case "FTYPE": case "VMODE": case "ERA": case "SUBOCT": case "SUBWAVE": {
      const l = LISTS[p.kind];
      return l.map((n, i) => [bandVal(i, l.length), n]);
    }
    case "OCT": return [-2, -1, 0, 1, 2, 3, 4].map((o) => [Math.floor(((o + 2) * 128 + 64) / 7), (o > 0 ? "+" : "") + o]);
    default: return null;
  }
}

export function fmt(p, v, patch) {
  switch (p.kind) {
    case "BIP": return (v > 64 ? "+" : "") + (v - 64);
    case "MIX": return v === 0 ? "WAVE 1" : v === 127 ? "WAVE 2" : `${Math.round(100 - (v / 127) * 100)} : ${Math.round((v / 127) * 100)}`;
    case "TIME": { const t = v / 127; return time(0.001 + 4 * t * t * t); }
    case "PCT": return `${Math.round((v * 100) / 127)} %`;
    case "BANK": return M.banks[bankOf(v, p.key === "W2BANK")];
    case "OMMODE": return M.mods[v >> 3];
    case "SPREAD": return v < 64 ? (v ? `UNISON ${v}` : "OFF") : M.shapes[spreadShape(v)];
    case "SEMI": { const s = semiOf(v); return `${s > 0 ? "+" : ""}${s} semi`; }
    case "CENT": { const c = Math.trunc(((v - 64) * 100) / 63); return `${c > 0 ? "+" : ""}${c} cents`; }
    case "OCT": { const o = ((v * 7) >> 7) - 2; return (o > 0 ? "+" : "") + o; }
    case "GLIDE": {
      if (v === 64) return "OFF";
      const g = Math.abs(v - 64) / 63;
      return `${v > 64 ? "ALWAYS" : "LEGATO"} ${time(2.5 * g * g)}`;
    }
    case "HZ": { const f = 20 * exp2((v * 10) / 127); return f < 1000 ? `${Math.round(f)} Hz` : `${(f / 1000).toFixed(1)} kHz`; }
    case "MORPH": return v < 22 ? "LOW PASS" : v < 50 ? "LOW SHELF" : v < 78 ? "BAND PASS" : v < 106 ? "BP TO HP" : "HIGH PASS";
    case "LSHAPE": return ["SINE", "TRIANGLE", "SAW", "SQUARE", "S+H"][Math.round((v * 4) / 127)];
    case "LMODE": return v < 42 ? "RETRIGGER" : v < 84 ? "FREE" : "SINGLE";
    case "ONOFF": return v >= 64 ? "ON" : "OFF";
    case "LRATE1":
      if (patch[PI.L1SYNC] >= 64) return `${L1SYNC[(v * 16) >> 7]} / bar`;
      return `${(0.02 * exp2((v * 10.64) / 127)).toFixed(2)} Hz`;
    case "LRATE2":
      if (v >= 64) return L2ROOT[((v - 64) * 8) >> 6];
      if (patch[PI.L2SYNC] >= 64) return `${L2SYNC[(v * 8) >> 6]} / beat`;
      return `${(0.02 * exp2((v * 2 * 10.64) / 127)).toFixed(2)} Hz`;
    case "DTIME":
      if (patch[PI.DSYNC] >= 64) return DSYNC[(v * 9) >> 7];
      return time(0.0005 * exp2((v * 10.55) / 127));
    case "TEMPO": return `${40 + 2 * v} BPM`;
    case "ARPRATE": return ARPRATE[(v * 6) >> 7];
    case "DEST": return M.dests[v] || "NONE";
    case "BMODE": case "FTYPE": case "VMODE": case "ERA": case "SUBOCT": case "SUBWAVE": { const l = LISTS[p.kind]; return l[band(v, l.length)]; }
    default: return String(v);
  }
}
