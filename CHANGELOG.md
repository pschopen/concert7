# Changelog

## Unreleased

- New Smooth control (0 – 100 %): evens out resonances, ringing and bumps in the decay of the late reverb.
  Like Decay it rebuilds the impulse response, so the change fades in after a moment. At 0 % (default)
  the reverb sounds exactly as before
- The MOD / DUCK tab is now called EXTRAS and holds Modulation, Ducker and Smooth

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
