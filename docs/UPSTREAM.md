# Where Crafty Synth's code comes from

Everything is GPL-3.0-only unless a file says otherwise.

**Ours** (Copyright (C) 2026 Jamie / TwistedGenetics): the patch and arp-seq (`src/core/`), the engine and wavetable
band-limiting (`src/dsp/`), the app and its editor commands (`src/app/`), the WebAssembly wrapper (`src/wasm/`), the
editor (`web/editor/`), the factory wavetables and presets and their generators (`tools/gen_wavetables.py`,
`tools/gen_presets.py`, `tools/gen_cr_font.py`), the host harness and tests, the docs.

**The design** follows the Modal CRAFTsynth 2.0's user manual (OS 1.4): its engine layout, its controls and its MIDI
CC numbers, as facts, for compatibility. No Modal code or wavetable is used: the waves and the DSP are written here.

**The platform** is [X0X](https://github.com/charlesvestal/fm1-x0x) by Charles Vestal, a git submodule
(`x0x-firmware/`) pinned to one commit, used the way DREADNOUGHT and Carnac OS use it. Nothing in it is edited: the
build takes X0X's `firmware/src/x0x.c` up to the line where its own app starts and puts ours after it. From X0X (and
through it from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita / Hügelton Instruments, and Melodee):
the HAL, the boot-loop guard and safe mode, the crash screen and watchdog, the panel map and its calibration, the
screen driver, the canvas and fonts, USB MIDI and USB audio, TRS MIDI in, flash storage, the update entry and the
update loader, the package maker and the build checks.

| Part | From | Version | Licence |
|---|---|---|---|
| Platform (`x0x-firmware/`, submodule) | X0X | `5048230b86254581d991121916ede3de0d0c91f6` (the commit DREADNOUGHT 0.44 pins) | GPL-3.0-only (fonts: SIL OFL 1.1, see X0X's `LICENSING.md`) |
| Device glue (`src/hardware/cr_main_fm1.c`) | FM1-Chord's `fc_main_fm1.c`, from DREADNOUGHT's `dn_main_fm1.c`, after Carnac OS and X0X | FM1-Chord 0.8 | GPL-3.0-only |
| Build script (`tools/build_fm1.py`), start-up preflight (`tools/fm1_preflight.py`), deps (`tools/get_deps.sh`) | FM1-Chord, from DREADNOUGHT, after Carnac OS | FM1-Chord 0.8 | GPL-3.0-only |
| Update-loader fix (`tools/cr_patches.py`) and its test (`tests/host/ldr_cut_test.c`) | DREADNOUGHT 0.43, after Skiff's loader | | GPL-3.0-only |
| Web flasher (`web/flasher/`), its update-protocol modules (`vendor/`) | DREADNOUGHT's flasher; SLOOP / Felucca `fm1ota.js`, `fm1pkg.js` | SLOOP v2.8.1 | GPL-3.0-only |
| Pages site and workflows (`tools/make_site.py`, `.github/workflows/`) | DREADNOUGHT | | GPL-3.0-only |
| Big font (generated) | Barlow Semi Condensed Bold, in X0X's `assets/fonts` | | SIL OFL 1.1 |
| SVF filter, phaseshaping | the published methods (Simper's trapezoidal SVF; Kleimola et al.'s phaseshaping), written again here | | GPL-3.0-only |

## Taking platform fixes

Move the submodule to a newer X0X commit, rebuild, run the host tests and the build checks, record the commit above.
`tools/build_fm1.py` and `tools/cr_patches.py` refuse to build if the lines they cut or replace have changed.
