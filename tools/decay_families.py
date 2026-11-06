#!/usr/bin/env python3
# Concert7 - open source true stereo convolution reverb
# Copyright (C) 2026 pschopen, GPL-3.0-or-later
"""
Checks DECAY against the Bricasti library: its Large / Medium / Small families are the closest thing to
"the same room with another decay". Every member is set to the decay of every other member of its family
(concert7-dev ir --set decay=...), the result is analysed like a preset (third octave reverb times) and
compared with the measured family member. Proportional scaling of the source is shown as a baseline.

    python3 tools/decay_families.py --irs IR_FOLDER [--dev path/to/Concert7Dev] [--verbose]

Plain Python, no packages needed. Takes a few minutes (one render per pair).
"""
import argparse
import math
import os
import re
import statistics
import subprocess
import sys
import tempfile

FAMILIES = {
    "Hall":              ["Halls/01 Large Hall", "Halls/02 Medium Hall", "Halls/03 Small Hall"],
    "Hall & Near":       ["Halls/04 Large & Near", "Halls/05 Medium & Near", "Halls/06 Small & Near"],
    "Hall & Deep":       ["Halls/08 Large & Deep", "Halls/09 Medium & Deep"],
    "Chamber":           ["Chambers/01 Large Chamber", "Chambers/02 Medium Chamber", "Chambers/03 Small Chamber"],
    "Chamber & Dark":    ["Chambers/04 Large & Dark", "Chambers/05 Small & Dark"],
    "Chamber & Bright":  ["Chambers/06 Large & Bright", "Chambers/07 Small & Bright"],
    "Ambience":          ["Ambiences/01 Large Ambience", "Ambiences/02 Medium Ambience", "Ambiences/03 Small Ambience"],
    "Ambience & Dark":   ["Ambiences/04 Large & Dark", "Ambiences/05 Medium & Dark", "Ambiences/06 Small & Dark"],
    "Ambience & Bright": ["Ambiences/07 Large & Bright", "Ambiences/08 Medium & Bright", "Ambiences/09 Small & Bright"],
    "Tiled Room":        ["Rooms/12 Large Tiled Room", "Rooms/13 Medium Tiled Room", "Rooms/14 Small Tiled Room"],
    "Wooden Room":       ["Rooms/10 Large Wooden Room", "Rooms/11 Small Wooden Room"],
    "Room":              ["Rooms/26 Large Room", "Rooms/27 Small Room"],
    "& Room":            ["Rooms/21 Large & Room", "Rooms/22 Small & Room"],
    "Plate":             ["Plates/16 Large Plate", "Plates/17 Small Plate"],
}

RANGES = {"lows 125-400 Hz": (110.0, 450.0), "mids 500 Hz-2 kHz": (450.0, 2200.0), "highs 2.5-10 kHz": (2200.0, 11000.0)}


def parse_analysis(text):
    native = float(re.search(r"native RT ([0-9.]+) s", text).group(1))
    bands = {}
    for line in text.splitlines():
        m = re.match(r"\s+(\d+)\s+([0-9.]+)\s+", line)
        if m and "band" not in line:
            bands[float(m.group(1))] = float(m.group(2))
    return native, bands


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--irs", required=True, help="folder with the Bricasti library")
    ap.add_argument("--dev", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build",
                                                  "Concert7Dev_artefacts", "Release", "Concert7Dev"))
    ap.add_argument("--verbose", action="store_true", help="print every pair")
    args = ap.parse_args()

    presets = {p for members in FAMILIES.values() for p in members}
    measured = {p: parse_analysis(run([args.dev, "analyze", "--irs", args.irs, "--preset", p])) for p in sorted(presets)}

    errors = {}   # (family, range, model, direction) -> list of log2(result / reference)
    with tempfile.TemporaryDirectory() as tmp:
        for family, members in FAMILIES.items():
            for a in members:
                for b in members:
                    if a == b:
                        continue
                    src, ref = measured[a], measured[b]
                    wav = os.path.join(tmp, "ir.wav")
                    run([args.dev, "ir", "--irs", args.irs, "--preset", a, "--set", f"decay={ref[0]:.3f}", "--out", wav])
                    got = parse_analysis(run([args.dev, "analyze", "--irs", args.irs, "--file", wav]))
                    direction = "longer" if ref[0] > src[0] else "shorter"
                    if args.verbose:
                        print(f"{a} -> {b}: native {src[0]:.2f} -> {ref[0]:.2f} s, result {got[0]:.2f} s")
                    for name, (lo, hi) in RANGES.items():
                        for f, r in ref[1].items():
                            if not lo <= f <= hi or f not in src[1] or f not in got[1]:
                                continue
                            proportional = src[1][f] * ref[0] / src[0]
                            for model, value in (("Concert7", got[1][f]), ("proportional", proportional)):
                                errors.setdefault((family, name, model, direction), []).append(math.log2(value / r))

    pct = lambda x: (2.0 ** x - 1.0) * 100.0

    def summary(keys):
        vals = [v for k in keys for v in errors.get(k, [])]
        return statistics.median(abs(v) for v in vals) if vals else float("nan"), vals

    print("\nMedian |error| of the third octave reverb times against the measured family member")
    header = f"{'family':18s}" + "".join(f"{name:>26s}" for name in RANGES)
    print(header + "\n" + f"{'':18s}" + "".join(f"{'Concert7 / proportional':>26s}" for _ in RANGES))
    for family in list(FAMILIES) + ["ALL"]:
        fams = list(FAMILIES) if family == "ALL" else [family]
        row = f"{family:18s}"
        for name in RANGES:
            cells = []
            for model in ("Concert7", "proportional"):
                med, _ = summary([(f, name, model, d) for f in fams for d in ("longer", "shorter")])
                cells.append(f"{pct(med):5.1f} %")
            row += f"{' / '.join(cells):>26s}"
        print(row)

    print("\nMean signed error, all families (+ = result too long)")
    for direction in ("longer", "shorter"):
        row = f"  decay {direction:8s}"
        for name in RANGES:
            _, vals = summary([(f, name, "Concert7", direction) for f in FAMILIES])
            row += f"   {name}: {pct(statistics.mean(vals)):+5.1f} %"
        print(row)
    return 0


if __name__ == "__main__":
    sys.exit(main())
