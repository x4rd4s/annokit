# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(annokit_tests
    test_main.cpp
    unit/test_bink.cpp
    unit/test_core.cpp
    unit/test_data_file_write.cpp
    unit/test_loopback.cpp
    unit/test_random.cpp
    unit/test_script_sandbox.cpp
    unit/test_wav_mixer.cpp
    unit/test_webm.cpp
    unit/test_zip_archive.cpp
)
target_link_libraries(annokit_tests PRIVATE annokit::core annokit::audio annokit::video annokit::net annokit::script)
# The Bink tests reach the decoder's internal headers.
target_include_directories(annokit_tests PRIVATE "${PROJECT_SOURCE_DIR}/modules/video/src")
target_compile_definitions(annokit_tests PRIVATE ANNOKIT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
annokit_set_warnings(annokit_tests)

add_test(NAME unit COMMAND annokit_tests)
set_tests_properties(unit PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME layering COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckLayering.cmake)

if(ANNOKIT_BUILD_PLATFORM)
    add_executable(annokit_sdl_tests test_main.cpp sdl/test_audio_output.cpp)
    target_link_libraries(annokit_sdl_tests PRIVATE annokit::platform annokit::audio)
    annokit_set_warnings(annokit_sdl_tests)
    add_test(NAME sdl COMMAND annokit_sdl_tests)
    set_tests_properties(sdl PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
endif()
