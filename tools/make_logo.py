#!/usr/bin/env python3
"""Makes the Kaia logo SVGs in Resources/Logo/.

The logo is a K built from MPC pads (the stem in the text colour, the arms lit in the accent blue)
followed by "KAIA" in Michroma (SIL Open Font License), converted to outlines so the SVGs need no
font. Resources/Logo/alt/ keeps the runner-up: a 4x4 pad grid with pad 1 lit, glowing outwards.

Needs fontTools (pip install fonttools). Usage:
    python3 tools/make_logo.py <path to Michroma-Regular.ttf>
(github.com/google/fonts/raw/main/ofl/michroma/Michroma-Regular.ttf). The PNGs next to the SVGs
were rendered from them with a browser (Chromium); re-render them when the SVGs change.

The plugin header draws the pad K itself (PluginEditor.cpp, so the arms follow the user's accent
colour) and embeds kaia-wordmark.svg.
"""
import math
import pathlib
import sys

from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

OUT = pathlib.Path(__file__).resolve().parent.parent / "Resources" / "Logo"

ACCENT = "#3d8bff"
DARK = {"stem": "#eef1f5", "off": "#1a1e24", "text": "#eef1f5", "edge": "#262b33"}    # on dark backgrounds
LIGHT = {"stem": "#111317", "off": "#e2e5ea", "text": "#111317", "edge": "#d3d7de"}   # on light backgrounds

# The pad K: 4 columns x 5 rows, X = pad on. Column 0 is the stem, the rest are the lit arms.
PAD_K = ["X..X",
         "X.X.",
         "XX..",
         "X.X.",
         "X..X"]
CELL, PITCH, RADIUS = 24, 28, 5
ICON_W, ICON_H = 3 * PITCH + CELL, 4 * PITCH + CELL

CAP_HEIGHT = 50          # wordmark cap height, in the same units as the icon (136 tall)
TRACKING = 0.23          # extra letter spacing, in em (as in the first concept sheet)
GAP = 40                 # between the icon and the wordmark


def wordmark (font_path, cap_height):
    """Returns (svg path data, width) for "KAIA", baseline at y = cap height."""
    font = TTFont (font_path)
    glyphs = font.getGlyphSet()
    cmap = font.getBestCmap()
    upm = font["head"].unitsPerEm
    cap = font["OS/2"].sCapHeight or upm * 0.7
    scale = cap_height / cap
    x, parts, last_right = 0.0, [], 0.0
    for i, ch in enumerate ("KAIA"):
        name = cmap[ord (ch)]
        pen = SVGPathPen (glyphs)
        glyphs[name].draw (TransformPen (pen, (scale, 0, 0, -scale, x, cap_height)))
        parts.append (pen.getCommands())
        advance = font["hmtx"][name][0] * scale
        last_right = x + advance
        x += advance + (TRACKING * upm * scale if i < 3 else 0)
    # trim the right side bearing of the last letter
    bp = BoundsPen (glyphs)
    glyphs[cmap[ord ("A")]].draw (bp)
    width = last_right - (font["hmtx"][cmap[ord ("A")]][0] - bp.bounds[2]) * scale
    return " ".join (parts), width


def pad_k (colours, x0=0.0, y0=0.0):
    rects = []
    for r, row in enumerate (PAD_K):
        for c, ch in enumerate (row):
            fill = colours["off"] if ch != "X" else (colours["stem"] if c == 0 else ACCENT)
            rects.append ('<rect x="%g" y="%g" width="%d" height="%d" rx="%d" fill="%s"/>'
                          % (x0 + c * PITCH, y0 + r * PITCH, CELL, CELL, RADIUS, fill))
    return "\n  ".join (rects)


def pad_one (colours, x0=0.0, y0=0.0):
    """The runner-up icon: 4x4 pads, pad 1 (bottom left) lit, the others fading with distance."""
    out = ['<defs><filter id="glow" x="-1" y="-1" width="3" height="3">'
           '<feGaussianBlur stdDeviation="6"/></filter></defs>']
    size, step = 30, 34
    for r in range (4):
        for c in range (4):
            x, y = x0 + c * step, y0 + r * step
            d = math.hypot (c, 3 - r)
            if d == 0:
                out.append ('<rect x="%g" y="%g" width="%d" height="%d" rx="6" fill="%s" filter="url(#glow)" opacity=".9"/>'
                            % (x, y, size, size, ACCENT))
                out.append ('<rect x="%g" y="%g" width="%d" height="%d" rx="6" fill="#5a9dff"/>' % (x, y, size, size))
            else:
                alpha = max (0.0, 1.0 - d / 3.2) * 0.55
                out.append ('<rect x="%g" y="%g" width="%d" height="%d" rx="6" fill="%s" fill-opacity="%.2f" '
                            'stroke="%s" stroke-width="1.5"/>' % (x, y, size, size, ACCENT, alpha, colours["edge"]))
    return "\n  ".join (out), 3 * step + size


def svg (width, height, body, background=None):
    bg = '<rect width="100%%" height="100%%" fill="%s"/>\n  ' % background if background else ""
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%g" height="%g" viewBox="0 0 %g %g">\n  %s%s\n</svg>\n'
            % (width, height, width, height, bg, body))


def main():
    OUT.mkdir (parents=True, exist_ok=True)
    (OUT / "alt").mkdir (exist_ok=True)
    text, text_w = wordmark (sys.argv[1], CAP_HEIGHT)
    text_y = (ICON_H - CAP_HEIGHT) / 2
    files = {}

    # Icon alone (transparent), and the app icon: the K on a dark rounded square
    files["kaia-icon.svg"] = svg (ICON_W, ICON_H, pad_k (DARK))
    s = 256
    k = 0.7 * s / ICON_H
    files["kaia-app-icon.svg"] = svg (s, s, '<rect width="%d" height="%d" rx="56" fill="#0d0f12"/>\n  ' % (s, s)
                                      + '<g transform="translate(%g,%g) scale(%g)">\n  %s\n  </g>'
                                        % ((s - ICON_W * k) / 2, (s - ICON_H * k) / 2, k, pad_k (DARK)))

    # Wordmark alone (white; the plugin header draws it)
    files["kaia-wordmark.svg"] = svg (round (text_w, 2), CAP_HEIGHT, '<path fill="#ffffff" d="%s"/>' % text)

    # Full logo for dark and light backgrounds
    w = ICON_W + GAP + text_w
    for name, colours in (("dark", DARK), ("light", LIGHT)):
        body = (pad_k (colours) + '\n  <path fill="%s" transform="translate(%g,%g)" d="%s"/>'
                % (colours["text"], ICON_W + GAP, text_y, text))
        files["kaia-logo-%s.svg" % name] = svg (round (w, 2), ICON_H, body)

    # Runner-up: pad one
    for name, colours in (("dark", DARK), ("light", LIGHT)):
        m = 12      # room for the glow
        icon, icon_w = pad_one (colours, m, m + (ICON_H - 132) / 2)
        body = icon + ('\n  <path fill="%s" transform="translate(%g,%g)" d="%s"/>'
                       % (colours["text"], m + icon_w + GAP, m + text_y, text))
        files["alt/kaia-pad-one-%s.svg" % name] = svg (round (2 * m + icon_w + GAP + text_w, 2), ICON_H + 2 * m, body)

    for name, content in files.items():
        (OUT / name).write_text (content)
        print (name)


if __name__ == "__main__":
    main()
