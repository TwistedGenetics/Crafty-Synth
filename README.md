# Crafty Synth

A firmware for the **M-VAVE FM-1** that turns it into a monophonic wavetable synth after the **Modal CRAFTsynth 2.0**
(its manual, OS 1.4): the same engine layout, the same controls, the same MIDI CC chart. On the FM-1 you get 27 keys,
a big screen and seven knobs to drive it.

**Also as an AU / VST3 plugin for macOS and an MPC plugin**, the same engine, released together: [docs/PORTS.md](docs/PORTS.md).

**Status: 0.15 beta, not yet tried on a real FM-1.** Read [docs/RECOVERY.md](docs/RECOVERY.md) before installing.

- **Editor:** [twistedgenetics.github.io/Crafty-Synth/editor](https://twistedgenetics.github.io/Crafty-Synth/editor/):
  every value, a preview running the firmware's own engine (WebAssembly), presets to and from the FM-1, and **Serum /
  Vital wavetable import**. The sound works in any modern browser; talking to the FM-1 needs Chrome or Edge.
- **Flasher:** [twistedgenetics.github.io/Crafty-Synth/flasher](https://twistedgenetics.github.io/Crafty-Synth/flasher/)
- **Firmware:** `Crafty-Synth-X.Y.fwsc` on [Releases](https://github.com/TwistedGenetics/Crafty-Synth/releases/latest)
- **Manual:** [docs/MANUAL.md](docs/MANUAL.md)

## The engine (as the CRAFTsynth's)

- **8 oscillators**: 4 on Wave 1, 4 on Wave 2, each sweeping smoothly through a bank of 5 waves.
- **9 factory banks** in the CRAFTsynth's layout (analogue, two digital, math, polygon, two curated, formant, and a sine /
  noise bank on Wave 2). The waves are Crafty Synth's own; Modal's aren't copied. There are also **32 user banks** you
  fill from WAV files.
- **16 oscillator modifiers**, with Wave 1 the carrier and Wave 2 the modulator: phase mod, window sync, ring mod,
  triangle fold, de-rez, rise/run phaseshaper, glitchy sine, window amp sync, FM, hard sync, min mod, sine fold, bit
  crush, scrunch, lo-fi phaseshaper and vocal sync.
- **OSC B modifier** (beyond the CRAFTsynth): FM, PM, sync or ring from Wave 1, or a wavefold, with its own amount.
- **Sub oscillator** (beyond the CRAFTsynth): a sine, triangle or square one or two octaves down, before the filter.
- **SPREAD**: unison detune in the first half; past the middle, chord shapes (major, minor, maj6, sus4, 5ths, 5th+oct,
  octave stacks).
- **Filter**: 12 morphable types after Mega Synth (SVF, ladder, MS-20, TB-303, SEM, Steiner, Polivoks, formant,
  comb, phaser, dual peak, decimator), with a bipolar filter EG.
- **3 ADSR envelopes** (0–4 s) for filter, amp and mod. The amp EG amount is bipolar; at 0 the synth drones.
- **2 LFOs**: sine → triangle → saw → square → S&H, retrigger / free / single, tempo-synced if you want. LFO 2's top
  half locks to the note (root/8 … root×3) up into audio rate.
- **Mod matrix**: 8 sources (LFO 1, LFO 2, mod EG, note, velocity, mod wheel, expression, aftertouch), one
  destination each out of 43, bipolar depth. Assign from the panel the CRAFTsynth way: hold LFO (or SEL for the mod
  EG) and turn a knob.
- **Glide**: always, or legato-only (fingered).
- **Effects**: distortion, EraCrush (SP-1200 / MPC60 / S950 / S1000 / Amiga), phaser, a tape-style delay (to 0.74 s or
  synced), chorus, the Horizon reverb (Super-Massive-style, six modes, up to 30 s) and OTT.
- **Sequenced arpeggiator**: it arpeggiates the held notes upward, or plays a pattern of up to 16 steps (notes and
  rests) from the last note held. It follows its own tempo or MIDI clock.
- **64 factory presets** in 8 banks of 8 plus **128 user slots**. The factory presets: classics, bass, jungle (hoover, rave and Phrygian stabs, reese, neuro, wobble), leads,
  chords, drones, one per modifier, and arp-seq patterns.

Every parameter is a MIDI CC with the **CRAFTsynth 2.0's own numbers**, so a CRAFTsynth controller mapping drives it.
The FM-1 sends the CC of every knob you turn.

## Build

Every push builds in GitHub Actions (`.github/workflows/build.yml`). To build locally on Linux x86-64:

```sh
git clone --recurse-submodules https://github.com/TwistedGenetics/Crafty-Synth.git && cd Crafty-Synth
sh tests/run_host.sh                         # host tests: cc, python3 (Pillow, fonttools, numpy), node
sh tools/build_wasm.sh                       # the editor's engine (clang + wasm-ld)
eval "$(tools/get_deps.sh)"                  # JieLi toolchain + 3 AC79 SDK files
python3 tools/build_fm1.py --release 0.1     # -> build/fm1/Crafty-Synth-0.1.fwsc
```

The firmware runs on X0X's FM-1 platform (pinned in `x0x-firmware/`), built the way FM1-Chord, DREADNOUGHT and Carnac
OS build. See [docs/UPSTREAM.md](docs/UPSTREAM.md) for credits and licences.

GPL-3.0. Not affiliated with Modal Electronics or M-VAVE. CRAFTsynth is Modal Electronics' product; this firmware
follows its published manual's design.
