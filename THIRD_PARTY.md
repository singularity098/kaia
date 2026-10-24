# Third-party software and references

## Linked into the binaries

| Component | License | |
|---|---|---|
| [JUCE 8](https://github.com/juce-framework/JUCE) | AGPLv3 (or commercial) | framework, plugin wrappers, audio file decoders |
| VST3 SDK (bundled with JUCE) | GPLv3 option of the Steinberg dual license | VST3 wrapper |
| LV2 headers (bundled with JUCE) | ISC | LV2 wrapper |
| [CLAP](https://github.com/free-audio/clap) and [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) | MIT | CLAP wrapper |
| zlib, FLAC, libvorbis, libpng (bundled with JUCE) | zlib / BSD-style | decoding |

"VST" is a trademark of Steinberg Media Technologies GmbH.

## Format documentation used (no code copied)

The file formats were implemented from these open descriptions and implementations:

* [ConvertWithMoss](https://github.com/git-moss/ConvertWithMoss) by Jürgen Moßgraber (LGPLv3):
  MPC XPM (XML and JSON) field meanings, MPC1000 and MPC2000 program layouts.
* [VMPC2000XL / mpc2000xl_kaitai](https://github.com/izzyreal/mpc2000xl_kaitai) by Izmar Verhage:
  Kaitai Struct specs of the MPC2000XL and MPC3000 PGM/SND formats, envelope timing, velocity switching.
* [MPCMaid](https://github.com/cyriux/mpcmaid) by Cyrille Martraire: MPC1000 PGM offsets.
* [mpc_valet](https://github.com/plule/mpc_valet): a reference XPM saved by MPC software 2.1.

Real program files from the VMPC2000XL and MPCMaid repositories were used in testing; they are
not distributed with Kaia.
