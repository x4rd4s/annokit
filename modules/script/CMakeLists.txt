# SPDX-License-Identifier: GPL-3.0-or-later
# A sandboxed, deterministic Lua 5.4 state for scripts. No platform dependency, so tests run it
# headless.

add_library(annokit_script
    src/sandbox.cpp
)
add_library(annokit::script ALIAS annokit_script)

target_include_directories(annokit_script PUBLIC include)
target_link_libraries(annokit_script PUBLIC annokit::core PRIVATE annokit_lua)
annokit_set_warnings(annokit_script)
