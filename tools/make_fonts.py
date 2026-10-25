#!/usr/bin/env python3
"""Makes the static Jost instances in Resources/Fonts/ from the variable font.

Jost (SIL Open Font License, Resources/Fonts/OFL.txt) is the font NAMsayin uses. JUCE can't pick a
weight from a variable font, so this writes one file per weight; CMake embeds them as binary data.

Needs fontTools (pip install fonttools). Usage: python3 tools/make_fonts.py <path to Jost[wght].ttf>
"""
import pathlib
import sys

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

OUT = pathlib.Path(__file__).resolve().parent.parent / "Resources" / "Fonts"
WEIGHTS = {"Jost-Light.ttf": 300, "Jost-Regular.ttf": 400, "Jost-Bold.ttf": 700}

for name, weight in WEIGHTS.items():
    font = instancer.instantiateVariableFont(TTFont(sys.argv[1]), {"wght": weight}, updateFontNames=True)
    font.save(OUT / name)
    print("%s: weight %d, %d bytes" % (name, weight, (OUT / name).stat().st_size))
