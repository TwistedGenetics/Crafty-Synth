# Changelog

## 0.15 beta (2026-11-23)

- **Your wavetables come from a folder, as on the Twisted Tracker** (AU / VST3 and MPC): the 32 user bank slots are
  gone from their bank lists, which now hold the factory banks only. Put WAVs in the Wavetables folder (sub-folders
  too) and pick them with TABLE FILE under each oscillator.
  - MPC: TABLE FILE steps through the folder's files in name order (folders together); the project keeps the file by
    its path, and new files are picked up while you browse.
  - AU / VST3: the TABLE FILE list shows the folder, grouped by sub-folder, and the oscillator's 3D view now draws
    the file's own waves. The USER WAVETABLES importer is gone from the plugin window.
  - FM-1 and the editor in the browser: unchanged (the FM-1 has no folder: its 32 user banks stay, filled from the
    editor). Patches that already use a user bank still play it.
- **MPC: short page names** in the tab bar: OSC, FILTER, ENVELOPE, MOD, MATRIX, FX, PRESET (the long ones ran into each
  other).

## 0.14 beta (2026-11-23)

- **MPC: the AU / VST3's look, and the 3D wavetable views.** The MPC skin is now drawn like the editor and the plugin
  window (SERUM colours, rounded panels, each control in its own cell, Barlow titles, the editor's knobs: grey track,
  blue arc, white pointer).
  - A new first page, **OSC**, is the editor's two oscillator panels: the A / B badge, the bank list in the head, the
    bank's wavetable drawn in 3D (its frames stacked in depth, the one playing bright and filled, as in the editor)
    and below it the controls. The view follows WT POS (16 steps), the bank and a table file; a user bank or a table
    file shows YOUR WAVETABLE (its waves are only known on the MPC). Touching the view moves WT POS.
  - OSC A: WT POS, OSC MOD, MOD MODE, TABLE FILE, MIX, SPREAD, GLIDE, OCTAVE; OSC B: WT POS, TUNE, FINE, B MOD,
    B MODE, TABLE FILE (the TABLE FILES page is gone: its two controls are here).
  - FM-1, editor and AU / VST3: unchanged.

## 0.13 beta (2026-11-23)

Lined up with the Twisted Tracker's CRAFTY engine (its v1.17 / v1.18 changes), so all of them behave the same.

- **RANDOM on every version**, the Tracker's rules: a random preset (never an empty user slot) varied so it stays
  playable: knob values pushed from it by up to a quarter (now and then 60%) of their range, a third of the switches
  changed, a random factory bank on each oscillator; volume, octave, glide, amp EG amount, the arp-seq, delay time and
  feedback and the reverb's mode, size and decay left alone; no matrix destination that holds the amp open or moves a
  bank. KEEP keeps your banks (and files).
  - FM-1: on the SAVE page, REC; TABLES (K2) NEW / KEEP.
  - Editor and AU / VST3: RANDOM (shift-click keeps the tables). It used to randomise a dozen values over their whole
    range, banks included (empty user banks too).
  - MPC: a RANDOM button and RND TABLES on the PRESET page.
- **Wavetable files**, as the Tracker's `/Crafty/Wavetables`: WAVs in a Wavetables folder (sub-folders too) get a
  number for good in `TableIndex.txt` (the same file format everywhere), and TABLE 1 / TABLE 2 play one in place of
  an oscillator's bank.
  - AU / VST3: `.../Crafty Synth/Wavetables`, a TABLE FILE list under each oscillator; the project keeps the path.
  - MPC: `data/Wavetables`, TABLE 1 / TABLE 2 on their own page, shown by name.
  - RANDOM (NEW) gives each oscillator one of your files half the time, as on the Tracker.
- **One WAV importer**: the editor's (JavaScript) and the Tracker's (C++) are now one C file every version uses
  (`src/host/cr_wav.c`); the editor runs it in WebAssembly. Same results as before (within 1 in 26000). It also reads
  WAVE_FORMAT_EXTENSIBLE files; frames over 4096 samples are refused, as on the Tracker.
- Engine (for the Tracker): bank builds can wait for the host's main loop instead of the audio thread
  (`defer_tables`, `cr_inst_tables()`); a host's wavetable file can stand in for a bank (`cr_inst_file_table()`). The
  playing bank's noise flag now follows the bank that is actually built. Off by default: the FM-1, editor and plugins
  play as before.

## 0.12 beta (2026-11-23)

- **Sub oscillator** on every version: SUB LVL, SUB OCT (-1 / -2) and SUB WAVE (sine, triangle, square, its edges
  smoothed), one or two octaves under OSC A, mixed in before the filter. All three are mod destinations (77 now).
  - FM-1: its own page, HOME's fifth (SUB), the wave drawn as tall as its level, big text.
  - Editor and AU / VST3: in the VOICE panel's free space (nothing else moves).
  - MPC: a SUB OSC frame on the filter's page, its two lists as pop-ups.
- The basses use it: WARM BASS, SUB BASS, REESE, ROLLER, DREAD, DONK, WOBBLE, MIN REESE, NEURO, DUB SEQ.
- MPC fix: since 0.8 the matrix's destination lists stopped at 64 of 74 on the MPC; all 77 are there now, and the
  build fails if a list ever outgrows its room. The MPC's pop-ups line up with their row's knobs.
- Older patches load with the sub off.

## 0.11 beta (2026-11-23)

- **The AU / VST3 window resizes from a handle in its bottom-right corner** (the three lines), Serum-style: drag it
  and the window and the whole GUI grow or shrink together. In 0.10 the window's own corner sat under the editor and
  couldn't be grabbed. Hosts that resize plugin windows by their edges do that too.

## 0.10 beta (2026-11-23)

- **The AU / VST3 window is 0.7's layout again**, and dragging the window's corner zooms the whole GUI with it,
  Serum 2-style, instead of moving the panels around. The page scrolls down as it did in 0.7. 0.9's landscape
  layout is gone. The project keeps the window's size.
- Kept from 0.8: NAME beside SAVE, and every parameter as a mod destination.

## 0.9 beta (2026-11-23)

- **The AU / VST3 window is one GUI again, scaled from its corner**: every panel in one landscape layout (two rows of
  panels, the pattern, keyboard and wavetables below) instead of 0.8's pages, and dragging the window's corner
  scales the whole GUI, keeping its shape, as other synths do. The SIZE menu is gone. The project keeps the size.
- FM-1, editor and MPC: unchanged.

## 0.8 beta (2026-11-22)

- **Every parameter is a mod destination** (74, was 44), on the FM-1, the editor, the plugin and the MPC: banks,
  modes, tune / fine / octave, LFO modes and sync, delay sync, every source's depth, the arp-seq's on/off, tempo and
  rate, filter type, all of the reverb, chorus and phaser rates, crush era. Stepped values step as the source moves;
  a modulated bank is built as if it had been turned. Old patches keep their destinations.
- **Name your presets** in the editor and the plugin: NAME, beside SAVE (the FM-1 names them on its SAVE page).
- **The plugin's window scales**: pages like the FM-1's, the keyboard always in view, SIZE (WHOLE / WIDTH /
  60-150%), a resizable window, both kept with the project.

## 0.7 beta (2026-11-22)

- **MPC: Akai's official plugin format (MPC OS 3.10.1+).** `Crafty-Synth-0.7-mpc.zip` is one folder for every MPC and
  the Force, Gen1 and Gen2: turn on External Plugin Support, copy it into a `VST2 Plugins` folder, restart. No root.
  Made with Akai's MPC Plugin SDK (its converter and validator) from the same builds; the community-installer zips
  stay for older MPC OS.
- FM-1 and the Mac plugin: unchanged.

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
