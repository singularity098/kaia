#!/usr/bin/env bash
# Builds Kaia from source on Linux.
#   ./build.sh            build into ./build (Release)
#   ./build.sh --deps     first install the build dependencies with your package manager (uses sudo)
#   ./build.sh --install  build, then install the plugins for the current user
set -euo pipefail
cd "$(dirname "$0")"

want_deps=0
want_install=0
for arg in "$@"; do
    case "$arg" in
        --deps) want_deps=1 ;;
        --install) want_install=1 ;;
        -h|--help) sed -n '2,5p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg"; exit 1 ;;
    esac
done

if [ "$want_deps" = 1 ]; then
    if command -v apt-get >/dev/null; then
        sudo apt-get update
        sudo apt-get install -y build-essential cmake ninja-build git pkg-config \
            libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
            libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev
    elif command -v dnf >/dev/null; then
        sudo dnf install -y gcc-c++ cmake ninja-build git pkgconf-pkg-config \
            alsa-lib-devel pipewire-jack-audio-connection-kit-devel freetype-devel fontconfig-devel \
            libX11-devel libXcomposite-devel libXcursor-devel libXext-devel libXinerama-devel libXrandr-devel libXrender-devel
    elif command -v pacman >/dev/null; then
        sudo pacman -S --needed --noconfirm base-devel cmake ninja git pkgconf \
            alsa-lib jack2 freetype2 fontconfig libx11 libxcomposite libxcursor libxext libxinerama libxrandr libxrender
    elif command -v zypper >/dev/null; then
        sudo zypper install -y gcc-c++ cmake ninja git pkg-config alsa-devel libjack-devel freetype2-devel fontconfig-devel \
            libX11-devel libXcomposite-devel libXcursor-devel libXext-devel libXinerama-devel libXrandr-devel libXrender-devel
    else
        echo "Unknown package manager: install a C++20 compiler, cmake, ninja, and the ALSA, JACK, FreeType, fontconfig and X11 development packages."
    fi
fi

generator=()
command -v ninja >/dev/null && generator=(-G Ninja)

# JUCE and the CLAP extensions are downloaded automatically on the first configure
cmake -S . -B build "${generator[@]}" -DCMAKE_BUILD_TYPE=Release -DKAIA_BUILD_TESTS=OFF
cmake --build build --config Release --parallel

out=build/Kaia_artefacts/Release
echo
echo "Built:"
ls -d "$out"/VST3/Kaia.vst3 "$out"/LV2/Kaia.lv2 "$out"/CLAP/Kaia.clap "$out"/Standalone/Kaia 2>/dev/null || true

if [ "$want_install" = 1 ]; then
    # install.sh expects the same folder layout as the release archive
    bash packaging/install.sh --from "$out"
fi
