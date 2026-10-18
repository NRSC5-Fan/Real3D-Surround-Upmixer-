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

## Known risks / open items

* **Saved presets.** Presets are Boost binary archives. Boost binary archives are not guaranteed
  portable between 32- and 64-bit (the `std::vector` element count is a `size_t`). A preset written
  by the 32-bit plugin may fail to load in x64; the existing `catch(...)` falls back to defaults
  and logs "Unsupported DSP config version". Test this with a real 32-bit preset.
* `get_latency()` divides two `unsigned` values (integer division), so it always reports 0.
  Pre-existing and unchanged here.
* `sterilization\` is spelled that way in the repo; the old `.vcproj` pointed at `serialization\`.
