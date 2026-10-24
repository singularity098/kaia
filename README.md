# Kaia

**Kaia is a fast, lightweight drum sampler that plays Akai MPC programs and expansions directly —
no Kontakt, no MPC software.** It is built for Linux first (VST3, LV2, CLAP and a standalone app),
with Windows and macOS builds produced by CI.

![Kaia](docs/editor.png)

* Opens **.xpm** programs (MPC software 1.x/2.x XML and MPC 3.x compressed JSON),
  **.xpn** expansions (read straight from the archive, nothing to unpack), and **.pgm** programs
  from the MPC500/1000/2500, MPC2000/2000XL and MPC3000 (including `.SND` sounds).
* Also plays a folder of WAV/AIFF/FLAC/MP3/OGG files: one sample per pad.
* 128 pads in 8 banks (A–H) with the MPC pad layout and note map, up to 8 layers per pad.
* Velocity layers, round robin and random layers, choke groups, mono pads,
  one-shot / note-on / note-off triggering, envelopes, filters, tuning, reverse.
* 17 stereo outputs (Main + Aux 1–16) for multi-track drum mixing.
* Real-time safe engine: no locks or memory allocation on the audio thread, every sample in RAM,
  high-quality windowed-sinc resampling. About 4–5% of one 2.1 GHz core for 50 simultaneous voices.

## Get it

### Prebuilt Linux binaries

Every push builds **Kaia-linux-x86_64**: open the latest successful *Build* run on the
[Actions](https://github.com/singularity098/kaia/actions) tab and download it under *Artifacts*
(GitHub wraps it in a zip; inside is `Kaia-linux-x86_64.tar.gz`). Tagged versions will also
appear on the [Releases](https://github.com/singularity098/kaia/releases) page once published. Then:

```sh
tar xzf Kaia-linux-x86_64.tar.gz
cd Kaia-linux-x86_64
./install.sh                    # for your user: ~/.vst3, ~/.lv2, ~/.clap, ~/.local/bin
# sudo ./install.sh --system    # or system-wide
# ./install.sh --uninstall
```

Rescan plugins in your DAW. Requirements: x86-64 Linux with **glibc 2.35 or newer**
(Ubuntu 22.04+, Debian 12+, Mint 21+, Fedora 36+, Arch, openSUSE Tumbleweed, …) and the usual
desktop libraries (FreeType, fontconfig, X11; ALSA/JACK/PipeWire for the standalone app).
Open `Kaia Demo Kit.xpn` (included in the archive) to check that everything works.

### Build from source

```sh
git clone https://github.com/singularity098/kaia.git
cd kaia
./build.sh --deps --install     # installs build packages (apt/dnf/pacman/zypper), builds, installs for your user
```

or by hand:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# plugins end up in build/Kaia_artefacts/Release/{VST3,LV2,CLAP,Standalone}
```

JUCE 8 and the CLAP extensions are downloaded automatically during the first configure.

| CMake option | Default | |
|---|---|---|
| `KAIA_BUILD_CLAP` | ON | CLAP plugin |
| `KAIA_BUILD_TESTS` | ON | `KaiaTests`: parser and engine tests |
| `KAIA_PORTABLE_LINUX` | OFF | static libstdc++ and a glibc 2.35 baseline, for redistributable binaries |
| `KAIA_BUILD_HOST_TEST` | OFF | developer tools: host test harness and UI snapshot renderer |
| `KAIA_JUCE_DIR` | | use a local JUCE checkout instead of downloading |

## Using Kaia

* **Open** a program (.xpm/.pgm), an expansion (.xpn), a folder, or audio files — or drag any of
  them onto the window. Recent files are under *Open → Recent*.
* **Programs** lists every program in the open expansion or folder; click one or use the arrow
  keys to switch kits. `<` and `>` step through them.
* **Pads**: click to play (higher on the pad = harder hit). Right-click for load/copy/paste/clear.
  Drop audio files on a pad to replace its sample; several files become round-robin layers.
* **Pad editor**: level, pan, tune, velocity sensitivity, envelope, filter, trigger mode, layer
  mode, choke group, output and mono/poly, plus per-layer velocity range, level, tuning, pan and
  reverse. Double-click a layer to replace its sample.
* **Outputs** routes pads to Aux 1–16 for separate mixer channels. In VST3 hosts, enable the
  plugin's extra outputs (Reaper: pin connector; Bitwig: add chains per output). LV2 and CLAP
  always expose all 17 outputs. If an aux output is switched off in the host, its pads play through
  Main, so nothing goes silent.
* **Saving**: the DAW project stores the kit and every pad edit. Samples are referenced from their
  original location, not copied into the project; if you move your library, use **Relink…** and
  point it at the new folder.

### Envelope modes

* **Tail fade** (MPC "decay from end"): the sample plays fully; *Decay* fades out its tail. This is
  the MPC default for drums.
* **Decay** (MPC "decay from start"): the sound fades out over *Decay* right after the attack —
  use it to shorten hits.
* **AHDSR**: classic envelope with sustain; *Release* starts when the note ends (note-on mode).

## Format support

| Format | What is read |
|---|---|
| **XPM (MPC 2.x XML)** | drum and keygroup programs, pad note map, up to 8 layers, velocity ranges, layer play mode (velocity / cycle / random), mute groups and mute targets, simultaneous-play targets, trigger mode, mono/polyphony, level/pan/tune (pad and layer), velocity sensitivity, amp envelope (AD / AHDSR, decay from start/end), filter type/cutoff/resonance/velocity, sample start/end/slices, reverse, loops (note-on mode), keygroup root notes and key tracking |
| **XPM (MPC 3.x JSON)** | the same, from the compressed format MPC 3 firmware writes; samples in `<program>_[ProgramData]` are found automatically |
| **XPN** | zip expansions including Zip64 (> 4 GB); programs and samples are read directly from the archive; title, manufacturer and cover art are shown |
| **PGM MPC500/1000/2500** | 64 pads × 4 samples, velocity ranges, tuning, play mode, voice overlap, mute groups, attack/decay/decay mode, filter, velocity → level/cutoff, mixer level/pan, pad note map |
| **PGM MPC2000/2000XL** | per-note sound, tuning, attack/decay/decay mode, cutoff/resonance, velocity → level, velocity-switch and simultaneous modes, voice overlap, mute assigns, mixer level/pan, pad → note map; `.SND` and `.WAV` sounds |
| **PGM MPC3000** | the same note parameters as the 2000XL, MPC3000 `.SND` sounds |

Samples are matched the way an MPC does: by name, case-insensitively, next to the program first,
then in sub-folders, then in a sibling folder (for the common `Programs/` + `Samples/` layout).

Not reproduced: MPC insert effects, LFOs, pitch and filter envelopes, Q-Link assignments,
warp/time-stretch, the MPC2000XL "decay switch" mode (played as a normal note) and the MPC's
individual-output routing (all pads start on Main; use **Outputs** to route them).

**Copy-protected expansions**: some commercial expansions are encrypted so that only Akai's
software can open them. Kaia detects them and says so; it does not try to unlock them.

## Quality checks

* `KaiaTests` builds synthetic programs in every supported format, loads them, renders MIDI
  through the engine and checks the audio: velocity layers, round robin, chokes, tuning,
  resampling, reverse, note-off behaviour, multi-out routing, state round trips, keygroups,
  zero heap allocations on the audio thread, CPU use, and fuzzed garbage input. With
  `--real-data <folder>` it also runs against real MPC files.
* Release binaries are checked with pluginval (strictness 10, VST3), clap-validator (CLAP), and a
  host harness (`KaiaHostTest`) that loads the built VST3 and LV2 like a DAW, plays every pad,
  restores saved state and checks multi-out routing.
* The test suite also passes under AddressSanitizer and UndefinedBehaviorSanitizer.

Set `KAIA_DEBUG=1` in the environment to print diagnostics from the plugin to stderr.

## Troubleshooting

* **"N samples not found"**: the program refers to samples by name; keep the pack's folder
  structure intact, or use **Relink…** to point Kaia at the folder that holds them.
* **Pads are silent in a multi-out setup**: a pad routed to an aux output plays only there if the
  host has that output enabled. Use *Outputs → All pads to Main* to reset.
* **Plugin doesn't load on an older distro**: the prebuilt binaries need glibc 2.35+; build from
  source instead.

## Contributing

See [CLAUDE.md](CLAUDE.md) for architecture, threading rules, format notes and the test/release
checklist.

## License

Kaia is free software under the GNU Affero General Public License v3 (see `LICENSE`). It uses
JUCE (AGPLv3), the VST3 SDK (GPLv3 option), the LV2 headers (ISC) and CLAP with
clap-juce-extensions (MIT). See [THIRD_PARTY.md](THIRD_PARTY.md).

MPC is a trademark of inMusic Brands. Kaia is an independent project, not affiliated with or
endorsed by Akai Professional or inMusic.
