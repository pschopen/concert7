#!/usr/bin/env bash
# Concert7 - builds the Linux release archive (VST3, LV2, standalone + install.sh).
#
#   packaging/linux/package.sh <version> <artefacts dir> <output dir>
set -euo pipefail

VERSION="$1"
ARTEFACTS="$(cd "$2" && pwd)"
OUT="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
NAME="Concert7-$VERSION-Linux-x86_64"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/$NAME" "$OUT"
cp -R "$ARTEFACTS/VST3/Concert7.vst3" "$WORK/$NAME/"
cp -R "$ARTEFACTS/LV2/Concert7.lv2" "$WORK/$NAME/"
cp "$ARTEFACTS/Standalone/Concert7" "$WORK/$NAME/"
cp "$HERE/install.sh" "$ROOT/README.md" "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$ROOT/CHANGELOG.md" "$WORK/$NAME/"
chmod +x "$WORK/$NAME/install.sh" "$WORK/$NAME/Concert7"

tar -C "$WORK" -czf "$OUT/$NAME.tar.gz" "$NAME"
echo "built $OUT/$NAME.tar.gz"
