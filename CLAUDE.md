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
                            editable note table + custom maps (drum-maps/*.kaiamap), learned notes;
                            resolve() = which pads each MIDI note plays
  Model/UserFiles           config folder (XDG on Linux, KAIA_CONFIG_DIR override, legacy
                            ~/Kaia migration), SharedSettings (PropertiesFile), Presets, drum-maps/,
                            Favourites (favourites.txt)
  Formats/
    ZipReader               own zip reader: stored + deflate, Zip64, detects encryption (never decrypts)
    SampleSource            audio decoding (JUCE formats + .SND), SampleIndex (find samples by name),
                            ProgramLocation, bytesToText (safe UTF-8/UTF-16/Latin-1 decoding)
    SndDecoder              MPC2000XL (01 04) and MPC3000 (01 02) .SND
    XpmParser               XPM XML + JSON  -> Kit (sample *names*, resolved later)
    PgmParser               MPC1000-family, MPC2000XL, MPC3000 .pgm -> Kit
    KitLoader               listPrograms (folder / xpn / siblings), parseProgram (+ sample resolution),
                            loadSamples (parallel decode, weak cache), reloadKit, relinkMissing,
                            readPackArtwork (cover only, for the library tiles)
    Library                 scan of the library folder into packs (xpn / program folder / sample
                            folder), XML cache, LibraryService (shared, background scan thread)
  Engine/
    SampleData.h            immutable decoded audio (float, zero-padded both ends), SampleMap
    KitRuntime              immutable render snapshot of a Kit (built on the message thread)
    SamplerEngine           voices, MIDI, layer selection, chokes, envelopes, SVF filter, multi-out
    SincTable               polyphase windowed-sinc tables (24 taps, 256 phases, 8 anti-alias bands)
  PluginProcessor           buses (Main + Aux 1-16), loader thread, state save/restore, edit API
  UI/                       LookAndFeel (near-black theme, Jost font via uiFont(), user accent colour,
                            pad colour themes), PadGrid (MPC layout, pad 1 bottom-left, library
                            drop target), PadEditor, ProgramBrowser, LibraryBrowser (TreeView,
                            audition, drag to pads; TileView: pack covers in 2-4 columns),
                            FileBrowser (Files tab: plain folder tree over setting `filesFolder`,
                            filter over a background walk), SearchBox (text field + clear "x"),
                            KineticScroller (drag/fling/wheel scrolling on VBlank), SettingsPanel
                            (drum map editor), PluginEditor (header: pack artwork, last MIDI note;
                            the logo is a transparent link to GitHub)
Resources/Fonts/            Jost Light/Regular/Bold (static instances of NAMsayin's variable Jost, OFL),
                            embedded as binary data (KaiaFonts)
Resources/Logo/             the logo (made by tools/make_logo.py): a K built from pads (stem in the
                            text colour, arms lit in the accent) + "KAIA" in Michroma as outlines.
                            kaia-logo-dark/light (README), kaia-icon, kaia-app-icon(-N.png: plugin
                            ICON_BIG/ICON_SMALL, installer's hicolor icons), kaia-wordmark.svg
                            (embedded as KaiaLogo; the header draws the pad K in code so it follows
                            the accent). alt/ keeps the runner-up "pad one" logo (4x4 grid, pad 1 lit)
Tests/
  TestMain.cpp              KaiaTests: synthetic fixtures for every format + engine/audio assertions
  HostTest.cpp              KaiaHostTest: loads the *built* VST3/LV2 binary like a DAW and checks audio
  UiSnapshot.cpp            KaiaUiSnapshot: renders the real editor to PNG (docs/editor.png; also
                            library.png/library-favourites.png/library-tiles.png/files.png/settings.png
                            when given a library
                            folder, files-filter.png with KAIA_SNAPSHOT_FILTER=<text>;
                            KAIA_SNAPSHOT_PAD_THEMES=1 adds one shot of the pads per colour theme)
tools/
  make_fonts.py             makes Resources/Fonts/ from Jost[wght].ttf (needs fontTools)
  make_logo.py              makes the Resources/Logo/ SVGs from Michroma-Regular.ttf (needs fontTools);
                            re-render the PNGs from the SVGs with a browser afterwards
  make_demo_pack.py         generates "Kaia Demo Kit.xpn" (synthesized drums, safe to redistribute)
  sandbox-sysroot.sh        header-only sysroot for build machines without the distro -dev packages
  xatoms.c                  pre-registers X11 atoms so JUCE windows work on a bare Xvfb
cmake/glibc_compat.{h,c}    portable Linux build: glibc 2.35 baseline (see below)
packaging/install.sh        user/system install of the release archive (also `--from <build dir>`)
build.sh                    one-command source build for users (`--deps`, `--install`)
.github/workflows/build.yml CI: Linux (portable, ubuntu-22.04) + Windows + macOS, runs KaiaTests
```

UI text: always use `uiFont (height, bold)`. It draws Jost 1.4x larger than the layout height (Jost
has a small x-height), Regular below 24 px and Light above, Bold for bold text; the look-and-feel
maps JUCE's own default fonts (menus, dialogs, text editors) to Jost too. Don't keep `Font`s in
statics (they hold the typeface past JUCE's shutdown).

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
* MIDI Learn: the engine publishes the last note-on as `(sequence << 8) | note` in an atomic (and
  its velocity in another); the processor's timer polls it while a pad is learning, the editor's
  header and the settings map editor poll it for display and "Learn".
* Pads started by the same note event never choke each other (`eventFirstSerial`): a note may play
  several pads (open hi-hat = A08 + A04). Simult targets carry the original input note, so the
  same note-off releases them.
* Sample loops are only honoured when the shared setting `playLoops` is on (off by default);
  `buildRuntime (..., playLoops)` drops them otherwise.
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
A05 snare rim, A06 cross-stick, A07 crash 1, A08 HH open, A09 tom 4 (floor) .. A12 tom 1 (high), A13
ride, A14 ride bell, A15 crash 2 edge, A16 crash 2; tom rims and the ride edge play the head/bow pad.
The open hi-hat notes also play A04 (owner's request: both pads at once). This layout and the
TD-10 notes come from the owner's own TD-10: toms reversed (low tom on A09), 22/26 hi-hat edges,
52 and 10 -> A15. The TD-10 table is the owner's own choice: 49 -> A13 (with 59), 55 -> A16, and
51, 53, 57 unmapped ("too much"); the other Roland classic modules keep 49/55 -> A07, 51/59 -> A13,
53 -> A14, 57 -> A16. Note tables are factory defaults: Roland classic (TD-3..TD-10)
36/38/40, toms 48/50 45/47 43/58 41/39, HH 42+22 / 46+26 / 44, crash 1 49/55, crash 2 57 + edge 52,
ride 51/59, bell 53; modern Roland and Alesis add 37 cross-stick; Yamaha DTX toms 48/47/43 (DTX402
reference), 37 closed rim, 83 foot splash; GM for the rest. Partly checked against the published
TD-17/TD-27 and DTX402 maps; the rest is unverified. The active map is a note table in
`TriggerMap::notes` (editable in Settings, saveable as `custom:<name>`); settings store it only when
it differs from the factory table (`drumNotes`), so built-in modules pick up improved defaults,
while projects always store it. With a module, unlisted
notes are ignored and HH pads share mute group 33 (`kHiHatChokeGroup`) unless the kit groups them.
Keygroup programs ignore the map. The map is global (settings) and also saved per project
(`<TriggerMap>` in `KaiaState`); projects without one keep the instance's default.

Engine behaviour worth knowing: velocity -> level is linear amplitude scaled by `velToLevel`;
pan is a balance law (centre = unity); chokes fade in 5 ms, mono retrigger 3 ms; loops only apply
in note-on mode and only with the `playLoops` setting on; program individual-output routing is ignored (all pads start on Main).

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
   `docs/editor.png`, `Resources/Logo/kaia-logo-*.svg` (README header), `icons/`
   (`Resources/Logo/kaia-app-icon-*.png`, used by install.sh), and `Kaia Demo Kit.xpn` from
   `tools/make_demo_pack.py`. The CI Linux job produces the same archive as an artifact.
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
* Drum-module maps: only the TD-10 has been tried with real hardware (by the owner).
* Library: one root folder; packs are listed at the top level (presets under a heading above them);
  opening a pack opens its Programs and Samples groups. Program pads are parsed on the message
  thread when expanded. The list starts fully collapsed ("My presets" too; result headings open);
  Expand/Collapse all opens packs (not programs) / closes everything but a pack's group headings.
* Library favourites: star buttons on program and sample rows; keys are
  `program|sample <TAB> pack file <TAB> path in pack`, one per line in `favourites.txt` (re-read
  before every change, so instances don't overwrite each other). The star filter shows them as
  flat Programs/Samples results (combined with the search words).
* Program previews: a sample in a pack folder whose path contains "previews" (`[Previews]/Kit.xpm.wav`
  or `Kit.wav`) belongs to the program of the same name. Row buttons are hit-tested in
  `Item::buttonAt` and act on mouse-up like other clicks, but also on the second click of a
  double-click, so play/stop can be clicked quickly. The stop state lasts while
  `SamplerEngine::getSoundingPreview()` reports the preview's id (set by the audio thread from its
  voice scan, no extra work per sample).
* Header tabs, in this order: Open, Files, Library, Programs, then `<` `>` (they step Programs).
* Files tab (`FileBrowser`): never indexed or cached on disk. Folders are listed from disk when
  opened (folders first, natural order, hidden files skipped; only audio, programs, presets and
  .xpn shown). The filter walks the whole folder once per showing of the tab on a 1-thread pool
  (cancellable, capped at 300k files), then matches every word against the file name or its
  folder path in memory; at most 1000 results, shown as a tree with every folder open. Samples
  are dragged with the library's `kaiaLibrary: sample` description, so `dropOnPad` handles them.
* Touch: Linux delivers touch as mouse events, so lists use `ScrollOnDragMode::all`; the library
  (list and tiles) scrolls with `KineticScroller` instead: drag, fling with friction (shared setting
  `flingStrength` 0..1, Settings slider 0-10), wheel glide, all applied once per frame from a
  `VBlankAttachment` (on Linux the peer repaints right after the VBlank callbacks, so there is no
  beating between an animation timer and the repaint timer). It only starts a drag-and-drop when
  the drag starts sideways (`Item::dragIsVertical`, and never while the press scrolls) and acts on
  single clicks on mouse-up (`pendingClick` -> `Item::clicked()`: open/close containers and
  headings, load programs/presets, play samples/pads) unless the press scrolled or stopped a fling;
  double-click = `activate()` (put on pad). The TreeView's scroll bars are hidden (its viewport
  passes the wheel on), with our own ScrollBar.
* Library tiles: covers are read on a 2-thread pool only for tiles on screen (jobs skip themselves
  when scrolled past), kept downscaled (<= 512 px, at most 160 covers) and each tile is drawn once
  into an image at the screen scale. View mode and columns: settings `libraryView`, `libraryColumns`.
  `KaiaUiSnapshot` writes library-tiles.png; `KAIA_SNAPSHOT_BENCH=1` prints the list's paint time.
