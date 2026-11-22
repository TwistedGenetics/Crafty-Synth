#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Crafty Synth's MPC plugin in Akai's official format (MPC OS 3.10.1+, External Plugin Support): one plugin folder
for every MPC, made with Akai's MPC Plugin SDK (external/mpc-plugin-builder-sdk) from the Gen1 and Gen2 builds.

    python3 ports/mpc/official.py <armv7 zip> <aarch64 zip> -o <zip to write>

The SDK's legacy_convert.py merges the two builds into one folder (plugin.json, both libraries, the skin, data),
then this declares the skin's UI version in version.xml (the SDK's build.py does the same for its own plugins: without
it the MPC may show generic sliders), zips it with the SDK's INSTALL.txt, and checks it with the SDK's validate.py
--strict. Copyright (C) 2026 Jamie (TwistedGenetics)."""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SDK = REPO / "external" / "mpc-plugin-builder-sdk" / "tools"
FOLDER = "Twisted Genetics - VST - Crafty Synth"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("armv7")
    ap.add_argument("aarch64")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    sys.path.insert(0, str(SDK))
    import legacy_convert
    from package import install_text
    with tempfile.TemporaryDirectory() as td:
        legacy_convert.convert([a.armv7, a.aarch64], td)
        dest = Path(td) / FOLDER
        man = json.loads((dest / "plugin.json").read_text())
        if "file32" not in man or "file64" not in man:
            sys.exit("official: the package lacks a 32-bit or a 64-bit build")
        vx = dest / "version.xml"
        x = vx.read_text()
        if "<uiversion>" not in x:
            vx.write_text(re.sub(r"(</version>\n)", r"\1    <uiversion>0.1.0.0</uiversion>\n", x, count=1))
        r = subprocess.run([sys.executable, str(SDK / "validate.py"), str(dest), "--strict"], capture_output=True, text=True)
        print("\n".join(ln for ln in r.stdout.splitlines() if not ln.strip().startswith("OK")) or "validate: all OK")
        if r.returncode:
            sys.exit("official: Akai's validate.py --strict failed:\n" + r.stdout[-3000:] + r.stderr[-2000:])
        out = Path(a.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
            for root, dirs, files in os.walk(dest):
                dirs.sort()
                for fn in sorted(files):
                    full = Path(root) / fn
                    info = zipfile.ZipInfo.from_file(full, f"{FOLDER}/{full.relative_to(dest).as_posix()}")
                    info.compress_type = zipfile.ZIP_DEFLATED
                    zf.writestr(info, full.read_bytes())
            zf.writestr("INSTALL.txt", install_text(man, FOLDER))
        print(f"official: {out} ({out.stat().st_size} bytes): {man['file32']}, {man['file64']}")


if __name__ == "__main__":
    main()
