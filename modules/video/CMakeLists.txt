# SPDX-License-Identifier: GPL-3.0-or-later
# Videos: Smacker, Bink 2 and WebM (AV1 with dav1d, Opus with libopus). No platform dependency (the
# frontend shows the frames and hands the sound to the mixer), so tests run them headless.

add_library(annokit_video
    src/bink.cpp
    src/bink2_video.cpp
    src/bink2_video_g.cpp
    src/bink_audio.cpp
    src/smacker.cpp
    src/webm.cpp
)
add_library(annokit::video ALIAS annokit_video)

target_include_directories(annokit_video PUBLIC include)
target_link_libraries(annokit_video PUBLIC annokit::audio annokit::core PRIVATE annokit::dav1d annokit::opus)
annokit_set_warnings(annokit_video)
