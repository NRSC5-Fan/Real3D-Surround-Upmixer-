# Changelog

All notable changes to Real3D are listed here.

## [1.3] - 2026-10-19

Version numbering continues from the original 0.9.0. First release of the 32-bit **and** 64-bit build in a single component file (`foo_dsp_real3d-x64+x86.fb2k-component`).

### Added
- **64-bit (x64) support** for foobar2000 v2.x. The package also contains the 32-bit build, so one file installs on either.
- **Center spread** slider (0-100%, off by default).
- **Selectable block size**: 1024, 2048, 4096 (default), 8192 or 16384 samples.
- **New LFE controls**:
  - low-pass cutoff, 0-200 Hz (0 = nothing is split off)
  - LFE level, 0-200%
  - bass split method: time-domain Linkwitz-Riley crossover (12 / 24 / 48 dB per octave) or soft FFT fade
  - **LFE Copy**: copy the split bass to front left/right and/or the center channel
- **Dark theme** with a smooth fade. It follows the Windows app mode on first open, and clicking the logo switches it; the choice is remembered.
- Real3D logo in the settings dialog.
- Versioned presets (Boost class version 2), so future settings can be added without breaking saved presets.
- Test programs for the decoder core, the new features and the LFE processing (`tests/`).

### Changed
- Renamed to **Real3D**: DSP name, component name, about text, module `foo_dsp_real3d.dll`, and "Real3D surround wrap" (was "Real3D ambient processor").
- The "Bass transition lower/upper" sliders are replaced by the single cutoff and the level control. Older presets with bass redirection on load with the soft FFT fade around their old transition range.
- Project files updated for Visual Studio 2022+ (C++20, v143/v145), a current foobar2000 SDK, Boost 1.92 and WTL.

### Fixed
- Overlapping `memcpy` in the decoder (now `memmove`).
- Mismatched `new[]` / `delete` on the FFT configuration.
- 64-bit pointer-size truncations in the decoder and the stream chunker.
- DSP settings that could fall back to the defaults because presets were read through `std::strstream`.

### Known limitations
- Presets are not portable between the 32-bit and 64-bit builds (Boost binary archives store a pointer-sized length).
- Presets saved by this version can't be read by older builds.
- In crossover mode only the mono sum of the bass goes to the LFE; the left-minus-right difference below the cutoff is not kept in the main channels.
- `get_latency()` reports 0 (as in the original).
