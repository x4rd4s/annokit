# SPDX-License-Identifier: GPL-3.0-or-later
# Sound decoding (WAVE, Ogg Vorbis, FLAC) and the mixer. No platform dependency (the device is
# platform::AudioOutput), so tests run it headless.

add_library(annokit_audio
    src/mixer.cpp
    src/sound_file.cpp
    src/wav.cpp
)
add_library(annokit::audio ALIAS annokit_audio)

target_include_directories(annokit_audio PUBLIC include)
target_link_libraries(annokit_audio PUBLIC annokit::core PRIVATE annokit_stb_vorbis annokit_dr_flac)
annokit_set_warnings(annokit_audio)
