# Changelog

## Unreleased

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
    and gets air absorption (ISO 9613-1), and the reverb starts later the closer the source is. Optional source width
    (narrowed around the source's own panorama position) and time alignment for spot microphones. MIX and the DIRECT
    controls are greyed out and show the values in effect; switching it off hands them back. Real time, without
    rebuilding the impulse response
- New DIRECT tab for the dry signal: HIGHS (shelf at 3 kHz), AIR (air absorption, loss at 10 kHz), WIDTH (narrowed
  around the source's own position), DELAY, and REVERB GAP (the whole reverb starts later than the direct sound)
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
