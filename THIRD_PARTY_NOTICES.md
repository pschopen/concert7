# Third party notices

Concert7 is licensed under the GNU General Public License v3.0 or later (see `LICENSE`).
It is built with and contains code from the following projects.

## Licensing of the distributed binaries

The plugins and the standalone application are linked against the **JUCE framework**, which Concert7 uses under
the **GNU Affero General Public License v3 (AGPLv3)**. GPLv3 section 13 allows combining GPLv3 code with AGPLv3
code; the released binaries are therefore distributed under the terms of the AGPLv3, and the complete
corresponding source code is available in this repository (including the exact JUCE version as a git submodule).

## Components

| Component | Used for | License | Source |
| --- | --- | --- | --- |
| JUCE 8 | plugin framework, GUI, audio I/O | AGPLv3 (or commercial) | <https://github.com/juce-framework/JUCE> (submodule `libs/JUCE`) |
| VST3 SDK (bundled with JUCE) | VST3 plugin format | MIT | <https://github.com/steinbergmedia/vst3sdk> |
| LV2 (bundled with JUCE) | LV2 plugin format | ISC | <https://lv2plug.in> |
| FFTConvolver / KlangFalter by HiFi-LoFi | partitioned convolution, background tail processing (`libs/FFTConvolver`, `src/dsp/Convolver.*`) | GPLv3 or later | <https://github.com/HiFi-LoFi/FFTConvolver>, <https://github.com/HiFi-LoFi/KlangFalter> |
| AudioFFT by HiFi-LoFi | FFT wrapper (`libs/FFTConvolver/AudioFFT.*`) | MIT | <https://github.com/HiFi-LoFi/AudioFFT> |
| Ooura FFT (inside AudioFFT) | FFT on Windows and Linux | free use with attribution, © Takuya Ooura | <https://www.kurims.kyoto-u.ac.jp/~ooura/fft.html> |
| REEV-R by tiagolr | the convolution engine integration Concert7 started from | GPLv3 | <https://github.com/tiagolr/reevr> |

The license texts of the individual components are found in their source files and repositories.

## Not included

**Impulse responses** are *not* part of Concert7 and are not distributed with it. Users download a library
themselves and install it locally. The setup screen links to two free Bricasti M7 libraries Concert7 is tested
with; their makers are independent companies, and there is no partnership with them. Their terms apply to their
libraries (one of them may not be passed on without Bricasti's permission).

Bricasti and M7 are trademarks of their respective owner. Concert7 is not affiliated with Bricasti or with the
makers of the libraries.
