# SPDX-License-Identifier: GPL-3.0-or-later
# Platform layer (SDL3): window, renderer, audio device, mouse cursor, timing. Only built with
# ANNOKIT_BUILD_PLATFORM.

find_package(SDL3 CONFIG COMPONENTS SDL3)
if(NOT SDL3_FOUND)
    message(FATAL_ERROR "SDL 3 not found, but ANNOKIT_BUILD_PLATFORM=ON needs it. Install it:\n"
        "  Debian 13, Ubuntu 25.04+:  sudo apt install libsdl3-dev\n"
        "  Fedora 41+:                sudo dnf install SDL3-devel\n"
        "  Arch:                      sudo pacman -S sdl3\n"
        "  macOS (Homebrew):          brew install sdl3\n"
        "Or point CMAKE_PREFIX_PATH at an SDL 3 built from source.")
endif()
# SDL3Config marks its headers as non-system; the strict warning flags would then apply to them.
if(TARGET SDL3::Headers)
    set_property(TARGET SDL3::Headers PROPERTY SYSTEM 1)
endif()

add_library(annokit_platform
    src/audio_output.cpp
    src/mouse_cursor.cpp
    src/window.cpp
)
add_library(annokit::platform ALIAS annokit_platform)

target_include_directories(annokit_platform PUBLIC include)
target_link_libraries(annokit_platform PUBLIC annokit::core SDL3::SDL3)
annokit_set_warnings(annokit_platform)
