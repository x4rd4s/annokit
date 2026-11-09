# SPDX-License-Identifier: GPL-3.0-or-later
# Test coverage with gcov (GCC, or Clang's gcov-compatible output). Enable with -DANNOKIT_COVERAGE=ON
# in a Debug build; CI's coverage job reports it with gcovr.

option(ANNOKIT_COVERAGE "Instrument the build for test coverage (gcov, GCC/Clang)" OFF)

if(ANNOKIT_COVERAGE)
    if(MSVC)
        message(FATAL_ERROR "ANNOKIT_COVERAGE needs GCC or Clang")
    endif()
    add_compile_options(--coverage)
    # Atomic counters: the atlas decodes on several threads, and racing counters give gcov
    # negative branch counts that gcovr refuses.
    add_compile_options(-fprofile-update=atomic)
    add_link_options(--coverage)
    message(STATUS "annokit: coverage instrumentation enabled")
endif()
