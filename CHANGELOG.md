# Changelog

## 0.6 beta (2026-11-22)

- **MPC plugin for Gen2 MPCs** (MPC Live III and kin, 64-bit ARM, MPC OS 3.x): `Crafty-Synth-0.6-mpc-aarch64.zip`,
  beside the Gen1 / Force build (`-mpc-armv7.zip`). The same plugin, skin, presets and saved projects on both.
- FM-1 and the Mac plugin: unchanged (rebuilt with the release, as every release carries all of them).

## 0.5 beta (2026-11-22)

Crafty Synth in three forms, built from the same engine and released together, the FM-1 first (docs/PORTS.md):

- **AU / VST3 / standalone for macOS** (Apple Silicon and Intel). The window is the editor; every patch value is a
  host parameter showing the FM-1's value text; 192 programs; user presets and wavetables in one shared file (the
  FM-1's user area format); the arp-seq locks to the DAW's beat; any sample rate.
- **MPC plugin** for Gen1 MPCs and the Force, through the community MPC toolkit: every value on six pages in the
  SERUM look, presets by bank and slot and in MPC's PRESET menu, the MPC's tempo and transport, your user file.
- FM-1: unchanged in use. Its value text and user-area format moved into shared code (src/core) so the plugins
  read the same; the arp-seq's callbacks gained an instance for the plugins.
- Editor: the working patch is sent to the working slot (0.4 sent it to U8-8: INIT, RANDOM and pattern edits could
  overwrite that slot on the FM-1). The preview runs the shared engine instance.

## 0.4 beta (2026-11-22)

- **OSC B modifier**: its own slot beside OSC A's 16 modifiers. B MODE picks FM from A, PM from A, sync to A, ring with
  A or a wavefold; B MOD sets how much (and is a matrix destination).
- **12 filter types** after Mega Synth, each still morphing with MORPH: SVF, ladder, MS-20, TB-303, SEM, Steiner,
  Polivoks, formant, comb +/-, phaser, dual peak and decimator. TYPE is on the FILTER page.
- **5 new effects on 4 new FX pages**: Horizon (a Super-Massive-style reverb: room, hall, halo, swell, abyss, dub, with
  size, decay up to 30 s, warp, density and modulation), chorus, phaser, OTT, and EraCrush (SP-1200, MPC60, S950,
  S1000 and Amiga). Reverb, chorus, phaser, crush and OTT are matrix destinations.
- **128 user preset slots** (U1-1 … U16-8) beside the 64 factory presets, which can no longer be overwritten. SAVE
  stores into a user slot; saving from a factory preset picks the first empty one.
- **32 user wavetable banks** (was 2).
- Presets and banks you saved in 0.1–0.3 move to U1-1 onward and user banks 1–2 on first start.
- GLO → SETUP shows the audio's CPU load.
- The delay's longest time is now about 0.74 s (the reverb took the rest of its memory).
- The editor speaks protocol 2: it needs the FM-1 on 0.4.
- Editor: each user wavetable slot shows and plays its own waves. Changing slot no longer copies the open WAV into it,
  and on connecting the editor reads the banks already on the FM-1.

## 0.3 beta (2026-11-22)

- SERUM is the default theme (on a new install; GLO -> SETUP -> THEME changes it).
- The editor in the same look (below).

## 0.2 beta (2026-11-22)

- The screen redesigned after Serum 2 and Shrub: a preset bar, OSC A and OSC B panels with each wavetable's frames
  stacked in 3D (the one playing bright and filled), arc knobs, filled envelope, LFO and filter curves.
- Six themes, GLO -> SETUP -> THEME: CRAFT (the CRAFTsynth 2.0's white, black and blue), CRAFT NIGHT, SERUM, ACID,
  AMBER and PAPER.
- The editor in the same look: OSC A / B panels with 3D stacked wavetables, arc knobs (drag, wheel, arrows,
  double-click for the default), filled filter, envelope and LFO curves, and the same six themes (SERUM by default).

## 0.1 beta (2026-11-22)

First version: the CRAFTsynth 2.0's engine on the FM-1.

- 8 wavetable oscillators, 9 factory banks of 5 waves plus 2 user banks, 16 oscillator modifiers, spread (unison and
  9 chord shapes), the morphing 2-pole filter, 3 envelopes, 2 LFOs (LFO 2 into audio rate), an 8-slot mod matrix,
  glide and legato, distortion, delay, the sequenced arpeggiator.
- 64 factory presets in 8 banks.
- The panel: pages for every value, CRAFTsynth-style hold-to-assign for the LFOs and mod EG, a scale mode for the keys,
  latch, save and rename on the device.
- MIDI: the CRAFTsynth's CC chart in and out, program change, pitch bend, sustain, MIDI clock.
- The editor: every value, a preview running the firmware's engine (WebAssembly), presets to and from the FM-1, and
  Serum / Vital / WAV wavetable import into the user banks.
