#!/usr/bin/env bash
# Recreates a header-only "sysroot" with the development headers Kaia needs, for build machines
# where the distro package manager is unavailable (e.g. sandboxed CI/agent containers that can
# reach GitHub but not the Ubuntu archive). Runtime libraries (libasound.so.2, libX*.so) must
# already be installed; only headers and pkg-config files are provided.
#
#   tools/sandbox-sysroot.sh [dir]          (default: ../deps/sysroot next to the repo)
#   then: export PKG_CONFIG_PATH=$dir/lib/pkgconfig CPATH=$dir/include LIBRARY_PATH=$dir/lib
#
# On a normal machine just install the -dev packages instead (see build.sh --deps).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
P="${1:-$root/../deps/sysroot}"
work="$(dirname "$P")/sysroot-src"
mkdir -p "$P/include/alsa/sound" "$P/include/jack" "$P/include/X11/extensions" "$P/include/X11/Xcursor" "$P/lib/pkgconfig" "$work"
P="$(cd "$P" && pwd)"

# Match the installed runtime versions where possible
alsa_ver="$(dpkg-query -W -f='${Version}' libasound2t64 2>/dev/null || dpkg-query -W -f='${Version}' libasound2 2>/dev/null || echo 1.2.11)"
alsa_ver="${alsa_ver%%-*}"
[ -d "$work/alsa-lib" ] || git clone -q --depth 1 --branch "v$alsa_ver" https://github.com/alsa-project/alsa-lib.git "$work/alsa-lib" \
    || git clone -q --depth 1 --branch v1.2.11 https://github.com/alsa-project/alsa-lib.git "$work/alsa-lib"
[ -d "$work/jack2" ] || git clone -q --depth 1 --branch v1.9.21 https://github.com/jackaudio/jack2.git "$work/jack2"

# --- ALSA: copy headers and generate asoundlib.h / version.h like alsa-lib's configure does
inc="$work/alsa-lib/include"
for f in asoundef.h global.h input.h output.h error.h conf.h control.h pcm.h pcm_old.h timer.h rawmidi.h ump.h ump_msg.h \
         hwdep.h mixer.h seq_event.h seq.h seqmid.h seq_midi_event.h; do
    [ -f "$inc/$f" ] && cp "$inc/$f" "$P/include/alsa/"
done
cp -r "$inc/sound/." "$P/include/alsa/sound/"
{
    cat "$inc/asoundlib-head.h"
    echo "#include <endian.h>"
    printf '\n#ifndef DOC_HIDDEN\n#ifndef __GNUC__\n#define __inline__ inline\n#endif\n#endif /* DOC_HIDDEN */\n\n'
    for h in asoundef version global input output error conf pcm rawmidi ump timer hwdep control mixer ump_msg seq_event seq seqmid seq_midi_event; do
        [ -f "$P/include/alsa/$h.h" ] || [ "$h" = version ] && echo "#include <alsa/$h.h>"
    done
    cat "$inc/asoundlib-tail.h"
} > "$P/include/alsa/asoundlib.h"
IFS=. read -r maj min sub <<< "$alsa_ver"
cat > "$P/include/alsa/version.h" <<EOF
#define SND_LIB_MAJOR		$maj
#define SND_LIB_MINOR		$min
#define SND_LIB_SUBMINOR	${sub:-0}
#define SND_LIB_EXTRAVER	1000000
#define SND_LIB_VER(maj, min, sub) (((maj)<<16)|((min)<<8)|(sub))
#define SND_LIB_VERSION SND_LIB_VER(SND_LIB_MAJOR, SND_LIB_MINOR, SND_LIB_SUBMINOR)
#define SND_LIB_VERSION_STR	"$alsa_ver"
EOF
ln -sf "$(ls /usr/lib/x86_64-linux-gnu/libasound.so.2 /usr/lib64/libasound.so.2 2>/dev/null | head -1)" "$P/lib/libasound.so"

# --- JACK: headers only (JUCE loads libjack at runtime)
cp "$work/jack2/common/jack/"*.h "$P/include/jack/"

# --- X11 extensions: the declarations JUCE uses (it loads these libraries with dlopen)
cat > "$P/include/X11/extensions/Xrandr.h" <<'EOF'
#ifndef _XRANDR_H_
#define _XRANDR_H_
#include <X11/Xlib.h>
#include <X11/extensions/randr.h>
#include <X11/extensions/Xrender.h>
#include <X11/Xfuncproto.h>
typedef XID RROutput; typedef XID RRCrtc; typedef XID RRMode; typedef XID RRProvider;
typedef unsigned long XRRModeFlags;
typedef struct _XRRModeInfo { RRMode id; unsigned int width, height; unsigned long dotClock;
    unsigned int hSyncStart, hSyncEnd, hTotal, hSkew, vSyncStart, vSyncEnd, vTotal;
    char* name; unsigned int nameLength; XRRModeFlags modeFlags; } XRRModeInfo;
typedef struct _XRRScreenResources { Time timestamp, configTimestamp; int ncrtc; RRCrtc* crtcs;
    int noutput; RROutput* outputs; int nmode; XRRModeInfo* modes; } XRRScreenResources;
typedef struct _XRROutputInfo { Time timestamp; RRCrtc crtc; char* name; int nameLen;
    unsigned long mm_width, mm_height; Connection connection; SubpixelOrder subpixel_order;
    int ncrtc; RRCrtc* crtcs; int nclone; RROutput* clones; int nmode, npreferred; RRMode* modes; } XRROutputInfo;
typedef struct _XRRCrtcInfo { Time timestamp; int x, y; unsigned int width, height; RRMode mode;
    Rotation rotation; int noutput; RROutput* outputs; Rotation rotations; int npossible; RROutput* possible; } XRRCrtcInfo;
_XFUNCPROTOBEGIN
XRRScreenResources* XRRGetScreenResources (Display*, Window);
void XRRFreeScreenResources (XRRScreenResources*);
XRROutputInfo* XRRGetOutputInfo (Display*, XRRScreenResources*, RROutput);
void XRRFreeOutputInfo (XRROutputInfo*);
XRRCrtcInfo* XRRGetCrtcInfo (Display*, XRRScreenResources*, RRCrtc);
void XRRFreeCrtcInfo (XRRCrtcInfo*);
RROutput XRRGetOutputPrimary (Display*, Window);
_XFUNCPROTOEND
#endif
EOF
cat > "$P/include/X11/extensions/Xinerama.h" <<'EOF'
#ifndef _Xinerama_h
#define _Xinerama_h
#include <X11/Xlib.h>
#include <X11/Xfuncproto.h>
typedef struct { int screen_number; short x_org, y_org, width, height; } XineramaScreenInfo;
_XFUNCPROTOBEGIN
Bool XineramaQueryExtension (Display*, int*, int*);
Status XineramaQueryVersion (Display*, int*, int*);
Bool XineramaIsActive (Display*);
XineramaScreenInfo* XineramaQueryScreens (Display*, int*);
_XFUNCPROTOEND
#endif
EOF
cat > "$P/include/X11/extensions/Xcomposite.h" <<'EOF'
#ifndef _XCOMPOSITE_H_
#define _XCOMPOSITE_H_
#include <X11/Xlib.h>
#include <X11/extensions/composite.h>
#include <X11/Xfuncproto.h>
_XFUNCPROTOBEGIN
Bool XCompositeQueryExtension (Display*, int*, int*);
Status XCompositeQueryVersion (Display*, int*, int*);
_XFUNCPROTOEND
#endif
EOF
cat > "$P/include/X11/Xcursor/Xcursor.h" <<'EOF'
#ifndef _XCURSOR_H_
#define _XCURSOR_H_
#include <X11/Xlib.h>
#include <X11/Xfuncproto.h>
typedef int XcursorBool; typedef unsigned int XcursorUInt; typedef XcursorUInt XcursorDim; typedef XcursorUInt XcursorPixel;
typedef struct _XcursorImage { XcursorUInt version; XcursorDim size, width, height, xhot, yhot; XcursorUInt delay; XcursorPixel* pixels; } XcursorImage;
_XFUNCPROTOBEGIN
XcursorImage* XcursorImageCreate (int, int);
void XcursorImageDestroy (XcursorImage*);
Cursor XcursorImageLoadCursor (Display*, const XcursorImage*);
_XFUNCPROTOEND
#endif
EOF

# --- pkg-config files
cat > "$P/lib/pkgconfig/alsa.pc" <<EOF
prefix=$P
Name: alsa
Description: ALSA headers + system runtime
Version: $alsa_ver
Libs: -L\${prefix}/lib -lasound
Cflags: -I\${prefix}/include
EOF
for p in jack xrandr xinerama xcursor xcomposite; do
    printf 'prefix=%s\nName: %s\nDescription: headers\nVersion: 1.0\nLibs:\nCflags: -I${prefix}/include\n' "$P" "$p" > "$P/lib/pkgconfig/$p.pc"
done

echo "Sysroot ready: $P"
echo "export PKG_CONFIG_PATH=$P/lib/pkgconfig CPATH=$P/include LIBRARY_PATH=$P/lib"
