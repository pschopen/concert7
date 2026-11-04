#!/usr/bin/env bash
# Concert7 - builds the macOS installer (.pkg) with AU, VST3 and the standalone app as selectable parts.
#
#   packaging/macos/build_pkg.sh <version> <artefacts dir> <output dir>
#   e.g. packaging/macos/build_pkg.sh 0.1.0-beta build/Concert7_artefacts/Release dist
#
# The bundles are signed ad hoc (no Apple developer certificate); see README for Gatekeeper.
set -euo pipefail

VERSION="$1"
ARTEFACTS="$(cd "$2" && pwd)"
OUT="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
ID="com.github.pschopen.concert7"
PKG_VERSION="${VERSION%%-*}"   # pkgbuild wants a plain number

stage () {   # stage <name> <bundle> <install dir>
    local root="$WORK/root-$1"
    mkdir -p "$root$3"
    cp -R "$ARTEFACTS/$2" "$root$3/"
    codesign --force --deep --sign - "$root$3/$(basename "$2")"
    # keep the bundles where they belong even if an older copy exists elsewhere
    pkgbuild --analyze --root "$root" "$WORK/$1.plist" >/dev/null
    /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$WORK/$1.plist" 2>/dev/null || true
    pkgbuild --root "$root" --component-plist "$WORK/$1.plist" --identifier "$ID.$1" \
             --version "$PKG_VERSION" --install-location / "$WORK/pkgs/Concert7-$1.pkg"
}

mkdir -p "$WORK/pkgs" "$WORK/resources"
stage au AU/Concert7.component /Library/Audio/Plug-Ins/Components
stage vst3 VST3/Concert7.vst3 /Library/Audio/Plug-Ins/VST3
stage app Standalone/Concert7.app /Applications

sed "s/@VERSION@/$VERSION/g" "$HERE/welcome.txt" > "$WORK/resources/welcome.txt"
cp "$ROOT/LICENSE" "$WORK/resources/license.txt"
sed "s/@VERSION@/$VERSION/g; s/@PKG_VERSION@/$PKG_VERSION/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"

productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK/pkgs" --resources "$WORK/resources" \
             "$OUT/Concert7-$VERSION-macOS.pkg"
echo "built $OUT/Concert7-$VERSION-macOS.pkg"
