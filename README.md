<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logo-dark.png">
    <img alt="Real3D Spatial Audio" src="docs/logo-light.png" width="360">
  </picture>
</p>

# Real3D Surround Upmixer

Real3D is a DSP plugin for [foobar2000](https://www.foobar2000.org/) that upmixes stereo to surround, from 2.1 all the way up to 16.1 channels. It is based on FreeSurround and works as a 32-bit and 64-bit component for foobar2000 v2.x.

## Features

- **Many speaker layouts**: stereo, 3.1, 4.1, 5.1, 6.1, 7.1, 9.1, 11.1, 13.1 and 16.1, plus panorama and wrap variants.
- **Soundstage controls**: surround wrap (0-360 degrees), front/back shift, depth, focus, and separate front and rear stereo separation.
- **Center spread**: moves part of the center channel into front left/right (constant power).
- **Selectable block size** (1024 to 16384 samples; 4096 by default) to trade delay against frequency resolution.
- **LFE / bass management**:
  - low-pass cutoff from 0 to 200 Hz and an LFE level from 0 to 200%
  - bass split method: a time-domain Linkwitz-Riley crossover (12, 24 or 48 dB per octave; very little ringing) or a soft FFT fade
  - **LFE Copy**: also send the split bass to front left/right and/or the center channel
- **Dark theme**: follows the Windows app mode on first open, and you can switch by clicking the logo. The choice is remembered.

## Install

1. Download `foo_dsp_real3d-x64+x86.fb2k-component` from the [Releases](../../releases) page. One file contains both the 32-bit and the 64-bit build.
2. In foobar2000 open **Preferences > Components > Install...** and pick the file, then restart foobar2000.
3. Open **Preferences > Playback > DSP Manager**, add **Real3D** to the active chain and use the configure button to open its settings.

Presets saved by an older 32-bit build can't be read by a 64-bit build and the other way around (a limitation of Boost's binary archives). Same-architecture presets from earlier Real3D builds still load.

## Building

Needs Visual Studio 2022 or newer (C++20, MFC), the foobar2000 SDK (2023 or newer for 64-bit), Boost with the serialization library, and WTL. Open `foo_dsp_fsurround.vs2022.sln`, build **Release | x64** and **Release | Win32**, then run `powershell tools\package_fb2k_component.ps1` to create the `.fb2k-component` package. Full instructions are in [BUILDING.md](BUILDING.md); the 64-bit source changes are listed in [X64_PORT_NOTES.md](X64_PORT_NOTES.md).

Decoder tests need only a C++ compiler (see `BUILDING.md`).

## Credits

- Real3D DSP, modified and enhanced by **NRSC5-Fan**
- Originally developed by **pro_optimizer** (the foobar2000 FreeSurround plugin)
- FreeSurround decoder by Christian Kothe
- Claude (Anthropic) helped with bug fixing and testing

## Licensing

This project is licensed under the GNU General Public License v3.0.
Some source files may be licensed under the BSD 3-Clause License; see individual file headers for details.
