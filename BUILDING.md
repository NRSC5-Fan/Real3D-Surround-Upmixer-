# Building Real3D

Real3D is a foobar2000 DSP component (MFC dialog, Boost.Serialization presets). Core decoder: `freesurround_decoder.*`, `channelmaps.cpp` (large lookup tables), `kiss_fft*`. Plugin shell and dialog: `freesurround.cpp`, `foo_dsp_fsurround.rc`.

## Requirements
- Visual Studio 2022 or newer with the C++ desktop workload and MFC (static MFC is used)
- foobar2000 SDK (2023 or newer for 64-bit components; tested with the 2026-10-01 SDK)
- Boost with the serialization library (tested with 1.92; static libs for x64 and Win32, static runtime: `b2 --with-serialization link=static runtime-link=static address-model=64` / `32`)
- WTL (required by the SDK's libPPUI; for example the `wtl` NuGet package)

## Steps
1. Put this folder next to the SDK folders: `..\SDK`, `..\helpers`, `..\foobar2000_component_client`, `..\shared`, `..\..\pfc` and `..\..\libPPUI` (the layout of the SDK download, with this project at `foobar2000\foo_dsp_fsurround`).
2. Set `BOOST_ROOT` to your Boost folder (libraries are looked up in `stage\x64\lib` and `stage\Win32\lib`, or set `BOOST_LIBRARYDIR`). Set `WTL_INCLUDE` to WTL's include folder. Alternatively build with `/p:VendoredBoostSerialization=true` to compile `sterilization\*.cpp` (those sources must match your Boost headers).
3. Build `foo_dsp_fsurround.vs2022.sln` as **Release | x64** and **Release | Win32**. The project uses the static runtime (`/MT`), so the SDK projects must use `/MT` too. The output is `foo_dsp_real3d.dll`.
4. Run `powershell tools\package_fb2k_component.ps1` to create `out\foo_dsp_real3d.fb2k-component` (32-bit DLL at the package root, 64-bit DLL in `x64/`).

The old `foo_dsp_fsurround.sln` and `foo_dsp_tutorial.vcproj` are the original VS2008 Win32-only files, kept for reference. See `X64_PORT_NOTES.md` for the source changes made for 64-bit.

## Notes for editing
- Source files use CRLF line endings. `freesurround_decoder.cpp`, `freesurround_decoder.h` and `foo_dsp_fsurround.rc` are Windows-1252 (degree signs); edit them with encoding-preserving tools.
- Default settings must keep sounding as before: block size 4096, center spread 0, bass redirection off.
- Presets are Boost binary archives with a class version (`BOOST_CLASS_VERSION(freesurround_params, 2)`). Never reorder existing fields; add new ones behind `if (v >= N)`. Version 1 added center spread and block size, version 2 the LFE settings. Presets older than version 2 load with the soft FFT fade around their old transition range.
- `center_image` is stored in presets but unused by the decoder (left alone on purpose).
- The theme preference (follow Windows / light / dark) is stored in a foobar2000 config variable, not in presets.

## Tests
They need only a C++ compiler and the headers in this folder (no SDK). Compile the decoder together with a test, for example with MSVC from a developer prompt (stdafx.h pulls in MFC on Windows, so use an empty `stdafx.h` in a scratch copy) or with g++/clang:

    g++ -std=c++17 -O2 -I. tests/lfe_check.cpp freesurround_decoder.cpp channelmaps.cpp kiss_fft.cpp kiss_fftr.cpp -o lfe_check && ./lfe_check
    g++ -std=c++17 -O2 -I. tests/feat_check.cpp freesurround_decoder.cpp channelmaps.cpp kiss_fft.cpp kiss_fftr.cpp -o feat_check && ./feat_check
    g++ -std=c++17 -O2 -I. tests/widen.cpp freesurround_decoder.cpp channelmaps.cpp kiss_fft.cpp kiss_fftr.cpp -o widen && ./widen   # front/rear separation measurements

`tests/core_check.cpp` additionally needs the Boost headers and `tests/sdk_stub`.
