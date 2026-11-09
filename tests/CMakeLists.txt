# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(annokit_tests
    test_main.cpp
    unit/test_bink.cpp
    unit/test_core.cpp
    unit/test_data_file_write.cpp
    unit/test_loopback.cpp
    unit/test_random.cpp
    unit/test_resource_archive.cpp
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
# Tests dereference results right after checking them; a null there crashes the test, which reports
# it as failed. GCC's -Wnull-dereference flags such lines at -O2, so it stays on for the modules but
# is off for the tests.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(annokit_tests PRIVATE -Wno-null-dereference)
endif()

add_test(NAME unit COMMAND annokit_tests)
set_tests_properties(unit PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME layering COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckLayering.cmake)

if(ANNOKIT_BUILD_PLATFORM)
    add_executable(annokit_sdl_tests test_main.cpp sdl/test_audio_output.cpp)
    target_link_libraries(annokit_sdl_tests PRIVATE annokit::platform annokit::audio)
    annokit_set_warnings(annokit_sdl_tests)
    if(WIN32)
        # Windows finds DLLs next to the program: SDL3.dll goes there after every build. Without it
        # the test would wait on Windows' "DLL not found" box instead of failing.
        set(runtime_dlls $<TARGET_RUNTIME_DLLS:annokit_sdl_tests>)
        add_custom_command(TARGET annokit_sdl_tests POST_BUILD
            COMMAND "$<IF:$<BOOL:${runtime_dlls}>,${CMAKE_COMMAND};-E;copy_if_different;${runtime_dlls};$<TARGET_FILE_DIR:annokit_sdl_tests>,${CMAKE_COMMAND};-E;true>"
            COMMAND_EXPAND_LISTS
            VERBATIM)
    endif()
    add_test(NAME sdl COMMAND annokit_sdl_tests)
    # The tests take seconds; a hang fails after two minutes instead of using up the job.
    set_tests_properties(sdl PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120
                         ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
endif()
