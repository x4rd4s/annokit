# annokit

Shared, game-independent building blocks for C++20 game engines on SDL3: files and checksums,
data files, PNG, zip and resource archive reading, a deterministic random number generator, a
window layer, sound decoding and mixing, Smacker, Bink 2 and WebM video decoding, reliable UDP
transport, a sandboxed Lua host and self-updates from signed release manifests.

Licensed under GPL-3.0-or-later.

## Modules

| Target | Include prefix | Contents | Depends on |
|---|---|---|---|
| `annokit::core` | `annokit/core/` | `Result<T>`, bounded byte reading, files, CRC, zlib/PNG/zip/resource archive reading, data files, `Random` (PCG32) | (zlib, optional) |
| `annokit::audio` | `annokit/audio/` | WAVE/IMA ADPCM, Ogg Vorbis and FLAC decoding, a mixer with groups | core |
| `annokit::video` | `annokit/video/` | Smacker, Bink 2 and WebM (AV1 + Opus) decoding | audio, core |
| `annokit::net` | `annokit/net/` | A message transport: ENet over UDP, and a loopback with simulated loss and delay for tests | core |
| `annokit::script` | `annokit/script/` | A sandboxed Lua 5.4 host with instruction and memory limits | core |
| `annokit::update` | `annokit/update/` | Versions, Ed25519-signed update manifests, zip and tar.gz release packages, HTTPS through the system (WinHTTP, libcurl loaded at run time), installing with the previous version kept, restart | core |
| `annokit::platform` | `annokit/platform/` | SDL3 window, icons and audio device (`ANNOKIT_BUILD_PLATFORM=ON`) | core, SDL3 |

The modules may include only the modules they depend on; the `layering` test checks that.

## Using annokit

Pin a commit with FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(annokit
    GIT_REPOSITORY https://github.com/x4rd4s/annokit.git
    GIT_TAG <commit>)
set(ANNOKIT_BUILD_PLATFORM ON)
FetchContent_MakeAvailable(annokit)
target_link_libraries(my_game PRIVATE annokit::core annokit::audio annokit::platform)
```

## Building and testing

```sh
cmake --preset debug          # or release, asan, tsan, ci, coverage
cmake --build --preset debug
ctest --preset debug
```

Needs CMake 3.25, Ninja and a C++20 compiler (GCC, Clang or MSVC 2022, tested in CI). zlib is optional;
SDL3 (3.2 or later) is needed only for the platform module.

## Rules for contributions

- Code is formatted with clang-format 18 (`.clang-format`) and checked with clang-tidy.
- Readers of untrusted bytes are bounded and return `Result<T>`; nothing throws across the API.
- Everything here stays independent of any one game: `python3 -I tools/check_clean.py` fails on
  names of games, editions or projects, on data files of games and on references into other
  programs. Tests use synthetic data only.
- Third-party code is listed with origin, version and licence in `third_party/README.md`.
