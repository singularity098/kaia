/* Pre-creates the X11 atoms a window manager normally registers. JUCE apps (the host test and
   UI snapshot tools) abort with BadAtom on a bare Xvfb without them. Build: gcc -o xatoms xatoms.c -lX11
   Run once per Xvfb session, before the tool (start Xvfb with -noreset so the atoms persist). */
#include <X11/Xlib.h>
#include <stdio.h>
int main(void) {
    const char* names[] = { "WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW", "_NET_WM_PING", "WM_CHANGE_STATE", "WM_STATE",
        "_NET_ACTIVE_WINDOW", "_NET_WM_PID", "_NET_WM_WINDOW_TYPE", "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_SKIP_TASKBAR",
        "_NET_WM_STATE_ABOVE", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_COMBO",
        "_NET_WM_WINDOW_TYPE_DIALOG", "_NET_WM_ALLOWED_ACTIONS", "_MOTIF_WM_HINTS", "_NET_FRAME_EXTENTS", "_NET_WORKAREA",
        "_NET_WM_MOVERESIZE", "_XEMBED", "_XEMBED_INFO", "XdndAware", "XdndEnter", "XdndLeave", "XdndPosition", "XdndStatus",
        "XdndDrop", "XdndFinished", "XdndSelection", "XdndTypeList", "XdndActionList", "XdndActionDescription", "XdndActionCopy",
        "XdndActionPrivate", "UTF8_STRING", "_NET_WM_NAME", "_NET_WM_ICON", "_NET_SUPPORTED", "_NET_WM_USER_TIME", 0 };
    Display* d = XOpenDisplay(0);
    if (!d) { fprintf(stderr, "no display\n"); return 1; }
    for (int i = 0; names[i]; ++i) XInternAtom(d, names[i], False);
    XSync(d, False);
    XCloseDisplay(d);
    return 0;
}
