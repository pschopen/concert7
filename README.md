# Concert7

[![CI](https://github.com/pschopen/concert7/actions/workflows/ci.yml/badge.svg)](https://github.com/pschopen/concert7/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/pschopen/concert7?include_prereleases)](https://github.com/pschopen/concert7/releases)
[![License: GPL v3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)

**Concert7** is an open source true stereo reverb (AU, VST3, LV2, standalone – macOS, Windows, Linux) built for
free libraries of **Bricasti M7 impulse responses** (and plays any others), with a hardware-style dot matrix
display, banks of presets and a big decay control. Concert7 is an independent project and is not affiliated with
Bricasti or with the makers of the libraries.

**Website:** <https://concert7.github.io/> · **[Documentation of all controls](https://concert7.github.io/docs/)**

Concert7 stands on the shoulders of two open source projects, and we are very grateful to their authors:
**[REEV-R](https://github.com/tiagolr/reevr) by tiagolr**, the convolution reverb Concert7 started from, and
**[KlangFalter](https://github.com/HiFi-LoFi/KlangFalter) by HiFi-LoFi**, whose
[FFTConvolver](https://github.com/HiFi-LoFi/FFTConvolver) engine does the actual convolution. Thank you!

> **Beta:** 0.2.2-beta. Please report problems in the [issue tracker](https://github.com/pschopen/concert7/issues).

![Concert7](doc/concert7.png)

## Features

- **Three libraries** – SAM / LS / OTHER: two free M7 libraries Concert7 is tested with (134 and 221 presets; the
  second with early reflections and late reverb recorded on their own) and any other impulse responses, mono,
  stereo or 4 channel true stereo
- **Decay** 0.2 – 30 s from each preset's native reverb time, by room acoustics or one of five decay modes; a preset
  just loaded is the recording itself
- **Reverb controls**: pre-delay (tempo sync), offset, early / late / VLF levels, roll-offs, multi-band decay,
  32 early patterns, delay voices, ducker, post EQ, true stereo / no crossfeed / mono in
- **Movement**: modulation (Drift, Chorus, Drift + Chorus, Vibrato), Smooth; TAKES tab (LS): the reverb moves
  through the preset's eight takes, late offset
- **SPACE tab**: hall model (volume, absorption) and a calibrated source distance with dry signal controls
- **Workflow**: user presets with a browser (search, favourites, tags), keep groups of settings when changing
  presets, undo / redo, A/B, typed values, tooltips, scalable UI, English / German setup
- **Zero latency**; Low CPU mode, half rate convolution at high sample rates, pause on silence

Every control is described in the [documentation](https://concert7.github.io/docs/), every change in
`CHANGELOG.md`.

## Download and installation

Get the installer for your system from the [releases page](https://github.com/pschopen/concert7/releases):
`.pkg` for macOS 10.13+ (Apple Silicon and Intel: AU, VST3, standalone), `-Setup.exe` for Windows 10/11 (VST3,
standalone), `.tar.gz` for Linux x86_64 (`./install.sh`: VST3, LV2, standalone).

**The installers are not signed**, so the systems warn before the first start:

- **macOS:** *System Settings → Privacy & Security* → **Open Anyway**, or in Terminal
  `xattr -d com.apple.quarantine ~/Downloads/Concert7-*-macOS.pkg`
- **Windows:** "Windows protected your PC" → **More info → Run anyway**

**Impulse responses are required and not included.** Concert7's setup screen (also SETUP → "Install impulse
responses…") links to the two free M7 libraries it is tested with – their makers are independent companies, there
is no partnership with them. Download an archive and drop it onto the setup screen; for OTHER choose any folder of
WAV, AIFF or FLAC files (subfolders become the categories).

## Building from source

Requirements: CMake ≥ 3.22, a C++17 compiler (Xcode 15+, Visual Studio 2022, GCC 11+ / Clang 14+), git.

```bash
git clone --recursive https://github.com/pschopen/concert7.git   # JUCE 8 lives in libs/JUCE
cd concert7
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Linux needs the ALSA, JACK, FreeType, Fontconfig, X11 and OpenGL development packages (see
`.github/workflows/ci.yml`). Tests: `tools/run_tests.sh [IR_FOLDER] [SPLIT_CAPTURES_FOLDER]` (developer tool
`concert7-dev`, also renders, analyses and benchmarks). Installers are built by GitHub Actions when a tag `v*` is
pushed; the scripts are in `packaging/`.

## License

Concert7 is free software under the GNU General Public License v3.0 or later (see `LICENSE`). The released
binaries include JUCE, used under the AGPLv3, and are therefore distributed under the terms of the AGPLv3; see
`THIRD_PARTY_NOTICES.md` for all components and their licenses. Impulse responses are not part of this project.
Bricasti and M7 are trademarks of their respective owner.
