# Third-party code

Third-party code built with annokit, with origin, version and licence. Only GPL-3.0-compatible
licences.

| Library | Version | Licence | Origin | Purpose |
|---|---|---|---|---|
| ENet | 1.3.18 | MIT (`enet/LICENSE`) | https://github.com/lsalzman/enet, unchanged sources | Reliable UDP (`annokit::net`) |
| stb_vorbis | 1.22 (commit 1ee679c) | MIT or public domain (end of `stb_vorbis/stb_vorbis.c`) | https://github.com/nothings/stb, `stb_vorbis.c` unchanged | Ogg Vorbis sounds (`annokit::audio`) |
| Lua | 5.4.8 | MIT (`lua/LICENSE`, end of `lua/lua.h`) | https://github.com/lua/lua tag v5.4.8, unchanged sources without the interpreter, tests and the io, os, package and debug libraries; `lstate_fixed_seed.c` builds `lstate.c` with a fixed string hash seed | Sandboxed scripts (`annokit::script`) |
| dr_flac | 0.13.4 in development (commit d422edf of 2026-10-24) | MIT No Attribution or public domain (end of `dr_flac/dr_flac.h`) | https://github.com/mackron/dr_libs, `dr_flac.h` unchanged; taken after 0.13.3 for its fixes of overflows and crashes on damaged files | FLAC sounds (`annokit::audio`) |
| dav1d | 1.5.4 (commit 54706fc) | BSD-2-Clause (`dav1d/COPYING`) | https://github.com/videolan/dav1d tag 1.5.4 (mirror of code.videolan.org), unchanged `src/*.c`, `src/*.h`, `src/win32` and `include`; without the assembly, tools, tests and meson files; `CMakeLists.txt`, `config.h.in` and `include/vcs_version.h` are annokit's in place of what meson generates (C only, 8 bits per component) | AV1 pictures of WebM videos (`annokit::video`) |
| Opus | 1.6.1 (commit 22244de) | BSD-3-Clause (`opus/COPYING`) | https://github.com/xiph/opus tag v1.6.1, unchanged `celt`, `silk`, `silk/float`, `src` and `include` without the arch folders, the neural network parts (`dnn`) and the demo programs; `CMakeLists.txt` is annokit's (floating point, C only, sources of the `*_sources.mk` lists) | Opus sound of WebM videos (`annokit::video`) |
| Monocypher | 4.0.2 | BSD-2-Clause or CC0-1.0 (`monocypher/LICENCE.md`) | https://github.com/LoupVaillant/Monocypher tag 4.0.2, unchanged `src/monocypher.c`, `src/monocypher.h` and `src/optional/monocypher-ed25519.{c,h}`; `CMakeLists.txt` is annokit's | Ed25519 signatures of update manifests (`annokit::update`) |
