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

**The window is the web editor**, in the same SERUM look: both oscillators with their wavetables, the filter,
envelopes, LFOs, the matrix, every effect, the arp-seq pattern and the keyboard. It is always connected: the preset
list, **SAVE** (to a user slot), **RELOAD**, **EMPTY SLOT** and the wavetable importer (**STORE**) work on the plugin
the way they work on an FM-1.

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
| OSC A / OSC B | Wave 1, bank 1, mod, mod mode, mix, spread, glide, octave · Wave 2, bank 2, B mode, B mod, tune, fine |
| FILTER / FILTER EG | cutoff, reso, morph, type, filter EG, distortion · attack, decay, sustain, release |
| AMP EG / MOD EG / LFO 1 | the amp envelope, amp EG amount, volume · the mod envelope · LFO 1 |
| LFO 2 / MATRIX | LFO 2 · the matrix, one destination and depth per source |
| REVERB / DELAY | the Horizon reverb (mode, size, decay, mix, warp, density, mod) · the delay |
| MOD FX / CRUSH / PRESET / ARP-SEQ | chorus, phaser · era, crush, OTT · bank, slot, preset · arp, tempo, rate |

- **Presets**: BANK (1-8 factory, U1-U16 user) and SLOT step one at a time; PRESET runs through all 192, and MPC's
  own PRESET menu lists them by name.
- **Values read as on the FM-1** (cutoff in Hz, delay in divisions, LADDER, HALL...). Lists (banks, modifiers,
  filter types, reverb modes, matrix destinations) open as pop-ups.
- **Your user presets and wavetables**: copy `Crafty Synth.user` from your Mac (path above) into the plugin's `data`
  folder on the MPC (`VST2 Plugins/Twisted Genetics - VST - Crafty Synth/data/`).
- **The arp-seq follows the MPC's tempo and transport.** The MPC project keeps the patch.

## How it's built

`src/host/cr_inst.c` is the engine as an instance (the FM-1's synth, arp, wavetables and patch code) and
`src/host/cr_proto.c` answers the editor's protocol (docs/PROTOCOL.md) over a user area in RAM; the editor's
WebAssembly preview, the AU / VST3 and the MPC plugin all use them. `ports/plugin` is the JUCE wrapper and
`ports/mpc` the MPC toolkit's engine interface, with its parameters and skin made from the engine's own tables
(`make_port.py`); Akai's SDK (`external/mpc-plugin-builder-sdk`, `legacy_convert.py` and `validate.py`) turns its two
builds into the official package. CI builds the FM-1 first, then the plugins (CLAUDE.md).
