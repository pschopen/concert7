# Changelog

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
