#!/usr/bin/env bash
# Concert7 - Linux installer
#
#   ./install.sh              install for the current user (~/.vst3, ~/.lv2, ~/.local/bin)
#   sudo ./install.sh --system install for all users (/usr/local/lib/vst3, /usr/local/lib/lv2, /usr/local/bin)
#   ./install.sh --uninstall [--system]
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SYSTEM=0
UNINSTALL=0
for arg in "$@"; do
    case "$arg" in
        --system) SYSTEM=1 ;;
        --uninstall) UNINSTALL=1 ;;
        -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg"; exit 1 ;;
    esac
done

if [ $SYSTEM -eq 1 ]; then
    VST3="/usr/local/lib/vst3"; LV2="/usr/local/lib/lv2"; BIN="/usr/local/bin"
else
    VST3="$HOME/.vst3"; LV2="$HOME/.lv2"; BIN="$HOME/.local/bin"
fi

if [ $UNINSTALL -eq 1 ]; then
    rm -rf "$VST3/Concert7.vst3" "$LV2/Concert7.lv2" "$BIN/Concert7"
    echo "Concert7 removed."
    exit 0
fi

mkdir -p "$VST3" "$LV2" "$BIN"
rm -rf "$VST3/Concert7.vst3" "$LV2/Concert7.lv2"
cp -R "$HERE/Concert7.vst3" "$VST3/"
cp -R "$HERE/Concert7.lv2" "$LV2/"
install -m 755 "$HERE/Concert7" "$BIN/Concert7"

echo "Concert7 installed:"
echo "  VST3        $VST3/Concert7.vst3"
echo "  LV2         $LV2/Concert7.lv2"
echo "  Standalone  $BIN/Concert7"
echo
echo "Impulse responses: open Concert7, its setup screen guides you through installing a library"
echo "(a .7z archive needs bsdtar, or unpack it yourself)."
