# CLAUDE.md

Guidance for Claude (and other agents) working on this repository.

## What Kaia is

Kaia is a drum sampler plugin (JUCE 8, C++20) that plays Akai MPC programs and expansions
directly: `.xpm` (MPC 1.x/2.x XML and MPC 3.x gzip-JSON), `.xpn` expansions (zip, read in place),
and `.pgm` (MPC500/1000/2500, MPC2000/2000XL, MPC3000 with `.SND` sounds). Formats: VST3, LV2,
CLAP and Standalone on Linux (primary platform); VST3/CLAP/Standalone (+AU) on Windows/macOS via CI.

The owner built this to replace Kontakt Player for drum libraries because Kontakt crackled.
**Glitch-free, low-CPU playback is the top priority.** Never regress real-time safety.

## Repository workflow

* **Always commit and push directly to `main`.** No feature branches or PRs for this project.
* End commit messages with the attribution lines given by the session (Co-Authored-By etc.).
* Keep `README.md` (user-facing) and this file in sync when behaviour, options or formats change.
* **Never write into the user's library** (expansions, program folders, samples). Kaia writes
  only inside the config folder (`UserFiles::configDirectory()`): settings, presets, library cache.
* Don't commit build output (`build*/` is ignored), generated `.xpn` packs, or third-party MPC
  files used for testing (they belong to other projects; keep them outside the repo).

## Layout

```
Source/
  Model/Kit.h/.cpp          Kit / Pad / Layer data model, XML (de)serialisation for plugin state,
                            MPC default pad->note map, MPC fader-law helpers
  Model/TriggerMap          e-drum modules (note -> DrumZone tables), fixed zone->pad layout,
                            learned notes; resolve() = which pads each MIDI note plays
  Model/UserFiles           config folder (XDG on Linux, KAIA_CONFIG_DIR override, legacy
                            ~/Kaia migration), SharedSettings (PropertiesFile), Presets
  Formats/
    ZipReader               own zip reader: stored + deflate, Zip64, detects encryption (never decrypts)
    SampleSource            audio decoding (JUCE formats + .SND), SampleIndex (find samples by name),
                            ProgramLocation, bytesToText (safe UTF-8/UTF-16/Latin-1 decoding)
    SndDecoder              MPC2000XL (01 04) and MPC3000 (01 02) .SND
    XpmParser               XPM XML + JSON  -> Kit (sample *names*, resolved later)
    PgmParser               MPC1000-family, MPC2000XL, MPC3000 .pgm -> Kit
    KitLoader               listPrograms (folder / xpn / siblings), parseProgram (+ sample resolution),
                            loadSamples (parallel decode, weak cache), reloadKit, relinkMissing
    Library                 scan of the library folder into packs (xpn / program folder / sample
                            folder), XML cache, LibraryService (shared, background scan thread)
  Engine/
    SampleData.h            immutable decoded audio (float, zero-padded both ends), SampleMap
    KitRuntime              immutable render snapshot of a Kit (built on the message thread)
    SamplerEngine           voices, MIDI, layer selection, chokes, envelopes, SVF filter, multi-out
    SincTable               polyphase windowed-sinc tables (24 taps, 256 phases, 8 anti-alias bands)
  PluginProcessor           buses (Main + Aux 1-16), loader thread, state save/restore, edit API
  UI/                       LookAndFeel (near-black theme, user accent colour), PadGrid (MPC layout,
                            pad 1 bottom-left, library drop target), PadEditor, ProgramBrowser,
                            LibraryBrowser (TreeView, audition, drag to pads), SettingsPanel, PluginEditor
Tests/
  TestMain.cpp              KaiaTests: synthetic fixtures for every format + engine/audio assertions
  HostTest.cpp              KaiaHostTest: loads the *built* VST3/LV2 binary like a DAW and checks audio
  UiSnapshot.cpp            KaiaUiSnapshot: renders the real editor to PNG (docs/editor.png; also
                            library.png/settings.png when given a library folder)
tools/
  make_demo_pack.py         generates "Kaia Demo Kit.xpn" (synthesized drums, safe to redistribute)
  sandbox-sysroot.sh        header-only sysroot for build machines without the distro -dev packages
  xatoms.c                  pre-registers X11 atoms so JUCE windows work on a bare Xvfb
cmake/glibc_compat.{h,c}    portable Linux build: glibc 2.35 baseline (see below)
packaging/install.sh        user/system install of the release archive (also `--from <build dir>`)
build.sh                    one-command source build for users (`--deps`, `--install`)
.github/workflows/build.yml CI: Linux (portable, ubuntu-22.04) + Windows + macOS, runs KaiaTests
```

Namespace is `kaia`. Code style follows JUCE (4-space indent, `function (args)` with a space,
braces on their own line).

## Threading model (must hold)

* **Message thread** owns `Kit` and `SampleMap` in `KaiaProcessor`. All edits go through
  `editPad`/`editKit` -> `rebuildRuntime()` -> `engine.setRuntime(buildRuntime(...))`.
* **Loader thread** (`KaiaProcessor::LoaderThread`) runs jobs that capture *copies* of what they
  need and post results back with `postToMessageThread` (guarded by the `alive` flag). Every
  user-initiated load bumps `loadGeneration`; stale jobs/results are dropped and
  `progressCallback(generation)` cancels superseded loads.
* **Audio thread** only touches `SamplerEngine::process`. It must never allocate, free, lock or
  block. New kits arrive through an atomic `pending` pointer; the previous `KitRuntime` is
  *retired* until no voice references it, then handed to the message thread through a lock-free
  FIFO (`collectGarbage()` on a 10 Hz timer). UI pad clicks reach the engine through a lock-free
  FIFO (`auditionNoteOn/Off`).
* MIDI input goes through `KitRuntime::padsForInput` (trigger map resolved at build time);
  UI auditions and simult targets use `padsForNote` (program notes). Voices carry the pad's
  program note (`note`, used by mute targets) and the MIDI note that started them (`inputNote`,
  used by note-off). Changing the trigger map = `rebuildRuntime()`; nothing on the audio path
  costs more than a table lookup, and Kaia must stay zero-latency.
* Library previews: a `PreviewSound` (pad + its samples, decoded on `previewPool`) becomes part of
  the next runtime snapshot (`KitRuntime::preview`, `previewId`); `engine.startPreview(id)` waits
  until that snapshot is active. Every `rebuildRuntime()` passes the current preview along.
* MIDI Learn: the engine publishes the last note-on as `(sequence << 8) | note` in an atomic; the
  processor's timer polls it while a pad is learning.
* `KaiaTests` overrides global `operator new` and fails if `process()` allocates — keep it passing.
* `getStateInformation` may be called off the message thread: it then uses `cachedKitXml`
  (refreshed by the timer when `stateDirty`). `setStateInformation` loads samples synchronously
  so offline renders right after a project load are correct.

## Format decisions (verify against real files when possible)

XPM (MPC software):
* `Instrument number` = pad number, 1-based (0-based detected automatically); `PadNoteMap`
  `PadNote number` likewise. Missing map -> MPC default (A01=37, A02=36, A03=42, ...; banks E-H get
  the unused notes 0-34, 99-127).
* Instrument/program `Volume` is a fader position: gain = 2·v², so 0.707946 = 0 dB, 1.0 = +6 dB.
  Layer `Volume` is linear gain (1.0 = 0 dB). Pans are 0..1 with 0.5 centre.
* `ZonePlay`: 0 cycle, 1 velocity (all matching layers stack), 2 random, 3 treated as cycle.
* `TriggerMode`: 0 one-shot, 1 note-off (plays on release), 2 note-on; if absent, `OneShot`.
* Envelope times are normalised 0..1 on a log scale 1 ms .. 100 s. `VolumeADEnvelope` True = AD;
  `VolumeDecayType` True = decay from **end** (sample plays fully, tail fades over Decay).
* Layer `RootNote` is stored as MIDI note + 1. Keygroup key tracking = `!IgnoreBaseNote || KeyTrack`.
* Region: `SliceStart/SliceEnd` if set, else `SampleStart/SampleEnd`; `Offset` added to start.
* `MuteTarget1-4` / `SimultTarget1-4` are interpreted as MIDI notes (0 = off). Unverified.
* MPC 3 JSON: gzip, 5 header lines (`ACVS`, version, `SerialisableProgramData`, `json`, OS) then
  JSON; `program.type == 1` is keygroup; instruments in `drum.instruments`, layers in `layersv`.
  Samples usually live in `<program>_[ProgramData]/`.
* Filter type ids 1-18 map to LP/HP/BP/notch 12/24 dB; 29 = MPC3000 LP; others pass through.

PGM:
* MPC500/1000/2500: 10756 bytes, header "MPC1000 PGM 1.00", 64 pads × 0xA4 from 0x18, 4 samples ×
  0x18 per pad, pad->note table at 0x2918. Tuning in cents. Velocity range byte order: low, high.
* MPC2000XL (`07 04`) and MPC3000 (`07 00`): parameters are **per note** (35..98) plus a
  pad->note table; tune in 1/10 semitone; velocity switch: vel > threshold1 plays note A's sound,
  > threshold2 note B's. Unmapped notes that have a sound get pads in banks E+.
* Classic attack/decay 0..100 -> (v²+1)/2 ms (VMPC2000XL firmware curve); decay mode 0 = end.
* `.SND`: 2000XL has a 42-byte header and stores stereo as the whole left block then the right;
  MPC3000 has a 38-byte header, 44.1 kHz.

E-drum modules (`TriggerMap.cpp`): zone layout A01 kick, A02 snare, A03 HH closed, A04 HH pedal,
A05 snare rim, A06 cross-stick, A07 aux, A08 HH open, A09-A12 toms 1-4, A13/A14 crash 1/2, A15
ride, A16 ride bell; rims/edges play the head/bow pad. Note tables are factory defaults: Roland
classic (TD-3..TD-10) 36/38/40, toms 48/50 45/47 43/58 41/39, HH 42/46/44, crashes 49/55 57/52,
ride 51/59/53; modern Roland and Alesis add 37 cross-stick, 22/26 HH edges; Yamaha DTX toms
48/47/43 (DTX402 reference), 37 closed rim, 83 foot splash; GM for the rest. Partly checked against the
published TD-17/TD-27 and DTX402 maps (kick, snare, rim, toms, hi-hats, ride bell); the TD-10 and
the rest are unverified. With a module, unlisted
notes are ignored and HH pads share mute group 33 (`kHiHatChokeGroup`) unless the kit groups them.
Keygroup programs ignore the map. The map is global (settings) and also saved per project
(`<TriggerMap>` in `KaiaState`); projects without one keep the instance's default.

Engine behaviour worth knowing: velocity -> level is linear amplitude scaled by `velToLevel`;
pan is a balance law (centre = unity); chokes fade in 5 ms, mono retrigger 3 ms; loops only apply
in note-on mode; program individual-output routing is ignored (all pads start on Main).

Encrypted (copy-protected) expansions are detected and reported. Do not add decryption.

## Building

Normal machine:
```sh
./build.sh --deps            # installs -dev packages, configures, builds into ./build
# or
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
JUCE 8.0.10 and clap-juce-extensions (pinned commit) are fetched by CMake unless
`-DKAIA_JUCE_DIR=...` / `-DKAIA_CLAP_EXT_DIR=...` point at local checkouts.

Options: `KAIA_BUILD_PLUGIN` (ON), `KAIA_BUILD_CLAP` (ON), `KAIA_BUILD_TESTS` (ON),
`KAIA_PORTABLE_LINUX` (OFF; use for release binaries), `KAIA_BUILD_HOST_TEST` (OFF; builds
`KaiaHostTest` and `KaiaUiSnapshot`). Release builds use LTO and take 6-8 minutes on 2 cores
(about 13 with the host-test tools as well); the tests-only build (`-DKAIA_BUILD_PLUGIN=OFF`)
takes about a minute and is the one to iterate with for parser/engine work.

Restricted sandbox (no apt, GitHub reachable): run `tools/sandbox-sysroot.sh <dir>` and export
the `PKG_CONFIG_PATH`, `CPATH` and `LIBRARY_PATH` it prints before configuring. In the Claude
agent sandbox the GitHub Actions API is blocked by the proxy, so CI results can't be read from
there; check the Actions tab on github.com (or ask the owner) after pushing.

## Testing — run before every push that touches Source/

```sh
./build/KaiaTests_artefacts/Release/KaiaTests                 # all checks must pass
./build/KaiaTests_artefacts/Release/KaiaTests --real-data DIR  # optional: real MPC files
```
Tests set `KAIA_CONFIG_DIR` to their work dir, so they never touch the real config folder. For
`KaiaUiSnapshot` set it yourself.

Real-data layout used so far (not in the repo): `DIR/mpcmaid/test.pgm` (MPCMaid),
`DIR/vmpc/TEST1|TEST2/*.PGM+*.SND`, `DIR/vmpc/RealMpc2000xl`, `DIR/vmpc/RealMpc3000`
(VMPC2000XL repo), `DIR/mpc_valet/Reference.xpm`.

Sanitizers: configure a Debug build with `-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"`
(and the same linker flag) and `-DKAIA_BUILD_PLUGIN=OFF`; everything except the CPU-budget
check must pass.

Plugin binaries (Linux, headless):
* pluginval: `xvfb-run -a pluginval --strictness-level 10 --validate-in-process --validate build/Kaia_artefacts/Release/VST3/Kaia.vst3`
  (download pluginval_Linux.zip from github.com/Tracktion/pluginval releases).
* clap-validator: `clap-validator validate build/Kaia_artefacts/Release/CLAP/Kaia.clap`
  (github.com/free-audio/clap-validator releases). 3 preset-discovery tests are expected to skip.
* Host test: `KaiaHostTest <Kaia.vst3|Kaia.lv2> <program> <outdir> [--editor-seconds N]`.
  It wraps the state the way a host does (VST3 `VST3PluginState`/`IComponent`, LV2 Turtle blob)
  — don't use `setPlayConfigDetails` on hosted instances, it resets bus activation.
* For any JUCE GUI tool on Xvfb: start Xvfb with `-noreset`, run the compiled `tools/xatoms`
  first. The embedded VST3 editor still renders blank under a WM-less Xvfb (XEmbed); use
  `KaiaUiSnapshot <pack> docs <pad>` for screenshots instead.

## Releases (Linux)

1. Configure with `-DKAIA_PORTABLE_LINUX=ON`, build.
2. Check the glibc baseline: `objdump -T <binary> | grep -oE 'GLIBC_[0-9.]+' | sort -V | tail -1`
   must be `GLIBC_2.35` and there must be no `GLIBCXX` entries. If new symbols appear, add a
   hidden shim to `cmake/glibc_compat.c` rather than raising the baseline.
3. Package `Kaia-linux-x86_64/`: `VST3/ LV2/ CLAP/ Standalone/` (strip the binaries with
   `strip --strip-unneeded`), `install.sh`, `README.md`, `LICENSE`, `THIRD_PARTY.md`,
   `docs/editor.png`, and `Kaia Demo Kit.xpn` from `tools/make_demo_pack.py`. The CI Linux job
   produces the same archive as an artifact.
4. Re-run pluginval, clap-validator and the host test on the stripped, installed binaries.

## Plugin identity (don't change casually: hosts key saved projects on these)

Product "Kaia", manufacturer code `Kaia`, plugin code `Kai1`, VST3/AU bundle id
`io.github.singularity098.kaia`, LV2 URI `https://github.com/singularity098/kaia`, CLAP id
`io.github.singularity098.kaia`. Plugin state root element `KaiaState` (version 1).

## Known gaps / ideas

* XPM drum programs saved by real MPC 2.x/3.x software haven't been tested yet (only synthetic
  files and an MPC 2.1 keygroup template). Envelope time curves and mute/simult targets are the
  least certain mappings.
* Not implemented: MPC insert effects, LFOs, pitch/filter envelopes, Q-links, warp,
  MPC2000XL decay-switch, per-program output routing, MIDI channel filter.
* Windows/macOS builds are produced by CI but haven't been tried in a host.
* Drum-module maps haven't been tried with real hardware yet (TD-10 included).
* Library: one root folder; program pads are parsed on the message thread when expanded.
