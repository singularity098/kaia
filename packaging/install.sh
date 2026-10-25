#!/usr/bin/env bash
# Installs the Kaia plugins for the current user (or system-wide with --system).
#   ./install.sh            -> ~/.vst3, ~/.lv2, ~/.clap, ~/.local/bin
#   sudo ./install.sh --system -> /usr/local/lib/{vst3,lv2}, /usr/lib/clap, /usr/local/bin
#   ./install.sh --uninstall [--system]
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
system=0
uninstall=0
while [ $# -gt 0 ]; do
    case "$1" in
        --system) system=1 ;;
        --uninstall) uninstall=1 ;;
        --from) shift; here="$(cd "$1" && pwd)" ;;
        -h|--help) sed -n '2,6p' "$0"; exit 0 ;;
        *) echo "unknown option: $1"; exit 1 ;;
    esac
    shift
done

if [ "$system" = 1 ]; then
    vst3="/usr/local/lib/vst3"; lv2="/usr/local/lib/lv2"; clap="/usr/lib/clap"   # CLAP does not search /usr/local
    bin="/usr/local/bin"; apps="/usr/local/share/applications"; icons="/usr/local/share/icons/hicolor"
else
    vst3="$HOME/.vst3"; lv2="$HOME/.lv2"; clap="$HOME/.clap"
    bin="$HOME/.local/bin"; apps="$HOME/.local/share/applications"; icons="$HOME/.local/share/icons/hicolor"
fi

if [ "$uninstall" = 1 ]; then
    rm -rf "$vst3/Kaia.vst3" "$lv2/Kaia.lv2" "$clap/Kaia.clap" "$bin/Kaia" "$apps/kaia.desktop"
    rm -f "$icons"/*/apps/kaia.png
    echo "Kaia removed."
    exit 0
fi

mkdir -p "$vst3" "$lv2" "$clap" "$bin" "$apps"

rm -rf "$vst3/Kaia.vst3" "$lv2/Kaia.lv2"
cp -r "$here/VST3/Kaia.vst3" "$vst3/"
cp -r "$here/LV2/Kaia.lv2" "$lv2/"
cp "$here/CLAP/Kaia.clap" "$clap/"
cp "$here/Standalone/Kaia" "$bin/Kaia"
chmod +x "$bin/Kaia"

# App icon: next to this script in the release archive, in Resources/Logo/ in the source tree
logo="$here/icons"
[ -d "$logo" ] || logo="$(cd "$(dirname "$0")" && pwd)/../Resources/Logo"
for size in 32 64 128 256 512; do
    icon="$logo/kaia-app-icon-$size.png"
    [ -f "$icon" ] || continue
    mkdir -p "$icons/${size}x${size}/apps"
    cp "$icon" "$icons/${size}x${size}/apps/kaia.png"
done

cat > "$apps/kaia.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Kaia
Comment=Drum sampler for MPC programs and expansions
Exec=$bin/Kaia
Icon=kaia
Terminal=false
Categories=AudioVideo;Audio;Music;
EOF

echo "Installed Kaia:"
echo "  VST3       $vst3/Kaia.vst3"
echo "  LV2        $lv2/Kaia.lv2"
echo "  CLAP       $clap/Kaia.clap"
echo "  Standalone $bin/Kaia"
echo
echo "Rescan plugins in your DAW. The demo expansion \"Kaia Demo Kit.xpn\" is a quick way to check it works."

# Sanity check: the plugin needs glibc 2.35 or newer
if command -v ldd >/dev/null 2>&1; then
    glibc="$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+$' || true)"
    if [ -n "$glibc" ]; then
        major="${glibc%%.*}"; minor="${glibc##*.}"
        if [ "$major" -lt 2 ] || { [ "$major" -eq 2 ] && [ "$minor" -lt 35 ]; }; then
            echo
            echo "WARNING: this system has glibc $glibc; these binaries need 2.35+. Build from source instead (see README)."
        fi
    fi
fi
