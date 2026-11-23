# Crafty Synth: working rules

Crafty Synth ships three ways from this one repo, all running the same engine:

| Port | Where | Builds in CI job |
|---|---|---|
| **M-VAVE FM-1 firmware** (the original) | `src/app`, `src/hardware`, `x0x-firmware` | `firmware` |
| **AU / VST3 / standalone (macOS)** | `ports/plugin` (JUCE; its window is `web/editor`) | `plugin-mac` |
| **MPC plugin** (Gen1 MPC / Force: armv7; Gen2: aarch64) | `ports/mpc` (the `external/mpc-vst-plugins` toolkit) | `mpc` |

Shared by all three: `src/core` (patch, arp, value text, user area), `src/dsp` (the synth), `src/host` (the engine as
an instance, and the editor protocol for the plugins), `web/editor` (the web editor, and the plugin's window).

## Every change updates every port, the FM-1 first

1. **Make the change for the FM-1 first** (engine, app, screen, editor), and get its host tests green
   (`sh tests/run_host.sh`). The FM-1 is the reference; the plugins follow it, never the other way round.
2. **Then bring every port along in the same change**:
   - A new or changed parameter: `src/core/cr_patch.[ch]` (append only: ids never move), its value text in
     `src/core/cr_text.c`, its page in `TX_PAGE` there, the FM-1's page in `src/app/cr_app.c`, the editor
     (`tools/gen_presets.py --meta web/editor/meta.js`, its panel in `web/editor/app.js`), the MPC
     (`python3 ports/mpc/make_port.py`, and a page for it in `ports/mpc/gen_params.c`'s `SECTIONS`; an oscillator
     value also goes on the hand-made OSC page, `OSC` in `make_port.py`). The AU / VST3
     picks it up by itself (its parameters come from `cr_patch.h` and `cr_keys.h`).
   - Every parameter is also a mod destination: append a `CR_D_` (cr_patch.h), its name (`CR_DEST_NAME`), its
     parameter in `DEST_P` (cr_synth.c), and read it in the engine through the matrix (`e[]`, or `er()` for a stepped
     value), never `raw[]` / `base[]`.
   - Rebuild the editor's engine (`sh tools/build_wasm.sh`) whenever `src/core`, `src/dsp` or `src/host` changed.
3. **Bump `VERSION`** for anything that should be released. A push to `main` builds the FM-1 first; the plugin and
   MPC jobs only run once it has passed (`needs: firmware`), and one release `vX.Y` carries all three, the FM-1's
   package listed first: `Crafty-Synth-X.Y.fwsc`, `Crafty-Synth-X.Y-mac.zip`, `Crafty-Synth-X.Y-mpc.zip` (Akai's official format, MPC OS
   3.10.1+, every MPC), `Crafty-Synth-X.Y-mpc-armv7.zip` and `-mpc-aarch64.zip` (the community installer).
4. Update `CHANGELOG.md` (one entry for all ports), and the docs each change touches (`docs/MANUAL.md` for the
   FM-1, `docs/PORTS.md` for the plugins, `docs/PROTOCOL.md`).

## Rules that keep the ports in step
- The engine is C99 with no allocation and no libc in `src/core`,
  `src/dsp` and `src/host` (the WebAssembly build has none), and no doubles in `src/core` and `src/dsp` (the FM-1's
  audio path; `-Wdouble-promotion -Werror`).
- Parameter ids, CC numbers, MPC keys (`ports/mpc/params.lock`) and the plugin's parameter ids (`cr_keys.h`) are
  append only: hosts and projects store them.
- The user area (`src/core/cr_user.h`) is the same bytes in the FM-1's flash, the plugin's file and the MPC's file.
- Commits are made as `Jamie <noreply@anthropic.com>`.
