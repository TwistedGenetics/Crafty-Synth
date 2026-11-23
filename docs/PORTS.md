# Crafty Synth as a plugin: Mac (AU / VST3) and MPC

The FM-1's synth, the same engine code, as a plugin on your computer and on an MPC. A preset sounds the same on
all three. Every release carries all three builds (Releases page):

| File | For |
|---|---|
| `Crafty-Synth-X.Y.fwsc` | the M-VAVE FM-1 (install with the [web flasher](https://twistedgenetics.github.io/Crafty-Synth/flasher/)) |
| `Crafty-Synth-X.Y-mac.zip` | macOS 11 or later, Apple Silicon and Intel: AU, VST3 and a standalone app |
| `Crafty-Synth-X.Y-mpc.zip` | **every MPC and the Force on MPC OS 3.10.1+** (Akai's official plugin format) |
| `Crafty-Synth-X.Y-mpc-armv7.zip`, `-mpc-aarch64.zip` | older MPC OS with root: the community MPC plugin installer (Gen1 / Gen2) |

**Status: built and tested in CI, not yet tried in a DAW or on an MPC.**

## Mac: AU / VST3

**Install:** unzip, then follow `INSTALL.txt`: copy `Crafty Synth.component` to `~/Library/Audio/Plug-Ins/Components`
and `Crafty Synth.vst3` to `~/Library/Audio/Plug-Ins/VST3`, clear the download quarantine once (`xattr -dr
com.apple.quarantine ...`: the plugin isn't notarised), and rescan in your DAW. In Ableton Live it is under
Plug-Ins → Twisted Genetics → Crafty Synth.

**The window is the web editor**, in the same SERUM look and layout: both oscillators with their wavetables, the
filter, envelopes, LFOs, the matrix, every effect, the arp-seq pattern, the wavetables and the keyboard (scroll down for
the lower panels). It is always connected: the preset list,
**NAME**, **SAVE** (to a user slot), **RELOAD** and **EMPTY SLOT** work on the plugin the way they work on an FM-1.

- **Naming a preset:** type in **NAME** (12 characters, as the FM-1 shows them: capitals, digits, space, a little
  punctuation), pick a user slot, **SAVE** (or Enter in the name).
- **Size:** drag the handle in the bottom-right corner (the three lines): the window and the whole GUI grow or shrink
  together (Serum 2-style; the zoom follows the width), without the panels moving. The project keeps the size.

- **Every patch value is a host parameter** (79, named as on the FM-1's pages: "FILT EG ATTACK", "REVERB SIZE"...),
  showing its value the FM-1's way ("2.1KHZ", "1/8D", "LADDER"). Automate them, map them to a controller.
- **Presets** are the host's programs: 64 factory, then the 128 user slots (U1-1 … U16-8).
- **User presets and wavetables** live in one file, shared by every instance and every DAW:
  `~/Library/Application Support/Twisted Genetics/Crafty Synth/Crafty Synth.user`. It is the FM-1's user area byte
  for byte.
- **The arp-seq locks to the DAW**: with the transport running, steps fall on the beat; stopped, it runs at the
  DAW's tempo. The delay's sync follows it too.
- **MIDI**: notes, the CRAFTsynth's CC chart (the same CCs as the FM-1), pitch bend, mod wheel, aftertouch, sustain,
  program change. The project saves the patch you're playing (and which preset it came from).
- Any sample rate: the engine runs at 44.1 kHz like the FM-1 and is resampled to the DAW's rate.
- **Your wavetables** (0.15, as on the Twisted Tracker): put WAVs (Serum and Vital tables, single cycles,
  sub-folders too) in `~/Library/Application Support/Twisted Genetics/Crafty Synth/Wavetables`. **TABLE FILE** under
  each oscillator lists the folder (grouped by sub-folder) and plays the one you pick in place of its bank ("(the
  bank)" to go back), its waves drawn in the oscillator's 3D view; the list re-reads the folder when you open it. The
  bank lists hold the factory banks only, and the 32-slot importer is gone from the plugin window. Every file gets a number for good in `TableIndex.txt` next to the folder, the Tracker's and the MPC's format:
  copy both between them and the numbers match. The project keeps the file by its path. A file plays as the five
  waves the importer picks (evenly through the table); the matrix can't move a file as it moves a bank.
- **RANDOM** picks a preset and varies it (the FM-1's rules, docs/MANUAL.md); half the time each oscillator also gets
  one of your wavetable files. Shift-click keeps the banks and files you have.

## MPC

**The easy way: MPC OS 3.10.1 or later, any MPC or Force.** Akai opened MPC OS to third-party plugins in 3.10.1
(its MPC Plugin SDK), and `Crafty-Synth-X.Y-mpc.zip` is in that official format: one folder that works on Gen1
(32-bit: MPC One / One Retro, One+, Live, Live II, X, X SE, Key 61, Key 37, Force) and Gen2 (64-bit: MPC Live III,
MPC XL, One Gen 2, Key 37 Gen 2). No root, no installer.

1. On the MPC: **Preferences → Enhancements (Beta)**, turn on **Enable Enhancements** and **External Plugin Support**.
2. Unzip `Crafty-Synth-X.Y-mpc.zip`. Copy the folder `Twisted Genetics - VST - Crafty Synth` into a folder named
   exactly `VST2 Plugins` at the root of a USB stick, an SD card or the MPC's internal drive:
   `<drive>/VST2 Plugins/Twisted Genetics - VST - Crafty Synth/`.
3. Restart the MPC. Crafty Synth is in the plugin browser under **External Plugins**; add it to a plugin track.

To update, replace that folder (keep its `data` folder if you put your user file in it).

**Older MPC OS** (Gen1 with root / SSH): the `-mpc-armv7.zip` and `-mpc-aarch64.zip` builds are the same plugin
packaged for the community [MPC plugin installer](https://github.com/sd88me/mpc-vst-plugins).

A project saved on one MPC opens on any other.

**Pages** (Q-Links follow the page, 16 per page on two banks):

| Page | Controls |
|---|---|
| OSC | the editor's two oscillator panels: each bank's wavetable in 3D (as the editor draws it, following WT POS), the bank list in the head · OSC A: WT POS, OSC MOD, MOD MODE, TABLE FILE, MIX, SPREAD, GLIDE, OCTAVE · OSC B: WT POS, TUNE, FINE, B MOD, B MODE, TABLE FILE |
| FILTER | sub level, octave, wave · cutoff, reso, morph, type, filter EG, distortion |
| ENVELOPE | the filter envelope · the amp envelope, amp EG amount, volume |
| MOD | the mod envelope · LFO 1 · LFO 2 (rate, shape, mode, sync) |
| MATRIX | one destination and depth per source (LFO 1 - NOTE, then VELO - AFTTCH) |
| FX | the Horizon reverb (mode, size, decay, mix, warp, density, mod) · the delay · chorus, phaser |
| PRESET | era, crush, OTT · bank, slot, preset, RANDOM, RND TABLES · arp, tempo, rate |

**The look** (0.14) is the editor's and the plugin window's: the SERUM colours, rounded panels, each control in its
own cell, the same knobs. The 3D view shows the bank playing (a user bank or a table file shows YOUR WAVETABLE: its
waves are only known on the MPC) and follows WT POS in 16 steps; touching it moves WT POS.

- **Presets**: BANK (1-8 factory, U1-U16 user) and SLOT step one at a time; PRESET runs through all 192, and MPC's
  own PRESET menu lists them by name.
- **Values read as on the FM-1** (cutoff in Hz, delay in divisions, LADDER, HALL...). Lists (banks, modifiers,
  filter types, reverb modes, matrix destinations) open as pop-ups.
- **Your user presets**: copy `Crafty Synth.user` from your Mac (path above) into the plugin's `data`
  folder on the MPC (`VST2 Plugins/Twisted Genetics - VST - Crafty Synth/data/`).
- **Your wavetables** (0.15, as on the Twisted Tracker): put WAVs in `data/Wavetables` (sub-folders too). **TABLE FILE**
  on the OSC page steps through them in name order, folders together (0: the bank), and plays the one shown in place of
  that oscillator's bank; the bank lists hold the factory banks only. The project keeps the file by its path. Files you
  add are picked up as you browse (at most every 20 s). `data/TableIndex.txt` numbers them as the Tracker and the Mac do.
- **RANDOM** (a button on the PRESET page) picks a preset and varies it, as the FM-1's; **RND TABLES** = KEEP leaves
  the banks and files alone, NEW gives each oscillator a random factory bank and, half the time, one of your files.
- **The arp-seq follows the MPC's tempo and transport.** The MPC project keeps the patch.

## How it's built

`src/host/cr_inst.c` is the engine as an instance (the FM-1's synth, arp, wavetables and patch code) and
`src/host/cr_proto.c` answers the editor's protocol (docs/PROTOCOL.md) over a user area in RAM; the editor's
WebAssembly preview, the AU / VST3 and the MPC plugin all use them. `ports/plugin` is the JUCE wrapper and
`ports/mpc` the MPC toolkit's engine interface, with its parameters and skin made from the engine's own tables
(`make_port.py`: the OSC page by hand, the others by the toolkit's auto-layout, all in `crafty.css`'s look; the 3D
views are `make_views.py`'s filmstrips, `make_port.py --views` redraws them); Akai's SDK (`external/mpc-plugin-builder-sdk`, `legacy_convert.py` and `validate.py`) turns its two
builds into the official package. CI builds the FM-1 first, then the plugins (CLAUDE.md).

0.13 shares three more pieces with every version and the Twisted Tracker's CRAFTY: `src/host/cr_wav.c` (WAV files to
user banks or wavetable files; the editor runs it through WebAssembly), `src/host/cr_tables.c` (the numbered
wavetable files, `TableIndex.txt`) and `cr_patch_random()` in `src/core/cr_patch.c` (RANDOM). `cr_inst` can also
defer bank builds out of the audio thread (`defer_tables`, `cr_inst_tables()`: the Tracker needs it) and play a
host's wavetable file in place of a bank (`cr_inst_file_table()`).
