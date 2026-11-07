# Changelog

## Unreleased

- Three libraries: SAM / LS / OTHER above the categories. SAM and LS are two M7 libraries Concert7 is tested
  with (the setup links to them): one with a response per preset, one with the early reflections and the late
  reverb recorded on their own (also the M7's second banks Halls 2, Plates 2, Rooms 2, Spaces 2, and Nonlin).
  OTHER plays any other impulse responses, another library or your own, with its subfolders as categories.
  Switching goes to the same preset where both M7 libraries have it, with the preset's own settings except for
  the kept groups; the library comes with the preset in the project. The setup installs either archive (a `.7z`
  with the system's `tar`) or any archive or folder of impulse responses for OTHER. A preset recorded as two
  blocks is their sum at the files' levels; nothing is cut at a crossover: Decay changes only the late reverb, the
  reflections stay, and EARLY, LATE, the early patterns and the pre-delay work on the real blocks. Where the takes
  differ, the Drift modulation uses the second take instead of a synthesised variant. The display shows SAM or LS
- Decay mode (right-click DECAY): how Decay changes the reverb – Sabine (natural, as before), Proportional
  (brilliant: every frequency by the same factor, like the M7's own multipliers), Sabine with constant level (the
  loudness of the preset at every decay), Stretch (the response resampled by up to ×1.3, a larger or smaller room)
  or Original (the mechanism of 0.1.0-beta, sample for sample, for comparison). The choice stays when presets
  change and is shown in the display (PROP, CONST, STRETCH, ORIG)
- A longer Decay no longer sounds artificial, pumps or breaks where the recording ends: the random phases of the
  continued tail were not random. Seeded per frame with numbers a fixed step apart, the generator gave phases that
  advanced by the same step from frame to frame, a slightly detuned tone in every frequency instead of noise; the
  overlapping frames added it up or cancelled it, so single frequencies came out up to 27 dB too loud or too soft,
  and since the continuation starts at different times in different frequencies, the errors moved during the
  decay (Large Hall at 7 s: 160 Hz fell by 16 dB at 3.3 s). The Drift modulation's variant had the same fault.
  The seeds are hashed now; the continued tail matches ideal decaying noise band by band
- The recording hands over to the continued tail where it leaves its decay line (up to half of its late range
  earlier, before at most 0.5 s), with a 200 ms crossfade instead of 43 ms
- User presets: the whole sound (impulse response and every setting) in a `.c7preset` file. A preset browser
  (BROWSE or the display) shows library and user presets in one list with search, favourites, category and tag
  filters; a click or the arrow keys load a preset, `<` `>` step through the list as filtered. The category
  buttons still open the list of their presets. The display shows a user preset's name and marks changes with `*`
- Keep groups of settings when changing presets (SETUP → Keep when changing presets, or a knob's right-click menu):
  decay, pre-delay, levels, mix, roll-off, multi-band, delay, modulation, ducker, EQ, space – or all of them, so
  only the impulse response changes. Replaces "Hold decay" (that setting carries over)
- Undo / redo (buttons below the logo, Cmd/Ctrl+Z, Shift+Cmd+Z / Ctrl+Y) of your edits, not of host automation;
  a new preset and the values it resets are one step
- A/B comparison: two complete settings, each with its own undo, both stored with the project; switching back to
  a preset used just before skips loading and designing its impulse response
- Type values: double-click a knob's value or use its new right-click menu (also: reset, keep, the host's menu);
  units are optional and a decimal comma works. Hosts' own parameter entry reads the same texts
- Low CPU mode (SETUP, per instance): about half the CPU at 4096 samples latency
- Half rate convolution at 88.2 kHz and above (SETUP, per instance): about a third of the CPU at 96 kHz, same
  level and timing
- The convolution pauses while the input is silent for longer than the reverb
- The reverb has the same level at every sample rate: at 88.2 kHz and above it was 3 dB louder than at
  44.1 / 48 kHz (the impulse responses were normalised per sample, not per second)
- Decay follows room acoustics: it no longer scales every band by the same factor, only the part of the decay
  the walls give. Air absorption is the same in any room, so a longer Decay keeps the highs shorter, like a
  larger hall (Large Hall at 7 s: 8 kHz 3.0 s instead of 4.9 s, the lows a little longer), and a shorter Decay
  is flatter over frequency, like a smaller room (at 0.6 s: 8 kHz 0.54 s instead of 0.42 s)
- A longer Decay no longer sinks and swells where the recording ends: many impulse responses already drop a few
  dB below their decay line shortly before their end (the start of a fade-out or of the noise floor). That dip
  was kept, and the continued tail started on the line again. The recording is now used only as long as it
  follows the line
- A shorter Decay also fades the early reflections: a room that absorbs more absorbs them too. Before, they
  kept their level and the response dropped at the early / late crossover (by about 10 dB for Large Hall at 0.4 s).
  A longer Decay leaves them as they are
- Every frequency keeps its own decay: Decay scales the reverb time each frequency has in the recording
  instead of adding the same amount of decay to a whole third octave, so a frequency that rings longer stays
  relatively longer and does not ring on almost without end
- The continued tail of a longer Decay starts on each frequency's own level. It used to take it from the last
  frames of the recording, averaged with the neighbouring frequencies – in the bass more than an octave, so the
  lows jumped by up to 7 dB where the recording ended and rang on 7 - 15 % too long (Small Hall at 2.3 s)
- The continued tail keeps the coherence of the channels as recorded (it was fully decorrelated). The M7
  recordings are almost decorrelated anyway; impulse responses of coincident or closely spaced microphones
  now keep their image
- A preset just loaded is the recording itself, sample for sample, and the display shows both curves on top of
  each other: nothing changes before you change something (tested on every preset and through the plugin)
- Decay changes the bass at most by its own factor: below the mids the reverb time no longer grows (or shrinks)
  a little more than Decay, so a long deep bass in a recording grows with Decay, not faster
- Decay changes the level as on the M7: the early reflections keep theirs, the late reverb starts a little lower
  the longer it rings (0.55 dB per dB of reverb time, as in captures of the M7 with both blocks at fixed levels). A longer decay
  is therefore louder, but less than its longer tail alone would make it: twice the decay about 2 - 3 dB, three
  times 3 - 4 dB, half of it 2 - 4 dB softer (before, the whole response kept its energy and the early
  reflections moved by up to 4 dB)
- `tools/decay_families.py` checks Decay against the Large / Medium / Small families of the Bricasti library
- Modulation modes in the EXTRAS tab. An impulse response is a frozen snapshot, so the movement of the M7's own
  reverb is not in the recordings; the new modes add it back to the late reverb and leave the early reflections
  alone
  - Drift (most natural, the default; rate 0.3 Hz): the late reverb keeps changing its fine structure – sustained notes shimmer and the stereo
    image moves, but nothing wavers in pitch. Same decay and spectrum; needs about twice the CPU
  - Chorus: slight pitch movement of the late reverb along random curves, different left and right
  - Drift + Chorus: both, like the tail of an algorithmic hall
  - Vibrato: the previous modulation (pitch of the whole reverb). Projects saved with an earlier version keep it
  - At depth 0 the reverb is exactly as before; switching a mode or leaving depth 0 rebuilds the impulse response
- New OFFSET control in the REVERB tab (−5 – +5 s, finer around 0, frame now called TIMING / ROUTING): moves the start of the
  whole impulse response. Negative values cut its beginning with a short fade-in – a negative pre-delay that thins
  out or removes the early reflections and brings the tail in sooner. Like Decay it rebuilds the impulse response
- New Smooth control (0 – 100 %): evens out resonances, ringing and bumps in the decay of the late reverb.
  Like Decay it rebuilds the impulse response, so the change fades in after a moment. At 0 % (default)
  the reverb sounds exactly as before
- The MOD / DUCK tab is now called EXTRAS and holds Modulation, Ducker and Smooth
- New SPACE tab with two optional ways to set Concert7 in terms of a real hall. Both start from the current sound
  when switched on; while off, their greyed knobs and a top view of the hall show the values that match it
  - Hall model: hall volume (m³) and absorption (shown as stone church, bare hall, empty hall, concert hall, opera
    house, studio) set the decay after Eyring's formula; DECAY is greyed out and shows the result
  - Distance: a calibrated depth control for dry sources (0.5 – 60 m). The direct to reverberant ratio follows
    Barron's revised theory for concert halls (through MIX), the direct sound loses presence (near field timbre)
    and gets air absorption (ISO 9613-1), and the reverb starts later the closer the source is. Optional source size
    (narrows the image around the source's own panorama position) and microphone distance (time alignment for spot
    microphones), both off at the start of their knob. MIX and the DRY SIGNAL controls are greyed out and show the
    values in effect; switching it off hands them back. Real time, without rebuilding the impulse response
- New DRY SIGNAL controls, in the SPACE tab below the distance controls that drive them: HIGHS (shelf at 3 kHz),
  AIR (air absorption, loss at 10 kHz), WIDTH (narrowed around the source's own position), DELAY and REVERB GAP
  (the whole reverb starts later than the direct sound)
- Every control, tab and preset category now has a tooltip that explains in plain words what it does
- Choosing a preset, also the current one again, resets all controls to their defaults and DECAY to the preset's
  own decay ("Hold decay when changing presets" still keeps the decay, a hall model's included). Hall model and
  Distance are switched off

## 0.1.1-beta

- Samplicity now lists the Bricasti M7 library at the bottom of its downloads page
  (<https://samplicity.com/downloads/>); the setup screen, installers and README point there
- The installers no longer offer to open the download page, the plugin's setup screen guides you instead
- Windows: German texts on the setup screen are no longer garbled (umlauts)
- The setup screen counts your own impulse responses as well (it showed "0 presets" for a folder without
  Bricasti files)
- README: describes how your own impulse responses (mono, stereo, 4 channel) are used

## 0.1.0-beta

First public beta.

- True stereo convolution reverb (AU, VST3, LV2, standalone) for macOS, Windows and Linux
- Works with the free Bricasti M7 impulse responses by Samplicity: 134 presets in six banks
- Setup screen: download the ZIP from Samplicity's page, drop it onto the plugin, Concert7 checks and installs it
- Decay 0.2 – 30 s by re-synthesising the impulse response, multi-band decay (low/high shelf, ×0.2 – ×4)
- Pre-delay and delay (3 voices) with tempo sync, early/late/VLF levels, early and late roll-off
- 32 swappable early reflection patterns, propagation modes (true stereo, no crossfeed, mono in)
- 5 band post EQ with an interactive curve, modulation, ducker with sidechain input
- Zero latency, glitch-free IR changes (background rebuild, warm-up and crossfade), offline bounce support
- Dot matrix display with decay envelope, reverb time / frequency graph, scalable interface (75 – 200 %)
- Developer tool `concert7-dev` with unit/integration tests, offline renderer, IR export and plugin host tests
