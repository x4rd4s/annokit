# annokit - shared building blocks for C++20 game engines
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.25)

project(annokit
    VERSION 0.1.0
    DESCRIPTION "Shared, game-independent building blocks for C++20 game engines on SDL3"
    LANGUAGES C CXX)

if(PROJECT_IS_TOP_LEVEL AND PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a build directory or a CMake preset.")
endif()

# 64-bit systems only.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "annokit needs a 64-bit target; 32-bit builds are not supported.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    if(MSVC)
        add_compile_options(/utf-8)
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    endif()
    add_compile_definitions($<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>)
endif()

# By path, not through CMAKE_MODULE_PATH: a project that uses annokit may have modules of the same
# names. As a subproject, annokit takes the sanitizer and coverage flags of the project around it.
include("${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake")
if(PROJECT_IS_TOP_LEVEL)
    include("${PROJECT_SOURCE_DIR}/cmake/Sanitizers.cmake")
    include("${PROJECT_SOURCE_DIR}/cmake/Coverage.cmake")
endif()

option(ANNOKIT_BUILD_TESTS "Build the tests of annokit" ${PROJECT_IS_TOP_LEVEL})
option(ANNOKIT_BUILD_PLATFORM "Build the SDL3 platform module (window, audio device)" OFF)
option(ANNOKIT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# zlib for compressed streams and PNG; without it, compress.hpp reports Unsupported.
find_package(ZLIB QUIET)
find_package(Threads REQUIRED)

add_subdirectory(third_party/enet)
add_subdirectory(third_party/stb_vorbis)
add_subdirectory(third_party/dr_flac)
add_subdirectory(third_party/lua)
add_subdirectory(third_party/dav1d)
add_subdirectory(third_party/opus)
add_subdirectory(third_party/monocypher)
add_subdirectory(modules)

if(ANNOKIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
