#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Crafty Synth: the firmware's engine as WebAssembly for the editor (web/editor/crafty.wasm). Needs clang with the
# wasm32 target and wasm-ld (lld). Copyright (C) 2026 Jamie (TwistedGenetics).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/gen
python3 tools/gen_wavetables.py build/gen/cr_wavetables.h
python3 tools/gen_presets.py build/gen/cr_presets.h
OUT="${1:-web/editor/crafty.wasm}"
clang --target=wasm32 -O2 -std=c99 -ffreestanding -nostdlib -fvisibility=hidden -Wall -Wextra -Werror \
  -Wdouble-promotion -Wno-unused-function -Isrc/core -Isrc/dsp -Ibuild/gen \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--initial-memory=1048576 -Wl,--strip-all \
  src/wasm/cr_wasm.c src/dsp/cr_synth.c src/dsp/cr_wt.c src/core/cr_patch.c src/core/cr_arp.c -o "$OUT"
echo "build_wasm: $OUT $(wc -c < "$OUT") B"
