#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Crafty Synth's host tests: the engine measured (every modifier, bank and preset), the app driven from a simulated
# FM-1 panel (tests/host/scenarios), the editor's tables and its WebAssembly engine checked against the C. Pictures of
# the FM-1's screen and the sound land in build/host. Needs cc, python3 (Pillow, fonttools, numpy) and node; clang
# with wasm-ld rebuilds the editor's engine (else the committed one is tested).
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
mkdir -p build/gen/x0x build/host
python3 tools/gen_wavetables.py build/gen/cr_wavetables.h
python3 tools/gen_presets.py build/gen/cr_presets.h
python3 tools/gen_presets.py --meta build/host/meta.js
cmp -s build/host/meta.js web/editor/meta.js || { echo "FAIL web/editor/meta.js is stale: run tools/gen_presets.py --meta web/editor/meta.js"; exit 1; }
python3 tools/gen_cr_font.py build/gen/cr_font.h >/dev/null
[ -f build/gen/x0x/felucca_font.h ] || python3 x0x-firmware/tools/gen_font.py build/gen/x0x/felucca_font.h >/dev/null
# the firmware's own rules: C99, no doubles anywhere (-Wdouble-promotion), every warning an error
W="-O2 -std=c99 -Wall -Wextra -Werror -Wdouble-promotion -Wno-unused-function -Isrc/core -Isrc/dsp -Isrc/app -Ibuild/gen -Ibuild/gen/x0x"
CORE="src/core/cr_patch.c src/core/cr_arp.c src/dsp/cr_synth.c src/dsp/cr_wt.c src/dsp/cr_filter.c src/dsp/cr_fx.c"
# shellcheck disable=SC2086
$CC $W tests/host/synth_test.c $CORE -o build/host/synth_test
build/host/synth_test build/host
# shellcheck disable=SC2086
$CC $W -Wno-unused-parameter -Wno-sign-compare -Ix0x-firmware/firmware/src tests/host/cr_host.c $CORE -o build/host/cr_host
for s in tests/host/scenarios/*.txt; do
    build/host/cr_host "$s" build/host
done
python3 - <<'PY'
from pathlib import Path
from PIL import Image
for p in Path("build/host").glob("*.ppm"):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
PY
if command -v clang >/dev/null && command -v wasm-ld >/dev/null; then
    sh tools/build_wasm.sh build/host/crafty.wasm >/dev/null
    cmp -s build/host/crafty.wasm web/editor/crafty.wasm || echo "note web/editor/crafty.wasm differs from this build (another clang?): the site is built with CI's"
fi
node --test web/editor/test/*.test.mjs web/flasher/test/*.test.mjs >build/host/node-tests.txt 2>&1 || { cat build/host/node-tests.txt; exit 1; }
echo "ok   editor and flasher tests ($(grep -c '^ok' build/host/node-tests.txt) passed)"
echo "host tests: all ok"
