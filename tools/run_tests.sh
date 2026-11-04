#!/usr/bin/env bash
# Concert7 test runner: builds everything, runs the unit tests (with the Bricasti library if given),
# renders GUI snapshots and validates the Audio Unit.
#
#   tools/run_tests.sh [IR_FOLDER]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
OUT="$BUILD/test-output"
IRS="${1:-${CONCERT7_IR_DIR:-}}"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

DEV="$BUILD/Concert7Dev_artefacts/Release/Concert7Dev"
mkdir -p "$OUT"
IRARGS=()
if [ -n "$IRS" ]; then IRARGS=(--irs "$IRS"); else echo "note: no IR folder given, library/processor tests are skipped"; fi

echo "== unit tests"
STATUS=0
"$DEV" test "${IRARGS[@]}" > "$OUT/tests.log" 2>&1 || STATUS=$?
grep -E "^(  |!!!|====)" "$OUT/tests.log" || true
if [ $STATUS -ne 0 ]; then echo "FAIL (log: $OUT/tests.log)"; exit 1; fi
echo "PASS (log: $OUT/tests.log)"

echo "== GUI snapshots"
for tab in 0 1 2 3; do "$DEV" snapshot "${IRARGS[@]}" --tab $tab --out "$OUT/ui-tab$tab.png" >/dev/null; done
"$DEV" snapshot "${IRARGS[@]}" --welcome --out "$OUT/ui-welcome.png" >/dev/null
ls "$OUT"/*.png

if [[ "$(uname)" == "Darwin" ]]; then
    echo "== auval"
    AU="$BUILD/Concert7_artefacts/Release/AU/Concert7.component"
    mkdir -p "$HOME/Library/Audio/Plug-Ins/Components"
    rm -rf "$HOME/Library/Audio/Plug-Ins/Components/Concert7.component"
    cp -R "$AU" "$HOME/Library/Audio/Plug-Ins/Components/"
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    auval -v aufx Cc7r Cnc7 > "$OUT/auval.log" 2>&1 && echo "AU VALIDATION PASSED" || { tail -30 "$OUT/auval.log"; exit 1; }

    echo "== host tests (installed plugin binaries, as a DAW loads them)"
    mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3"
    rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/Concert7.vst3"
    cp -R "$BUILD/Concert7_artefacts/Release/VST3/Concert7.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/"
    "$DEV" host --plugin "$HOME/Library/Audio/Plug-Ins/VST3/Concert7.vst3" --sr 48000 --block 512 | tail -1
    "$DEV" host --plugin "AudioUnit:Effects/aufx,Cc7r,Cnc7" --sr 44100 --block 256 | tail -1
fi
