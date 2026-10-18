# 64-bit port notes

## Build

* Open `foo_dsp_fsurround.vs2022.sln` (VS 2022, toolset v143). Platforms: `x64` and `Win32`.
* Put this folder next to the foobar2000 SDK (`..\SDK`, `..\helpers`, `..\foobar2000_component_client`,
  `..\shared`, `..\..\pfc`, and `..\..\libPPUI` if your SDK has it). 64-bit components need
  foobar2000 v2.x and a 2023+ SDK.
* Set `BOOST_ROOT`. By default the project links the prebuilt static x64 Boost.Serialization
  (`b2 address-model=64 link=static runtime-link=static --with-serialization`). Build with
  `/p:VendoredBoostSerialization=true` to compile `sterilization\*.cpp` instead (those sources
  must match your Boost headers).
* The old `foo_dsp_fsurround.sln` / `foo_dsp_tutorial.vcproj` (VS2008, Win32 only) are untouched.

## Source changes

| File | Change | Why |
|---|---|---|
| `freesurround_decoder.cpp` | `memcpy` -> `memmove` when shifting `outbuf` | Source and destination overlap (undefined behaviour). Worked by luck with the old 32-bit CRT; a 64-bit CRT may copy in a different order. Found with ASan. |
| `freesurround_decoder.cpp` | `delete[] (char*)` for the two FFT configs | `kiss_fftr_alloc` uses `new char[]`; plain `delete` was a mismatched new/delete. |
| `freesurround_decoder.cpp` | `(unsigned)` cast in `num_channels` | `size_t` -> `unsigned` truncation (x64). |
| `stream_chunker.h` | size_t-safe `std::min`/`std::copy` | `chunk_len - buffer.size()` mixes `unsigned` and 64-bit `size_t`; breaks template deduction when the `min` macro is off. |
| `freesurround.cpp` | `audio_sample` instead of `float` for fb2k buffers | Does not assume the SDK's sample type. |
| `freesurround.cpp` | no temporaries bound to non-const refs | MSVC extension; rejected under `/permissive-`. |
| `freesurround.cpp` | `BOOST_BIND_GLOBAL_PLACEHOLDERS` | Keeps `_1` working with Boost >= 1.73. |
| `stdafx.h` | MFC include only on `_WIN32` | Lets the DSP core build/test anywhere. |

## New settings (dialog: "Center spread / block size")

* **Center spread** (slider, 0-100 %, default 0 = off): moves part of the front-center channel into
  front left/right with constant power (`cos`/`sin` law). Only affects layouts that have front
  L + C + R. At 0 the output is bit-identical to before. (The old `center_image` setting is stored but
  is not used by the decoder, so it is left alone: making it live would change how existing presets sound.)
* **Block size** (dropdown, 1024 / 2048 / **4096 (default)** / 8192 / 16384 samples per channel). Smaller =
  less delay and more time resolution; larger = finer frequency resolution. The decoder's delay is half a
  block (4096 -> 2048 samples).
* Presets are versioned (Boost class version 1). Presets saved before this change load with the defaults
  above, so existing settings keep sounding the same. A preset saved by this version cannot be read by an
  older build (it falls back to defaults).

## Verification done

* Decoder core: output of the 32-bit and 64-bit builds is bit-identical (3.2 M samples, all effects on);
  default-setting output is bit-identical to the original source; clean under ASan/UBSan for all block sizes.
* Presets: old-format presets load correctly into the new code (same architecture); new presets round-trip.
* `freesurround.cpp` was type-checked against a stub of the foobar2000 SDK/MFC (float and double
  `audio_sample`), not built with the real SDK, MFC or MSVC. The .rc/.vcxproj were not built either.

## Known risks / open items

* **32-bit <-> 64-bit presets (pre-existing).** Boost binary archives are not portable between 32- and
  64-bit: the archive header stores a `size_t`-sized string length. Verified with the original code too:
  a preset saved by a 32-bit build is not read by a 64-bit build (and vice versa); the existing
  `catch(...)` falls back to defaults and logs "Unsupported DSP config version". Same-architecture
  loading works. A small 32->64 converter could be added if this matters.
* `get_latency()` divides two `unsigned` values (integer division), so it always reports 0.
  Pre-existing and left unchanged on purpose.
* `std::strstream(buf, n)` as used for reading presets works with MSVC's runtime but not with
  libstdc++ (standard says the get area is empty in in|out mode); irrelevant for the MSVC build.
* `sterilization\` is spelled that way in the repo; the old `.vcproj` pointed at `serialization\`.
