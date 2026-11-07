# Concert7

[![CI](https://github.com/pschopen/concert7/actions/workflows/ci.yml/badge.svg)](https://github.com/pschopen/concert7/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/pschopen/concert7?include_prereleases)](https://github.com/pschopen/concert7/releases)
[![License: GPL v3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)

**Concert7** is an open source true stereo reverb (AU, VST3, LV2, standalone – macOS, Windows, Linux) built for
free libraries of **Bricasti M7 impulse responses** (and plays any others): a hardware-style dot matrix display with
banks of presets, a big decay control and pages for early/late balance, roll-offs, multi-band decay, delay, ducking
and a post EQ. Concert7 is an independent project and is not affiliated with Bricasti or with the makers of the
libraries.

**Website:** <https://concert7.github.io/> · **[Documentation of all controls](https://concert7.github.io/docs/)**

Concert7 stands on the shoulders of two open source projects, and we are very grateful to their authors:
**[REEV-R](https://github.com/tiagolr/reevr) by tiagolr**, the convolution reverb Concert7 started from, and
**[KlangFalter](https://github.com/HiFi-LoFi/KlangFalter) by HiFi-LoFi**, whose
[FFTConvolver](https://github.com/HiFi-LoFi/FFTConvolver) engine does the actual convolution. Thank you!

> **Beta:** 0.1.0 is the first public release. Please report problems in the
> [issue tracker](https://github.com/pschopen/concert7/issues).

![Concert7](doc/concert7.png)

## Download and installation

Get the installer for your system from the [releases page](https://github.com/pschopen/concert7/releases).

| System | File | Installs |
| --- | --- | --- |
| macOS 10.13 or newer (Apple Silicon and Intel) | `Concert7-<version>-macOS.pkg` | AU and VST3 to `/Library/Audio/Plug-Ins/…`, standalone app to `/Applications` |
| Windows 10/11 (64 bit) | `Concert7-<version>-Windows-Setup.exe` | VST3 to `C:\Program Files\Common Files\VST3`, standalone app |
| Linux (x86_64, glibc 2.35+, e.g. Ubuntu 22.04+) | `Concert7-<version>-Linux-x86_64.tar.gz` | `./install.sh` copies VST3, LV2 and the standalone app to `~/.vst3`, `~/.lv2`, `~/.local/bin` (`sudo ./install.sh --system` for all users) |

**The installers are not signed** (no Apple/Microsoft developer certificate), so the systems warn before the first start:

- **macOS:** double-clicking the `.pkg` shows "Apple could not verify …". Open *System Settings → Privacy & Security*,
  scroll down and click **Open Anyway** next to the Concert7 message, then run the installer. Alternatively, in
  Terminal: `xattr -d com.apple.quarantine ~/Downloads/Concert7-*-macOS.pkg`, then double-click it.
- **Windows:** SmartScreen shows "Windows protected your PC" – click **More info → Run anyway**.

### Impulse responses (required)

Concert7 does **not** ship any impulse responses. Its setup screen (also under SETUP → "Install impulse
responses…") links to two free Bricasti M7 libraries it is tested with – their makers are independent companies,
there is no partnership with them – and takes any other impulse responses as well. SAM / LS / OTHER above the
category buttons switches between the three:

| | Library | Installed into |
| --- | --- | --- |
| **SAM** | an M7 library with one response per preset (134 presets, ZIP ≈ 1 GB; Concert7 extracts the quad channel files, ≈ 520 MB) | `Impulse Responses` |
| **LS** | an M7 library with the early reflections and the late reverb recorded on their own, eight takes, 96 kHz (221 presets: the same and the M7's second banks Halls 2, Plates 2, Rooms 2, Spaces 2, and Nonlin; 7z ≈ 2.4 GB) | `Split Captures` |
| **OTHER** | any other impulse responses: another library or your own; subfolders become the categories | the folder you choose, or `Other Impulse Responses` for an archive |

The folders are in `~/Library/Application Support/Concert7` (macOS), `%APPDATA%\Concert7` (Windows) or
`~/.config/Concert7` (Linux). Download an archive with the setup screen's link and drop it onto the screen (or pick
it) – Concert7 checks what it holds and installs it, you can delete it afterwards. A 7z is unpacked with the
system's `tar` (libarchive: macOS, Windows 10 and later; on Linux `bsdtar` is needed); without it, unpack it
yourself and choose the folder ("Already unpacked? Choose folder…"). An unpacked M7 library in a folder named
after the hardware in Downloads, Music or Documents is found by itself. Files at 44.1 and 48 kHz are used at their
multiples, other rates are resampled once when a preset loads.

**Switching** goes to the same preset in the other M7 library (bank and number; Spaces 17 of the SAM library is
Spaces 16 of the LS library, presets without a counterpart switch to the first preset of their category) and, like
choosing a preset, starts with its own settings and decay except for the kept groups. The library comes with the
preset in the project; new instances start with the one chosen last.

**Two blocks:** a preset recorded as early reflections and late reverb is their sum at the files' levels. That
balance is not always the M7 preset's mix: against the other library the late reverb is within ±3 dB for 61 % of
the presets but 6 dB or more off for one in six, and no rule computed from the files comes closer, so it is left to
EARLY and LATE. Since the blocks are recordings of their own, nothing is cut at a crossover: Decay changes only the
late reverb and the reflections stay as they are (as on the M7); EARLY / LATE, the early patterns and the pre-delay
work on the real blocks. Where the takes have a fine structure of their own (the second banks), the Drift
modulation uses take 2 instead of a synthesised variant.

**User presets** (BROWSE → SAVE AS...) store the whole sound – impulse response and every setting – as
`.c7preset` files in the `Presets` folder next to the library folder (BROWSE → FOLDER). They can be copied between
computers; one whose impulse response is missing loads with the first preset and says so.

**Your own impulse responses (OTHER):** every WAV, AIFF or FLAC file in the chosen folder (and any other one in the
library folders); its first subfolder is its category, files in the folder itself are *User*. Add files and use
SETUP → "Rescan library". A 4 channel file is used as true stereo (channel order LL, LR, RL, RR), a stereo file as
L→L / R→R and a mono file on both sides; files at other sample rates are resampled. The early patterns are taken
from an M7 library (the one in use, for OTHER the SAM library if it is there).

## Features

| Area | Concert7 |
| --- | --- |
| Presets | an M7 library with 134 presets (Halls, Plates, Rooms, Chambers, Ambience, Spaces) or one with 221 (the same and the second banks, Nonlin; early reflections and late reverb recorded on their own), or any other impulse responses (mono, stereo or 4 channel true stereo) – SAM / LS / OTHER switches |
| User presets | the whole sound (impulse response and every setting) in a `.c7preset` file; preset browser with search, favourites, categories and tags; tags for the library presets too |
| Keep on preset change | eleven groups of settings (decay, pre-delay, levels, mix, roll-off, multi-band, delay, modulation, ducker, EQ, space) or all of them can be kept when another preset is chosen; padlocks on their knobs |
| Undo / A/B | undo / redo of your edits (not of host automation), Cmd/Ctrl+Z; A/B with two complete settings, each with its own undo, both stored with the project |
| Value entry | double-click a knob's value (or right-click it) to type a value, units optional (`2,5 s`, `250 ms`, `1.2k`, `off`); hosts' own text entry reads them too |
| Decay | 0.2 – 30 s, starts at each preset's analysed native RT60 (optionally kept on preset change) |
| Decay mode | right-click DECAY: Sabine (natural, default) – Proportional (brilliant) – Sabine, constant level – Stretch (room size) – Original (the mechanism of 0.1.0-beta); stays when presets change, shown in the display |
| Pre-delay | 0 – 500 ms or tempo synced, applied to the late reverb |
| Offset | −5 – +5 s (finer around 0), moves the start of the whole impulse response; negative values cut its beginning (negative pre-delay, thinner or no early reflections) |
| Levels | Early, Late and VLF (late reverb below ~120 Hz) |
| Roll-off | separate low pass for early reflections and late reverb, 80 Hz – 22 kHz |
| Multi-band decay | low shelf 80 Hz – 4.8 kHz and high shelf 200 Hz – 16 kHz, ×0.2 – ×4 |
| Early patterns | 32 patterns taken from the early part of other presets of the library in use, ordered short → long |
| Delay | 100 ms – 1 s or tempo synced, level –20…0 dB / off, 3 voices of the late reverb |
| Ducker | amount, threshold, release, sidechain input (EXTRAS tab) |
| Post EQ | high pass 12 dB, low shelf/bell, mid bell, high shelf/bell, high cut 12 dB, interactive curve |
| Propagation | True Stereo, No Crossfeed (L→L / R→R), Mono In |
| Modulation | four modes with depth / rate (EXTRAS tab): Vibrato (pitch of the whole reverb), Chorus (pitch of the late reverb, decorrelated left / right), Drift (most natural, the default: the late reverb keeps changing its fine structure, no pitch change), Drift + Chorus; early reflections stay untouched except in Vibrato |
| Smooth | 0 – 100 %, evens out resonances, ringing and bumps in the decay of the late reverb (EXTRAS tab) |
| Space | hall model: volume (m³) and absorption (stone church … studio) set the decay (Eyring); distance: places a dry source 0.5 – 60 m away – direct to reverberant ratio, presence, air absorption, gap before the first reflection, optional source width and time alignment (SPACE tab) |
| Direct | tone (HIGHS shelf, AIR absorption), WIDTH and DELAY of the dry signal, REVERB GAP of the whole reverb against it – by hand or set by the distance mode (SPACE tab, below the distance controls) |
| Interface | scalable UI (75 – 200 %), meters, dot matrix display with decay envelope, English / German setup screen |
| Latency | zero (non-uniform partitioned convolution, tail on a background thread); 4096 samples in low CPU mode |
| CPU | low CPU mode (about half the CPU, 4096 samples latency), half rate convolution at 88.2 kHz and above (a third at 96 kHz), the convolution pauses while the input is silent |

### Known limitations

- Changing an IR parameter rebuilds the impulse response in the background; the new reverb fades in after a moment.
- Presets with an abrupt, gated envelope keep their cut-off when lengthened and get a longer tail after it.
- The interface uses the system font, so it looks slightly different on Windows and Linux.
- Distance mode treats the input as the direct sound and the impulse response as everything the hall adds (true for
  the Bricasti library). Own impulse responses that contain a direct sound keep it. Use the mode as an insert on a dry
  source, not on an effect send.
- Not (yet) implemented: surround/Atmos.
- Low CPU mode convolves in blocks of 4096 samples: average CPU is about half, but the block that completes a chunk
  carries its work. At very small buffers with long, modulated reverbs at 96 kHz, combine it with half rate
  convolution.
- Tested by hand on macOS (REAPER 7, auval). Windows and Linux builds are compiled and unit tested by CI only.

## How it works

Static impulse responses cannot simply be "made longer". Concert7 re-synthesises the response whenever an IR
parameter changes (`src/dsp/IRDesigner.cpp`):

1. **Analysis** (once per preset): onset detection, third-octave band envelopes from an STFT, noise-floor / fade-out
   detection per band and a noise compensated Schroeder fit (Lundeby style) of the late reverb time. The
   captures with the two blocks recorded on their own are analysed on their late reverb alone; their tails reach a noise floor and are faded out
   after it, so the floor is looked for before the fade, and a band that never rises 20 dB above it takes its
   neighbours' reverb time.
2. **Split** into early reflections and late reverb (raised cosine crossover ≈ 80 ms after the onset). The
   captures with two blocks need none: they are the early part and the late part.
3. **Late reverb**: every STFT bin gets a time-varying gain that turns its own native decay slope (fitted per bin)
   into the target slope (Decay × low/high multipliers), so a frequency that rings longer stays relatively longer.
   A shorter decay fades the early reflections the same way. The gain is capped so the level never rises above the
   target decay line, which keeps the recording's noise floor down. Where the recording ends or reaches its noise
   floor, the tail is continued with random-phase noise that starts on each bin's fitted decay line, keeps the
   measured coherence of the channels and follows the target decay – so a 1 s room can become a 20 s space.
   The recording is used only as long as it follows its decay line (in the bass of the Bricasti recordings often
   only half of it), and it crossfades into the continuation over 200 ms.
   The early reflections keep their level and the late reverb starts 0.55 dB lower per dB of reverb time
   (captures of the M7 with both blocks at fixed levels), so a longer decay is louder, its energy grows with RT^0.45.
   How Decay spreads over frequency is the **decay mode** (right-click DECAY). Nobody knows which the M7 itself does –
   there are no recordings of one program at several decays – so the user chooses:
   * *Sabine (natural)*, the default: only the wall part of the decay rate scales, air absorption (ISO 9613-1) stays,
     so a longer decay keeps the highs shorter, like a larger hall.
   * *Proportional (brilliant)*: every frequency scales by the same factor, as the M7's HF / LF RT MPY multiply its
     reverb time. Long settings keep the brilliance of the highs (Large Hall at 7 s: 8 kHz 4.9 s instead of 3.1 s).
   * *Sabine, constant level*: Sabine, and the whole response is scaled back to the preset's energy (500 Hz – 2 kHz,
     measured on the result; roll-off and Smooth stay the user's), so turning Decay never changes the level.
   * *Stretch (room size)*: the response is resampled by up to ×1.3 (×1/1.3 shorter): reflections, build-up and
     colour move with it like in a larger or smaller room; Sabine does the rest of the way.
   * *Original (0.1.0-beta)*: the first mechanism, for comparison, sample for sample as in 0.1.0-beta: one decay
     per third octave scaled proportionally, the early reflections and the start of the late reverb as they are,
     the continuation from the last frames' level (each channel its own noise, a 43 ms crossfade).
   Below the mids no frequency changes its reverb time by more than Decay's own factor (Sabine would let the bass,
   which has no air part, grow a little more), so a long deep bass in a recording grows with Decay,
   not faster. A preset at its own decay is the recording itself, sample for sample: nothing changes before you
   change something.
   `tools/decay_families.py` checks the result against the Large / Medium / Small families of the library.
   VLF and the late roll-off are applied as spectral gains. **Smooth** lowers bins that stick out of their
   third-octave neighbourhood (measured with the native decay removed, so long ringing counts as much as loud) and
   pulls each band envelope towards its fitted decay line.
4. **Assembly**: early part (own or from the selected pattern, with roll-off), late part shifted by the pre-delay,
   plus delay voices. The offset then moves the whole response: leading silence, or its start is cut with a 2 ms
   fade-in.
5. **Modulation streams**: an impulse response is a frozen snapshot – the internal modulation of the M7 is not in
   the recordings. For the Chorus and Drift modes the late reverb is assembled as a response of its own (early
   part + late reverb = the whole response), so it can move while the early reflections stay put. Drift adds a
   **variant** of the late reverb: every STFT frame gets new random phases (the same for all four channels, so the
   stereo relation stays), and a 10 ms energy match keeps its envelope – same decay, same spectrum, independent
   fine structure.

**Modulation** (`TailModulator` in `src/dsp/PostProcessing.cpp`) runs on the convolver outputs. Drift turns the late
reverb between itself and its variant, `cos θ · late + sin θ · variant` with θ following a smooth random curve
(up to ±180° at 100 %): the energy stays the same, but the tail keeps changing – sustained notes shimmer, the stereo
image moves, nothing wavers in pitch. Chorus delays the late reverb by 1 … 4 ms along smooth random curves,
independently left and right (at 100 % and 0.5 Hz up to about 5 cents). Drift + Chorus uses both, the chorus at half depth.
Vibrato is the sine vibrato on the whole reverb of 0.1.x; projects saved before the modes keep it. Drift convolves
the late reverb twice and needs about twice the CPU; at depth 0 and in Vibrato the response is not split at all.

**SPACE tab** (`src/dsp/Distance.cpp`): two optional ways to set Concert7 in terms of a real hall. Both start from
the current sound when switched on, and while off their greyed knobs show the values that match it.

**Hall model** sets the reverb by the size of the hall (SIZE, its volume in m³) and how much it absorbs
(ABSORPTION). The decay follows from Eyring's formula T = 0.161 V / (−S ln(1−α)) for a shoebox hall (length 2 W,
height 0.8 W); it is shown below the two knobs, and DECAY is greyed out. Switching it on keeps the decay (the volume
starts at an estimate, 2 s ≈ 16 000 m³, the absorption at the value that gives the current decay – about 0.27 for
the Bricasti halls, a typical occupied concert hall); switching it off hands the decay back to DECAY. The absorption
is shown as a room type (stone church, bare hall, empty hall, concert hall, opera house, studio). It sets the mid
band decay only – the frequency balance stays the preset's and the multi-band decay's – and, like DECAY, it rebuilds
the impulse response. A larger volume lengthens the reverb but does not move the preset's early reflections, so it
works best with the hall presets.

**Distance mode** moves a dry source nearer or further away – a calibrated depth control for spot microphones and
dry recordings. The Bricasti responses contain no direct sound, so the dry input is treated as the direct sound and
the convolution as everything the hall adds. Everything the mode does is expressed in ordinary controls – MIX and
the DRY SIGNAL controls below the distance controls (HIGHS, AIR, WIDTH, DELAY, REVERB GAP) – which are greyed
out and show the values in effect while it is on. While it is off, the SPACE tab shows where your current mix places
the source (the distance at which the model gives the same direct to reverberant ratio). Switching it on starts
exactly there without changing the sound, every change is applied relative to the values the controls had, and
switching it off hands the values in effect back to the controls – nothing jumps either way, and everything can be
fine-tuned by hand afterwards.

- **Direct to reverberant ratio** follows Barron's revised theory (Barron & Lee 1988), which matches measurements in
  concert halls: direct energy 100/r², reflected energy (31200 T/V)·e^(−0.04 r/T). The starting point is calibrated
  against the energy of the built impulse response in the 500 Hz – 2 kHz band; the ratio change moves MIX through its
  normal mix law, so the level behaves exactly as if you turned MIX. The hall volume is the hall model's (or its
  estimate while the hall model is off).
- **HIGHS** (near field timbre): a close microphone hears more presence and air than a seat in the hall. A gentle
  high shelf (3 kHz, about 1.5 dB per doubling of the distance, −6 … +3 dB) follows what engineers do by ear – a
  heuristic, not physics.
- **AIR**: air absorption of the direct sound over the extra path after ISO 9613-1 (20 °C, 50 % humidity), as a
  short minimum phase FIR (no latency); the knob shows the loss at 10 kHz. It is subtle at hall distances.
- **REVERB GAP** (gap before the first reflection) from a shoebox hall (balcony fronts about a third of the hall
  width from the centre line): closer than the start the whole reverb starts later. Further away the gap stays – the
  impulse response is never cut, and the Bricasti halls already start within a few ms. PRE-DELAY (late reverb only)
  stays yours.
- Optional, both off at the start of their knob: with a **SOURCE SIZE** (the real width of the instrument or
  ensemble, not the stereo width) **WIDTH** narrows the direct sound to the angle the source covers, around its own
  panorama position (wide, decorrelated sources lose up to ≈ 3 dB when narrowed strongly); with a **MIC DISTANCE**
  **DELAY** adds the travel time from the spot microphone to the distance (spot microphones against a main pair).

The distances are nominal: they follow from the model and the estimated volume, not from a measured hall, and the
perspective of the reverb itself (one capture per preset) does not change. The mode runs in real time and never
rebuilds the impulse response.

The rebuilt IR is loaded on a worker thread into a second true stereo convolver (4 paths LL/LR/RL/RR), warmed up
with the last seconds of input so the new tail is already ringing, and cross-faded in (80 ms). Offline renders
wait for pending rebuilds, so bounces never contain a dry gap.

## Building from source

Requirements: CMake ≥ 3.22, a C++17 compiler (Xcode 15+, Visual Studio 2022, GCC 11+ / Clang 14+), git.

```bash
git clone --recursive https://github.com/pschopen/concert7.git   # JUCE 8 lives in libs/JUCE
cd concert7
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Linux needs these development packages (Debian/Ubuntu names):

```bash
sudo apt install build-essential cmake libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libgl1-mesa-dev
```

Artefacts end up in `build/Concert7_artefacts/Release/{AU,VST3,LV2,Standalone}`. Options:
`-DCONCERT7_COPY_AFTER_BUILD=ON` installs the plugins into your user plugin folders, `-DCONCERT7_BUILD_LV2=ON/OFF`
(on by default on Linux), `-DCONCERT7_BUILD_DEVTOOLS=OFF` skips the developer tool,
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` builds a universal binary on macOS.

Installers are built by GitHub Actions when a tag `v*` is pushed (`.github/workflows/release.yml`); the scripts are in
`packaging/`.

## Test environment

Everything can be tested without a DAW through the developer tool `concert7-dev`
(`build/Concert7Dev_artefacts/Release/Concert7Dev`). It uses its own temporary settings file, so it never touches
your plugin configuration. Point it at the extracted IR archive with `--irs DIR` or `CONCERT7_IR_DIR`.

```bash
tools/run_tests.sh /path/to/extracted/bricasti/irs   # macOS: build + tests + snapshots + auval + host tests
```

| Command | Purpose |
| --- | --- |
| `concert7-dev test [--irs DIR] [--category dsp\|library\|processor]` | unit and integration tests |
| `concert7-dev ir --preset "Halls/01 Large Hall" --set decay=6 --out ir.wav` | export the designed 4-channel IR |
| `concert7-dev render --in dry.wav --out wet.wav --preset … --set mix=100` | render audio through the full processor |
| `concert7-dev analyze --preset …` | per band analysis (RT, valid range, levels) of a preset |
| `concert7-dev presets` | list all presets with their native reverb time |
| `concert7-dev snapshot --out ui.png [--tab 0-3] [--welcome] [--drag] [--install archive.zip [--busy]]` | render the GUI to a PNG (no window needed); `--install` runs the real ZIP installation into a temporary folder; `CONCERT7_LANG=en\|de` forces the dialog language |
| `concert7-dev bench --preset … [--block 64]` | CPU load and worst block time |
| `concert7-dev host --plugin ~/Library/Audio/Plug-Ins/VST3/Concert7.vst3` | load the **built plugin binary** like a DAW (VST3 path or AU id `AudioUnit:Effects/aufx,Cc7r,Cnc7`): host parameter changes, offline bounce, state restore into a second instance, editor |
| `concert7-dev extract --zip archive.zip --out DIR` | run the installer's extraction on a local copy of the archive |
| `python3 tools/analyze_ir.py ir.wav … --plot edc.png` | octave band T20 table, energy decay curves, spectrograms |

What the tests cover:

* **DSP** – resampler accuracy and aliasing rejection, RT60 estimation on synthetic IRs with known decay,
  per-band analysis, exact reproduction with neutral settings (also with a long deep bass), decay scaling
  ×0.35 … ×3 and extension beyond the recording, the bass changing with Decay no more than the mids, multi-band multipliers, pre-delay, levels, delay voices, roll-off, VLF, early pattern swap,
  true stereo convolution against direct convolution for block sizes 1 … 4096, click-free IR switching while
  audio runs, smooth (resonance and echo reduction at unchanged reverb time), EQ gains, modulator and ducker;
  modulation: early part + late reverb add up to the whole response, the late variant keeps envelope, spectrum and
  channel relation but not the fine structure, every stream is convolved with its own part, drift keeps the level,
  the chorus pitch movement stays small.
* **Library** (real Bricasti files) – file name parsing, 134 presets in 6 categories, 32 distinct early patterns,
  quad files identical to L/R pairs, every preset loads and analyses plausibly, every preset at its own decay is
  the recording itself, the decay control works on real presets and changes their bass no more than the mids.
* **Processor** – synchronous load in `prepareToPlay`, processor impulse response equals the designed IR, a preset
  just chosen (also after changes) is the recording itself in the convolution and the display, preset
  change resets / holds decay, state save/restore across sample rates, random block sizes with extreme settings,
  tempo sync, offline rendering, mix 0 % bit transparency, mono input, editor creation.
* **Plugin formats** – `auval -v aufx Cc7r Cnc7` validates the Audio Unit; `concert7-dev host` loads the installed
  VST3 and AU binaries through the real plugin interfaces (both run by `tools/run_tests.sh`).

## License

Concert7 is free software under the GNU General Public License v3.0 or later (see `LICENSE`). The released
binaries include JUCE, used under the AGPLv3, and are therefore distributed under the terms of the AGPLv3; see
`THIRD_PARTY_NOTICES.md` for all components and their licenses. Impulse responses are not part of this project. Bricasti and M7 are trademarks of their respective owner.

Changes: see `CHANGELOG.md`.
