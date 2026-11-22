# The FM-1, as Crafty Synth uses it

Every fact here was read from existing open FM-1 firmware projects (16 Nov 2026), not assumed. The sources are at the
bottom. "Unverified" means nobody has measured it on a device yet, as far as we know.

## Hardware

| | | Source |
|---|---|---|
| Chip | JieLi **AC791N / WL82**, `pi32v2` CPU, **2 cores**, about 360 MHz as running | FM-1-RE; X0X; DREADNOUGHT `docs/PLATFORM.md` |
| Floating point | single precision is fine in audio code; **no doubles** (software helpers; the build refuses them) | X0X `tools/build.py`; Carnac OS |
| RAM | about 512 KB internal SRAM, no PSRAM. X0X's map: a 96 KB `.data`/`.bss` region and a ~336 KB pool | `fm1_open` linker script; X0X |
| Flash | **1 MB**, one bank, execute in place. App slot ~568 KB; user store objects in `0x97000`–`0xDFFFF` | X0X `tools/fm1pkg_make.py`, `storage.c` |
| Screen | **240 × 240 colour LCD**, RGB565 | `fm1_open` `fm1.h`; felucca-bsp `boards/fm1` |
| Keys | **27 note keys, F3 – G5**, with LEDs (velocity from the keys exists but is off by default) | X0X `app/plat.h` (`NKEYS 27`); felucca-bsp `fm1_input.h` |
| Buttons | **14** with LEDs: FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC, OCT−, OCT+ | X0X `app/plat.h` |
| Encoders | **7**: SELECT, ALGO, PRESET, K1 – K4; plus the **MASTER** pot | X0X `app/plat.h` |
| MIDI | USB-MIDI in and out; TRS (3.5 mm) MIDI in on fitted boards; BLE MIDI in stock firmware (not in X0X's platform) | `fm1_open` README; X0X; fm1-base |
| Audio | **44.1 kHz stereo**, built-in speaker and line out; X0X renders 128-frame blocks; also a class-compliant **USB audio** input to the computer | X0X `main_fm1.c`; Felucca `BUILDING.md` |

## What Crafty Synth does with it

| | |
|---|---|
| Keys | notes F3 – G5 (or a scale from its root), OCT −3 … +3 |
| Screen | light background, dark text; values in 23 px, the knob just turned in 44 px Barlow Bold |
| Synth | one voice of 8 wavetable oscillators (4 pairs), mip-mapped 256-sample tables built in RAM (3 × 20 KB), 2-pole SVF, delay 128 KB; one core |
| Storage | presets: 32 in `OBJ_PAT0`, 32 in `OBJ_PAT1` (96 B each); globals in `OBJ_SOUND`; user wavetables in `OBJ_SONG` and `OBJ_MOTION` (2,576 B each); X0X's calibration (`OBJ_PANEL`) untouched |
| MIDI | notes and the CRAFTsynth's CCs in and out; program change; MIDI clock drives the arp-seq |
| CPU | host measurement (PC): about 120–150 ns a frame for the heaviest patch (spread chord, vocal sync, noise, distortion, delay). The budget at 44.1 kHz is about 8,000 cycles a frame at 360 MHz. On the FM-1: **unverified** |

## Build and update

- The firmware builds on **Linux x86-64** with JieLi's Linux toolchain (clang 4.0.1 for pi32v2) and three files of
  the Apache-2.0 AC79 SDK (`tools/get_deps.sh`): GitHub Actions `ubuntu-latest`. No Windows-only vendor installer.
- Packages are `.fwsc` with X0X's identity scheme `FM-1_9XXYYZZ` (0.1 → `FM-1_9000100`).
- Updates are SysEx over USB-MIDI (Web MIDI in Chrome / Edge): the running firmware stages the update loader, the
  loader writes the image. Crafty Synth's loader writes the app header last (DREADNOUGHT 0.43's fix).

## Sources

| What | Where |
|---|---|
| Keys, buttons, encoders, platform API | [charlesvestal/fm1-x0x](https://github.com/charlesvestal/fm1-x0x) `firmware/src/app/plat.h`, pinned in `x0x-firmware/` |
| Board pins, LCD, key scan, ADC, codec lines | [hugelton/felucca-bsp](https://github.com/hugelton/felucca-bsp) `boards/fm1/` (MPL-2.0) |
| Official SDK headers, MIDI ports, stock V15 image | [m-vave/fm1_open](https://github.com/m-vave/fm1_open) `sdk/include/fm1.h`, README, `sdk/firmware/FM-1_015.fwsc` |
| Linux build, packaging, Felucca-family update protocol | [hugelton/Felucca](https://github.com/hugelton/Felucca) `BUILDING.md`, `web/fm1ota.js` |
| Measured clock, RAM / flash budget, CPU | [TwistedGenetics/DREADNOUGHT](https://github.com/TwistedGenetics/DREADNOUGHT) `docs/PLATFORM.md` (and its sources) |
| Chip-level recovery | [kurogedelic/FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter); X0X `tools/fm1_rescue.py` |
